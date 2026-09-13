#pragma once
#include <QLocalServer>
#include <QStringList>

// Local command-line requests; the caller must hold the instance lock to listen.
class DocumentRequests : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    bool listen(const QString &name);
    static bool send(const QString &name, const QStringList &paths);
signals:
    void received(const QStringList &paths);
private:
    QLocalServer server;
};
