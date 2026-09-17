#include "fileassociations.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <QSettings>
#include <qt_windows.h>
#include <shlobj.h>
#elif defined(Q_OS_MACOS)
#include <CoreServices/CoreServices.h>
#elif defined(Q_OS_LINUX)
#include <QImage>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#endif

QString registerDocumentFileTypes() {
#ifdef Q_OS_WIN
    // The packaged application runs from a temporary extraction directory.
    QString executable = qEnvironmentVariable("DOCUMENT_VIEWER_PORTABLE_PATH");
    if (executable.isEmpty()) executable = QCoreApplication::applicationFilePath();
    if (!QFileInfo(executable).isFile()) return "Could not find the Document Viewer executable.";
    executable = QDir::toNativeSeparators(QFileInfo(executable).absoluteFilePath());
    const QString command = '"' + executable + "\" \"%1\"";
    const QString icon = '"' + executable + "\",0";
    QSettings registry("HKEY_CURRENT_USER\\Software", QSettings::NativeFormat);
    const QString capabilities = "DocumentViewer/Capabilities/";
    registry.setValue(capabilities + "ApplicationName", "Document Viewer");
    registry.setValue(capabilities + "ApplicationDescription", "Read PDF and EPUB documents.");
    registry.setValue(capabilities + "ApplicationIcon", icon);
    for (const auto &extension : {QString("pdf"), QString("epub")}) {
        const QString id = "DocumentViewer." + extension;
        const QString type = "Classes/" + id + '/';
        registry.setValue(type + "Default", "Document Viewer " + extension.toUpper() + " document");
        registry.setValue(type + "DefaultIcon/Default", icon);
        registry.setValue(type + "shell/open/command/Default", command);
        registry.setValue("Classes/." + extension + "/OpenWithProgids/" + id, QString());
        registry.setValue(capabilities + "FileAssociations/." + extension, id);
    }
    registry.setValue("RegisteredApplications/Document Viewer", "Software\\DocumentViewer\\Capabilities");
    registry.sync();
    if (registry.status() != QSettings::NoError) return "Could not save file types in your user registry.";
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return {};
#elif defined(Q_OS_MACOS)
    QDir directory(QCoreApplication::applicationDirPath());
    if (!directory.cdUp() || !directory.cdUp() || !directory.dirName().endsWith(".app"))
        return "Run Document Viewer from its installed DocumentViewer.app bundle.";
    const auto path = directory.absolutePath().toUtf8();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr,
        reinterpret_cast<const UInt8 *>(path.constData()), path.size(), true);
    if (!url) return "Could not locate the application bundle.";
    const auto result = LSRegisterURL(url, true);
    CFRelease(url);
    if (result != noErr) return QString("Could not register DocumentViewer.app (error %1).").arg(result);
    return {};
#elif defined(Q_OS_LINUX)
    QString executable = qEnvironmentVariable("APPIMAGE");
    if (executable.isEmpty()) executable = QCoreApplication::applicationFilePath();
    if (!QFileInfo(executable).isExecutable()) return "Could not find the Document Viewer executable.";
    // Quote one Exec argument, then escape it for the desktop entry string format.
    QString quoted;
    for (const auto character : QFileInfo(executable).absoluteFilePath()) {
        if (QString("\\\"`$").contains(character)) quoted += '\\';
        quoted += character;
        if (character == '%') quoted += '%';
    }
    quoted.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t");
    const auto data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const auto applications = data + "/applications";
    const auto icons = data + "/icons/hicolor/256x256/apps";
    if (data.isEmpty() || !QDir().mkpath(applications) || !QDir().mkpath(icons))
        return "Could not create application registration directories.";
    if (!QImage(":/icons/DocumentViewer.png").scaled(256, 256).save(icons + "/DocumentViewer.png"))
        return "Could not save the Document Viewer icon.";
    const auto contents = QString("[Desktop Entry]\nType=Application\nName=Document Viewer\n"
        "Exec=\"%1\" %F\nIcon=DocumentViewer\nCategories=Office;Viewer;\n"
        "MimeType=application/pdf;application/epub+zip;\nTerminal=false\n").arg(quoted).toUtf8();
    QSaveFile entry(applications + "/DocumentViewer.desktop");
    if (!entry.open(QIODevice::WriteOnly) || entry.write(contents) != contents.size() || !entry.commit())
        return "Could not save the application desktop entry: " + entry.errorString();
    const auto refresh = QStandardPaths::findExecutable("update-desktop-database");
    if (!refresh.isEmpty()) {
        QProcess process;
        process.start(refresh, {applications});
        if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode()) {
            process.kill();
            process.waitForFinished(1000);
            return "Registration was saved, but refreshing Open With failed. Sign out and back in to refresh it.";
        }
    }
    return {};
#else
    return "File type registration is not available on this platform.";
#endif
}
