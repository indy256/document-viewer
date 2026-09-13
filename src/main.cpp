#include "window.h"
#include "documentrequests.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QApplication>
#include <QStandardPaths>
#include <QLockFile>
#include <QIcon>
#include <QDir>
#include <QFont>
#include <QMessageBox>
#include <QTimer>
#include <QScopeGuard>
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
    QStringList paths;
    for (const auto &path : app.arguments().mid(1)) paths.append(QFileInfo(path).absoluteFilePath());
    const auto dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QString serverName = "DocumentViewer-" + QString::fromLatin1(
        QCryptographicHash::hash(dataDirectory.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
#ifdef Q_OS_WIN
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    serverName += "-" + QString::number(sessionId);
#endif
    auto forward = [&] {
        if (DocumentRequests::send(serverName, paths)) return 0;
        QMessageBox::warning(nullptr, "Document Viewer", "Could not send documents to the running application. Please try again.");
        return 1;
    };
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
        return forward();
    }
    const auto releaseInstance = qScopeGuard([&] { CloseHandle(instanceMutex); });
#else
    if (!QDir().mkpath(dataDirectory)) return 1;
    QLockFile instanceLock(dataDirectory + "/instance.lock");
    instanceLock.setStaleLockTime(0); // A live process never expires; crashed processes are detected.
    if (!instanceLock.tryLock()) return instanceLock.error() == QLockFile::LockFailedError ? forward() : 1;
#endif
    DocumentRequests requests;
    if (!requests.listen(serverName)) {
        QMessageBox::critical(nullptr, "Document Viewer", "Could not start the document request service.");
        return 1;
    }
    app.setStyle("Fusion");
    app.setFont(QFont("Segoe UI", 10));
    FPDF_InitLibrary();
    int result;
    {
        Window window(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/session.json");
        window.show();
        QObject::connect(&requests, &DocumentRequests::received, &window, [&window](const QStringList &requested) {
            for (const auto &path : requested) window.openDocument(path);
            if (window.isMinimized()) window.showNormal();
            window.raise();
            window.activateWindow();
        }, Qt::QueuedConnection);
        QTimer::singleShot(0, &window, [&window, paths] {
            for (const auto &path : paths) window.openDocument(path);
        });
        result = app.exec();
    }
    FPDF_DestroyLibrary();
    return result;
}
