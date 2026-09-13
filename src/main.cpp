#include "window.h"
#include <QApplication>
#include <QStandardPaths>
#include <QLockFile>
#include <QIcon>
#include <QDir>
#include <QFont>
#include <QMessageBox>
#include <QTimer>
#include <fpdfview.h>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Document Viewer");
    app.setWindowIcon(QIcon(":/icons/DocumentViewer.png"));
    if (app.arguments().contains("--smoke-test")) {
        FPDF_InitLibrary();
        { Window window; app.processEvents(); }
        FPDF_DestroyLibrary();
        return 0;
    }
#ifdef Q_OS_WIN
    // Windows releases this named mutex even if the owning process crashes.
    const HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\DocumentViewer.SingleInstance");
    const DWORD mutexError = GetLastError();
    if (!instanceMutex) {
        QMessageBox::critical(nullptr, "Document Viewer", "Could not acquire the application instance lock.");
        return 1;
    }
    if (mutexError == ERROR_ALREADY_EXISTS) {
        CloseHandle(instanceMutex);
        return 0;
    }
#else
    const auto dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(dataDirectory)) return 1;
    QLockFile instanceLock(dataDirectory + "/instance.lock");
    instanceLock.setStaleLockTime(0); // A live process never expires; crashed processes are detected.
    if (!instanceLock.tryLock()) return instanceLock.error() == QLockFile::LockFailedError ? 0 : 1;
#endif
    app.setStyle("Fusion");
    app.setFont(QFont("Segoe UI", 10));
    FPDF_InitLibrary();
    int result;
    {
        Window window(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/session.json");
        window.show();
        if (app.arguments().size() > 1) {
            const auto paths = app.arguments().mid(1);
            QTimer::singleShot(0, &window, [&window, paths] {
                for (const auto &path : paths) window.openDocument(path);
            });
        }
        result = app.exec();
    }
    FPDF_DestroyLibrary();
#ifdef Q_OS_WIN
    CloseHandle(instanceMutex);
#endif
    return result;
}
