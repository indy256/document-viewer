#include "updater.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

class UpdaterTests : public QObject {
    Q_OBJECT
    QJsonObject release() const {
        return {{"tag_name", "v1.2.3"}, {"draft", false}, {"prerelease", false},
                {"assets", QJsonArray{QJsonObject{
                    {"name", "dv-windows-x64.exe"}, {"size", 100},
                    {"digest", "sha256:" + QString(64, 'a')},
                    {"browser_download_url", "https://github.com/indy256/document-viewer/releases/download/v1.2.3/dv-windows-x64.exe"}}}}};
    }
private slots:
    void matchingRelease() {
        Updater::Asset asset;
        QString error;
        QVERIFY(Updater::readRelease(QJsonDocument(release()).toJson(), "DV-WINDOWS-X64.EXE", asset, error));
        QCOMPARE(asset.version, "v1.2.3");
        QCOMPARE(asset.size, 100);
        QCOMPARE(asset.sha256, QByteArray(64, 'a'));
        QVERIFY(!Updater::readRelease(QJsonDocument(release()).toJson(), "dv-windows-arm64.exe", asset, error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(asset.size, 0);
    }
    void invalidRelease() {
        Updater::Asset asset;
        QString error;
        for (const auto bytes : {QByteArray("invalid"), QByteArray("[]"), QByteArray("{}")})
            QVERIFY(!Updater::readRelease(bytes, "dv-windows-x64.exe", asset, error));
        for (const auto field : {"draft", "prerelease"}) {
            auto value = release();
            value[field] = true;
            QVERIFY(!Updater::readRelease(QJsonDocument(value).toJson(), "dv-windows-x64.exe", asset, error));
        }
    }
    void invalidAsset_data() {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("missing-checksum") << QString("digest") << QJsonValue();
        QTest::newRow("wrong-checksum") << QString("digest") << QJsonValue("sha256:abc");
        QTest::newRow("zero-size") << QString("size") << QJsonValue(0);
        QTest::newRow("oversized") << QString("size") << QJsonValue(2 * 1024 * 1024 * 1024.0);
        QTest::newRow("http") << QString("browser_download_url") << QJsonValue("http://github.com/indy256/document-viewer/releases/download/v1/file");
        QTest::newRow("host") << QString("browser_download_url") << QJsonValue("https://example.com/indy256/document-viewer/releases/download/v1/file");
        QTest::newRow("repository") << QString("browser_download_url") << QJsonValue("https://github.com/indy256/image-viewer/releases/download/v1/file");
        QTest::newRow("credentials") << QString("browser_download_url") << QJsonValue("https://user@github.com/indy256/document-viewer/releases/download/v1/file");
    }
    void invalidAsset() {
        QFETCH(QString, field);
        QFETCH(QJsonValue, value);
        auto json = release();
        auto item = json["assets"].toArray().first().toObject();
        item[field] = value;
        json["assets"] = QJsonArray{item};
        Updater::Asset asset;
        QString error;
        QVERIFY(!Updater::readRelease(QJsonDocument(json).toJson(), "dv-windows-x64.exe", asset, error));
        QVERIFY(!error.isEmpty());
    }
    void hashFile() {
        QTemporaryDir directory;
        const auto path = directory.filePath("download");
        QVERIFY(Updater::fileHash(path).isEmpty());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("abc"), 3);
        file.close();
        QCOMPARE(Updater::fileHash(path), QByteArray("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    }
};
QTEST_GUILESS_MAIN(UpdaterTests)
#include "updater_tests.moc"
