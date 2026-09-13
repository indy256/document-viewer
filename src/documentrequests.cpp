#include "documentrequests.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#ifndef Q_OS_WIN
#include <QLocalSocket>
#endif
#include <QThread>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>

namespace {
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
            || !request->cbData || request->cbData >= 1024 * 1024) return FALSE;
        // WM_COPYDATA's buffer belongs to the sender; copy it before returning.
        const auto document = QJsonDocument::fromJson(QByteArray(
            static_cast<const char *>(request->lpData), request->cbData));
        if (!document.isArray()) return FALSE;
        QStringList paths;
        for (const auto &value : document.array()) {
            if (!value.isString()) return FALSE;
            paths.append(value.toString());
        }
        auto receiver = reinterpret_cast<DocumentRequests *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!receiver) return FALSE;
        emit receiver->received(paths);
        return TRUE;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}

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
    if (message.size() >= 1024 * 1024) return false;
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
            socket->setReadBufferSize(1024 * 1024);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(10000, socket, [socket] { socket->abort(); socket->deleteLater(); });
            auto read = [this, socket] {
                if (!socket->canReadLine()) {
                    if (socket->bytesAvailable() >= 1024 * 1024) socket->abort();
                    return;
                }
                const auto document = QJsonDocument::fromJson(socket->readLine());
                if (!document.isArray()) { socket->abort(); return; }
                QStringList paths;
                for (const auto &value : document.array()) {
                    if (!value.isString()) { socket->abort(); return; }
                    paths.append(value.toString());
                }
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
    if (message.size() >= 1024 * 1024 || socket.write(message) != message.size()) return false;
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
