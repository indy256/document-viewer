#include "fileassociations.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <qt_windows.h>

class FileAssociationsTests : public QObject {
    Q_OBJECT
private slots:
    void registration() {
        // Redirect HKCU for this process so the real associations are untouched.
        const auto keyName = ("Software\\DocumentViewer-Test-" +
            QUuid::createUuid().toString(QUuid::Id128)).toStdWString();
        HKEY temporary = nullptr;
        QCOMPARE(RegCreateKeyExW(HKEY_CURRENT_USER, keyName.c_str(), 0, nullptr, 0,
            KEY_ALL_ACCESS, nullptr, &temporary, nullptr), ERROR_SUCCESS);
        const auto cleanup = qScopeGuard([&] {
            RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            RegCloseKey(temporary);
            RegDeleteTreeW(HKEY_CURRENT_USER, keyName.c_str());
        });
        QCOMPARE(RegOverridePredefKey(HKEY_CURRENT_USER, temporary), ERROR_SUCCESS);
        const auto oldPath = qgetenv("DOCUMENT_VIEWER_PORTABLE_PATH");
        const auto restoreEnvironment = qScopeGuard([&] {
            if (oldPath.isNull()) qunsetenv("DOCUMENT_VIEWER_PORTABLE_PATH");
            else qputenv("DOCUMENT_VIEWER_PORTABLE_PATH", oldPath);
        });
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto portable = directory.filePath("Portable Viewer.exe");
        QFile executable(portable);
        QVERIFY(executable.open(QIODevice::WriteOnly));
        executable.close();
        qputenv("DOCUMENT_VIEWER_PORTABLE_PATH", portable.toUtf8());
        QSettings registry("HKEY_CURRENT_USER\\Software", QSettings::NativeFormat);
        registry.setValue("Classes/.pdf/Default", "Existing.PDF");
        registry.setValue("Classes/.epub/Default", "Existing.EPUB");
        registry.sync();
        QCOMPARE(registerDocumentFileTypes(), QString());
        QCOMPARE(registerDocumentFileTypes(), QString()); // Re-registration is safe.
        registry.sync();
        for (const auto &extension : {QString("pdf"), QString("epub")}) {
            const auto id = "DocumentViewer." + extension;
            QCOMPARE(registry.value("Classes/" + id + "/shell/open/command/Default").toString(),
                '"' + QDir::toNativeSeparators(portable) + "\" \"%1\"");
            QVERIFY(registry.contains("Classes/." + extension + "/OpenWithProgids/" + id));
            QCOMPARE(registry.value("DocumentViewer/Capabilities/FileAssociations/." + extension).toString(), id);
            QCOMPARE(registry.value("Classes/." + extension + "/Default").toString(), "Existing." + extension.toUpper());
        }
        QCOMPARE(registry.value("RegisteredApplications/Document Viewer").toString(),
            QString("Software\\DocumentViewer\\Capabilities"));
        qunsetenv("DOCUMENT_VIEWER_PORTABLE_PATH");
        QCOMPARE(registerDocumentFileTypes(), QString());
        registry.sync();
        QCOMPARE(registry.value("Classes/DocumentViewer.pdf/shell/open/command/Default").toString(),
            '"' + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\" \"%1\"");
        qputenv("DOCUMENT_VIEWER_PORTABLE_PATH", directory.filePath("missing.exe").toUtf8());
        QVERIFY(!registerDocumentFileTypes().isEmpty());
    }
};

QTEST_GUILESS_MAIN(FileAssociationsTests)
#include "fileassociations_tests.moc"
