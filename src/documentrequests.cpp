#include "documentrequests.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#ifndef Q_OS_WIN
#include <QLocalSocket>
#endif
#include <QThread>
#include <QTimer>
#include <QFileOpenEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

bool DocumentRequests::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::FileOpen) {
        const auto file = static_cast<QFileOpenEvent *>(event)->file();
        if (!file.isEmpty()) emit received({file});
        event->accept();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

namespace {
constexpr int maxRequestSize = 1024 * 1024;

bool decodePaths(const QByteArray &message, QStringList &paths) {
    const auto document = QJsonDocument::fromJson(message);
    if (!document.isArray()) return false;
    for (const auto &value : document.array()) {
        if (!value.isString()) return false;
        paths.append(value.toString());
    }
    return true;
}

#ifdef Q_OS_WIN
constexpr wchar_t windowClass[] = L"DocumentViewer.RequestWindow.v1";
constexpr ULONG_PTR requestType = 0x44565231;

LRESULT CALLBACK requestWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto creation = reinterpret_cast<CREATESTRUCTW *>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    if (message == WM_COPYDATA) {
        const auto request = reinterpret_cast<const COPYDATASTRUCT *>(lparam);
        if (!request || request->dwData != requestType || !request->lpData
            || !request->cbData || request->cbData >= maxRequestSize) return FALSE;
        // WM_COPYDATA's buffer belongs to the sender; copy it before returning.
        QStringList paths;
        if (!decodePaths(QByteArray(static_cast<const char *>(request->lpData), request->cbData), paths))
            return FALSE;
        auto receiver = reinterpret_cast<DocumentRequests *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!receiver) return FALSE;
        emit receiver->received(paths);
        return TRUE;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
#endif
}

#ifdef Q_OS_WIN
DocumentRequests::~DocumentRequests() {
    if (messageWindow) DestroyWindow(static_cast<HWND>(messageWindow));
}

bool DocumentRequests::listen(const QString &name) {
    WNDCLASSW type{};
    type.lpfnWndProc = requestWindowProc;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = windowClass;
    if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    const auto title = name.toStdWString();
    messageWindow = CreateWindowExW(0, windowClass, title.c_str(), 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, type.hInstance, this);
    return messageWindow != nullptr;
}

bool DocumentRequests::send(const QString &name, const QStringList &paths) {
    const auto title = name.toStdWString();
    QElapsedTimer timer;
    timer.start();
    HWND receiver = nullptr;
    do {
        receiver = FindWindowExW(HWND_MESSAGE, nullptr, windowClass, title.c_str());
        if (receiver) break;
        QThread::msleep(20);
    } while (timer.elapsed() < 5000);
    if (!receiver) return false;
    auto message = QJsonDocument(QJsonArray::fromStringList(paths)).toJson(QJsonDocument::Compact);
    if (message.size() >= maxRequestSize) return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(receiver, &processId);
    AllowSetForegroundWindow(processId);
    COPYDATASTRUCT request{requestType, static_cast<DWORD>(message.size()), message.data()};
    DWORD_PTR accepted = FALSE;
    return SendMessageTimeoutW(receiver, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&request),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &accepted) && accepted == TRUE;
}
#else
bool DocumentRequests::listen(const QString &name) {
    QLocalServer::removeServer(name); // Safe only while owning the instance lock.
    server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server, &QLocalServer::newConnection, this, [this] {
        while (auto socket = server.nextPendingConnection()) {
            socket->setReadBufferSize(maxRequestSize);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(10000, socket, [socket] { socket->abort(); socket->deleteLater(); });
            auto read = [this, socket] {
                if (!socket->canReadLine()) {
                    if (socket->bytesAvailable() >= maxRequestSize) socket->abort();
                    return;
                }
                QStringList paths;
                if (!decodePaths(socket->readLine(), paths)) { socket->abort(); return; }
                disconnect(socket, &QLocalSocket::readyRead, socket, nullptr);
                socket->write("OK\n");
                // Let the client close after reading the acknowledgment. Closing
                // here can race its pending write completion on Windows pipes.
                emit received(paths);
            };
            connect(socket, &QLocalSocket::readyRead, socket, read);
            read();
        }
    });
    return server.listen(name);
}

bool DocumentRequests::send(const QString &name, const QStringList &paths) {
    QLocalSocket socket;
    QElapsedTimer timer;
    timer.start();
    // A simultaneous launch may own the lock before its server is listening.
    do {
        socket.connectToServer(name);
        if (socket.waitForConnected(100)) break;
        socket.abort();
        QThread::msleep(20);
    } while (timer.elapsed() < 5000);
    if (socket.state() != QLocalSocket::ConnectedState) return false;
    const auto message = QJsonDocument(QJsonArray::fromStringList(paths)).toJson(QJsonDocument::Compact) + '\n';
    if (message.size() >= maxRequestSize || socket.write(message) != message.size()) return false;
    while (socket.bytesToWrite()) {
        if (!socket.waitForBytesWritten(5000)) return false;
    }
    QByteArray reply;
    while (!reply.contains('\n')) {
        if (!socket.bytesAvailable() && !socket.waitForReadyRead(5000)) return false;
        reply += socket.readAll();
    }
    return reply == "OK\n";
}

#endif
