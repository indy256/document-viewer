#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <functional>

class QWidget;
namespace Updater {
struct Asset {
    QString version;
    QUrl url;
    QByteArray sha256;
    qint64 size = 0;
};
// Validates metadata before any release download or installation is attempted.
bool readRelease(const QByteArray &json, const QString &assetName, Asset &asset, QString &error);
QByteArray fileHash(const QString &path);
// True only after the helper is ready and the saved session can be restored.
bool installLatest(QWidget *parent, const std::function<bool()> &saveSession);
}
