#include "documentrequests.h"
#include <QCoreApplication>
#include <QProcess>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <QFileOpenEvent>

class RequestTests : public QObject {
    Q_OBJECT
private slots:
    void fileOpenEvent() {
        DocumentRequests requests;
        QCoreApplication::instance()->installEventFilter(&requests);
        QSignalSpy received(&requests, &DocumentRequests::received);
        QFileOpenEvent event("/documents/book with spaces.epub");
        QCoreApplication::sendEvent(QCoreApplication::instance(), &event);
        QCOMPARE(received.count(), 1);
        QCOMPARE(received.first().first().toStringList(), QStringList{"/documents/book with spaces.epub"});
        QVERIFY(event.isAccepted());
    }
#ifndef Q_OS_WIN
    void partialRequest() {
        DocumentRequests requests;
        QSignalSpy received(&requests, &DocumentRequests::received);
        const QString name = "dv-test-" + QUuid::createUuid().toString(QUuid::Id128);
        QVERIFY(requests.listen(name));
        QLocalSocket socket;
        socket.connectToServer(name);
        QVERIFY(socket.waitForConnected());
        socket.write("[\"/first.pdf\",");
        socket.flush();
        QTest::qWait(30);
        QCOMPARE(received.count(), 0);
        socket.write("\"/second.epub\"]\n");
        socket.flush();
        QTRY_COMPARE(received.count(), 1);
        QCOMPARE(received.at(0).at(0).toStringList(), QStringList({"/first.pdf", "/second.epub"}));
        QTRY_VERIFY(socket.bytesAvailable() > 0);
        // The server must not close while the client is still awaiting its reply.
        QCOMPARE(socket.state(), QLocalSocket::ConnectedState);
        QCOMPARE(socket.readAll(), QByteArray("OK\n"));
        socket.disconnectFromServer();
    }
#endif
    void forwarding_data() {
        QTest::addColumn<int>("delay");
        QTest::newRow("running") << 0;
        QTest::newRow("starting") << 250;
    }
    void forwarding() {
        QFETCH(int, delay);
        DocumentRequests requests;
        QSignalSpy received(&requests, &DocumentRequests::received);
        const QString name = "dv-test-" + QUuid::createUuid().toString(QUuid::Id128);
        bool listening = false;
        if (delay) QTimer::singleShot(delay, [&] { listening = requests.listen(name); });
        else listening = requests.listen(name);
        QProcess child;
        const QStringList paths = {"/documents/first book.pdf", QString::fromUtf8("/books/?????.epub")};
        child.start(QCoreApplication::applicationFilePath(), QStringList{"--send", name} + paths);
        QVERIFY(child.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 10000);
        QVERIFY(listening);
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        QCOMPARE(received.count(), 1);
        QCOMPARE(received.at(0).at(0).toStringList(), paths);
        // Another launch without paths still requests activation.
        child.start(QCoreApplication::applicationFilePath(), {"--send", name});
        QVERIFY(child.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 10000);
        QCOMPARE(child.exitCode(), 0);
        QCOMPARE(received.count(), 2);
        QVERIFY(received.at(1).at(0).toStringList().isEmpty());
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.value(1) == "--send") return DocumentRequests::send(args.value(2), args.mid(3)) ? 0 : 1;
    RequestTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "request_tests.moc"
