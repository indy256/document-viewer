#include "updater.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

static void initializeUpdateResources() { Q_INIT_RESOURCE(updater_scripts); }

namespace Updater {
bool readRelease(const QByteArray &json, const QString &assetName, Asset &asset, QString &error) {
    asset = {};
    const auto document = QJsonDocument::fromJson(json);
    const auto release = document.object();
    if (!document.isObject() || release.value("tag_name").toString().isEmpty() ||
        !release.value("draft").isBool() || !release.value("prerelease").isBool() ||
        release.value("draft").toBool() || release.value("prerelease").toBool()) {
        error = "GitHub did not return a published stable release.";
        return false;
    }
    for (const auto &entry : release.value("assets").toArray()) {
        const auto item = entry.toObject();
        if (item.value("name").toString().compare(assetName, Qt::CaseInsensitive)) continue;
        const QUrl url(item.value("browser_download_url").toString());
        const auto checksum = item.value("digest").toString();
        const auto size = item.value("size").toInteger();
        if (!url.isValid() || url.scheme() != "https" || url.host() != "github.com" ||
            !url.userInfo().isEmpty() || url.port(-1) != -1 || url.hasQuery() || url.hasFragment() ||
            !url.path().startsWith("/indy256/document-viewer/releases/download/") ||
            !QRegularExpression("^sha256:[0-9a-f]{64}$").match(checksum).hasMatch() ||
            size <= 0 || size > 1024LL * 1024 * 1024) {
            error = "The release has invalid download, size, or SHA-256 information.";
            return false;
        }
        asset = {release.value("tag_name").toString(), url, checksum.mid(7).toLatin1(), size};
        return true;
    }
    error = "The latest release does not include " + assetName + ".";
    return false;
}

QByteArray fileHash(const QString &path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
    return hash.result().toHex();
}

namespace {
QProcessEnvironment cleanEnvironment() {
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto key : {"DOCUMENT_VIEWER_PORTABLE_PATH", "DOCUMENT_VIEWER_LAUNCHER_PID"})
        environment.remove(key);
#ifdef Q_OS_LINUX
    for (const auto key : {"APPIMAGE", "APPDIR", "OWD", "LD_LIBRARY_PATH", "LD_PRELOAD",
                           "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"})
        environment.remove(key);
#endif
    return environment;
}

bool write(const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

bool execute(QProgressDialog &progress, const QString &program, const QStringList &args, QString &error) {
    if (progress.wasCanceled()) return false;
    QProcess child;
    child.setProcessEnvironment(cleanEnvironment());
    QEventLoop events;
    QTimer limit;
    limit.setSingleShot(true);
    QObject::connect(&child, &QProcess::finished, &events, &QEventLoop::quit);
    QObject::connect(&child, &QProcess::errorOccurred, &events, &QEventLoop::quit);
    QObject::connect(&progress, &QProgressDialog::canceled, &events, &QEventLoop::quit);
    QObject::connect(&limit, &QTimer::timeout, &events, &QEventLoop::quit);
    child.start(program, args);
    limit.start(330000);
    events.exec();
    if (child.state() != QProcess::NotRunning) {
        child.kill();
        child.waitForFinished(5000);
    }
    if (progress.wasCanceled()) return false;
    if (!limit.isActive() || child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0 ||
        child.error() == QProcess::FailedToStart) {
        error = !limit.isActive() ? "The update operation timed out." :
            QString::fromUtf8(child.readAllStandardError()).trimmed().left(1500);
        if (error.isEmpty()) error = child.errorString();
        return false;
    }
    return true;
}
}

bool installLatest(QWidget *parent, const std::function<bool()> &saveSession) {
    initializeUpdateResources();
    const auto notify = [parent](const QString &message) {
        QMessageBox::warning(parent, "Update to latest version", message);
        return false;
    };
    QString target, assetName;
    qint64 launcherPid = 0;
#ifdef Q_OS_WIN
    target = qEnvironmentVariable("DOCUMENT_VIEWER_PORTABLE_PATH");
    launcherPid = qEnvironmentVariable("DOCUMENT_VIEWER_LAUNCHER_PID").toLongLong();
    if (target.isEmpty() || launcherPid <= 0)
        return notify("Run the portable Document Viewer EXE to update it automatically.");
#if defined(Q_PROCESSOR_ARM_64)
    assetName = "dv-windows-arm64.exe";
#elif defined(Q_PROCESSOR_X86_64)
    assetName = "dv-windows-x64.exe";
#endif
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)
    target = qEnvironmentVariable("APPIMAGE");
    assetName = "dv-linux-x64.AppImage";
    if (target.isEmpty()) return notify("Run the AppImage release to update it automatically.");
#elif defined(Q_OS_MACOS) && defined(Q_PROCESSOR_ARM_64)
    QDir bundle(QCoreApplication::applicationDirPath());
    if (!bundle.cdUp() || !bundle.cdUp() || !bundle.dirName().endsWith(".app"))
        return notify("Run the installed DocumentViewer.app to update it automatically.");
    target = bundle.absolutePath();
    assetName = "dv-macos-arm64.dmg";
#endif
    if (assetName.isEmpty()) return notify("There is no release for this platform and architecture.");
    target = QFileInfo(target).canonicalFilePath();
    if (target.isEmpty()) return notify("Could not locate the installed application.");
#ifdef Q_OS_WIN
    const auto system = qEnvironmentVariable("SystemRoot") + "/System32/";
    const auto curl = system + "curl.exe";
    const auto shell = system + "WindowsPowerShell/v1.0/powershell.exe";
#else
    const auto curl = QStandardPaths::findExecutable("curl");
    const QString shell = "/bin/sh";
#endif
    if (!QFileInfo::exists(curl) || !QFileInfo::exists(shell))
        return notify("Updating requires curl and the system scripting shell.");
    QTemporaryDir staging(QFileInfo(target).absolutePath() + "/.dv-update-XXXXXX");
    if (!staging.isValid()) return notify("The application folder is not writable. Move the app to a writable folder and try again.");
    const auto directory = staging.path();
    QProgressDialog progress("Checking the latest release...", "Cancel", 0, 0, parent);
    progress.setWindowTitle("Update to latest version");
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.show();
    QString error;
    const auto failure = [&](const QString &message = QString()) {
        progress.hide();
        return progress.wasCanceled() ? false : notify(message.isEmpty() ? error : message);
    };
    const auto download = [&](const QUrl &url, const QString &path, qint64 maxSize) {
        return execute(progress, curl, {"--fail", "--location", "--silent", "--show-error",
            "--proto", "=https", "--proto-redir", "=https", "--connect-timeout", "15",
            "--max-time", "300", "--max-filesize", QString::number(maxSize),
            "--user-agent", "DocumentViewer-Updater", "--output", path,
            url.toString(QUrl::FullyEncoded)}, error);
    };
    const auto metadataPath = directory + "/release.json";
    if (!download(QUrl("https://api.github.com/repos/indy256/document-viewer/releases/latest"),
                  metadataPath, 2 * 1024 * 1024)) return failure();
    QFile metadata(metadataPath);
    Asset asset;
    if (!metadata.open(QIODevice::ReadOnly) || metadata.size() > 2 * 1024 * 1024)
        return failure("Could not read the release information.");
    const auto bytes = metadata.readAll();
    metadata.close();
    if (!readRelease(bytes, assetName, asset, error)) return failure();
#ifndef Q_OS_MACOS
    if (fileHash(target) == asset.sha256) {
        progress.hide();
        QMessageBox::information(parent, "Update to latest version", "You already have the latest version.");
        return false;
    }
#endif
    progress.setLabelText("Downloading " + asset.version + "...");
    QString source = directory + "/replacement";
    if (!download(asset.url, source, asset.size)) return failure();
    progress.setLabelText("Verifying the download...");
    if (QFileInfo(source).size() != asset.size || fileHash(source) != asset.sha256)
        return failure("The download failed verification. The installed app has not been changed.");
#ifdef Q_OS_MACOS
    const auto mount = directory + "/mount";
    QDir().mkpath(mount);
    const bool mounted = execute(progress, "/usr/bin/hdiutil",
        {"attach", "-readonly", "-nobrowse", "-mountpoint", mount, source}, error);
    source = directory + "/replacement.app";
    const bool copied = mounted && execute(progress, "/usr/bin/ditto",
        {mount + "/DocumentViewer.app", source}, error);
    QProcess detach;
    detach.start("/usr/bin/hdiutil", {"detach", mount});
    if (!detach.waitForFinished(15000)) { detach.kill(); detach.waitForFinished(1000); }
    if (detach.exitStatus() != QProcess::NormalExit || detach.exitCode() != 0) {
        // A failed mount may still have attached a volume. Never delete through it.
        staging.setAutoRemove(false);
        return failure("Could not detach the update disk image. Files were retained at " + directory);
    }
    if (!copied) return failure();
    if (!QFileInfo(source + "/Contents/MacOS/DocumentViewer").isExecutable())
        return failure("The disk image does not contain DocumentViewer.app.");
#else
    if (!QFile::setPermissions(source, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                              QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther))
        return failure("Could not prepare the replacement application.");
#endif
    if (progress.wasCanceled()) return false;
    QProcess helper;
    helper.setProgram(shell);
    helper.setWorkingDirectory(QFileInfo(target).absolutePath());
    helper.setProcessEnvironment(cleanEnvironment());
#ifdef Q_OS_WIN
    // An inherited log handle would prevent the helper deleting its staging folder.
    helper.setStandardOutputFile(QProcess::nullDevice());
#else
    helper.setStandardOutputFile(directory + "/helper.log");
#endif
    helper.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    const auto script = directory + "/install.ps1";
    const auto planPath = directory + "/plan.json";
    QJsonObject plan{{"target", target}, {"viewerPid", QCoreApplication::applicationPid()},
                     {"launcherPid", launcherPid}};
    if (!QFile::copy(":/updates/install.ps1", script) || !write(planPath, QJsonDocument(plan).toJson()))
        return failure("Could not prepare the update helper.");
    helper.setArguments({"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                         "-WindowStyle", "Hidden", "-File", script, "-PlanPath", planPath});
    helper.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#else
    const auto script = directory + "/install.sh";
    if (!QFile::copy(":/updates/install.sh", script))
        return failure("Could not prepare the update helper.");
    helper.setArguments({script, target, source, QString::number(QCoreApplication::applicationPid())});
#endif
    if (!helper.startDetached())
        return failure("Could not start the update helper. The app has not been changed.");
    staging.setAutoRemove(false); // The helper owns the staged files from here on.
    progress.setLabelText("Preparing to restart...");
    QEventLoop events;
    QTimer poll, deadline;
    QObject::connect(&poll, &QTimer::timeout, &events, [&] {
        if (QFileInfo::exists(directory + "/ready") || QFileInfo::exists(directory + "/failed")) events.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &events, &QEventLoop::quit);
    QObject::connect(&progress, &QProgressDialog::canceled, &events, &QEventLoop::quit);
    deadline.setSingleShot(true);
    poll.start(50);
    deadline.start(15000);
    events.exec();
    if (progress.wasCanceled()) return false;
    if (!QFileInfo::exists(directory + "/ready"))
        return failure("The update helper could not prepare the restart. Details: " + directory + "/helper.log");
    if (!saveSession())
        return failure("Could not save the reading session. The update has been canceled.");
    if (!write(directory + "/commit", "install"))
        return failure("Could not hand off the update. The app has not been changed.");
    return true;
}
}
