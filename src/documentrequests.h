#pragma once
#include <QObject>
#ifndef Q_OS_WIN
#include <QLocalServer>
#endif
#include <QStringList>

// Local command-line requests; the caller must hold the instance lock to listen.
class DocumentRequests : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
#ifdef Q_OS_WIN
    ~DocumentRequests() override;
#endif
    bool listen(const QString &name);
    static bool send(const QString &name, const QStringList &paths);
signals:
    void received(const QStringList &paths);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
#ifdef Q_OS_WIN
    void *messageWindow = nullptr;
#else
    QLocalServer server;
#endif
};
