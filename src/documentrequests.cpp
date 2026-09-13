#include "documentrequests.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QThread>
#include <QTimer>

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
