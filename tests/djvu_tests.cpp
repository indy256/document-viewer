#include "djvu.h"
#include "pdfview.h"
#include "window.h"
#include <ByteStream.h>
#include <DjVmDoc.h>
#include <DjVuInfo.h>
#include <DjVuText.h>
#include <GPixmap.h>
#include <IFFByteStream.h>
#include <IW44Image.h>
#include <QFile>
#include <QPdfWriter>
#include <QPainter>
#include <QScrollBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QClipboard>
#include <QApplication>

using namespace DJVU;

static QByteArray fixture(int count = 2, int rotation = 0, const QString &indirectDirectory = {}) {
    auto bundle = DjVmDoc::create();
    auto output = ByteStream::create();
    for (int p = 0; p < count; ++p) {
        auto stream = ByteStream::create();
        auto iff = IFFByteStream::create(stream);
        iff->put_chunk("FORM:DJVU", 1);
        iff->put_chunk("INFO");
        auto info = DjVuInfo::create();
        info->width = 300; info->height = 400; info->dpi = 100; info->orientation = rotation;
        info->encode(*iff->get_bytestream());
        iff->close_chunk();
        auto pixmap = GPixmap::create(400, 300, &GPixel::WHITE);
        for (int y = 200; y < 400; ++y)
            for (int x = 0; x < 150; ++x) (*pixmap)[y][x] = GPixel::BLACK;
        auto encoder = IW44Image::create_encode(*pixmap);
        IWEncoderParms parameters;
        parameters.slices = 100;
        iff->put_chunk("BG44");
        encoder->encode_chunk(iff->get_bytestream(), parameters);
        iff->close_chunk();
        auto text = DjVuTXT::create();
        text->textUTF8 = "Banana reader";
        text->page_zone.ztype = DjVuTXT::PAGE;
        text->page_zone.rect = GRect(0, 0, 300, 400);
        text->page_zone.text_start = 0; text->page_zone.text_length = 13;
        auto word = text->page_zone.append_child();
        word->ztype = DjVuTXT::WORD; word->rect = GRect(20, 300, 80, 30);
        word->text_start = 0; word->text_length = 6;
        word = text->page_zone.append_child();
        word->ztype = DjVuTXT::WORD; word->rect = GRect(110, 300, 80, 30);
        word->text_start = 7; word->text_length = 6;
        iff->put_chunk("TXTa");
        text->encode(iff->get_bytestream());
        iff->close_chunk();
        iff->close_chunk();
        stream->seek(0);
        if (!indirectDirectory.isEmpty()) {
            QByteArray data(stream->size(), Qt::Uninitialized);
            stream->readall(data.data(), data.size());
            QFile component(indirectDirectory + QString("/page%1.djvu").arg(p));
            if (!component.open(QIODevice::WriteOnly) || component.write(data) != data.size()) return {};
            stream->seek(0);
        }
        if (count == 1) { output = stream; break; }
        const auto name = QString("page%1.djvu").arg(p).toUtf8();
        bundle->insert_file(*stream, DjVmDir::File::PAGE, name.constData(), name.constData());
    }
    if (!indirectDirectory.isEmpty()) bundle->write_index(output);
    else if (count != 1) bundle->write(output);
    QByteArray data(output->size(), Qt::Uninitialized);
    output->seek(0);
    output->readall(data.data(), data.size());
    return data;
}

static bool save(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

class DjvuTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { FPDF_InitLibrary(); }
    void cleanupTestCase() { FPDF_DestroyLibrary(); }
    void renderingAndText_data() {
        QTest::addColumn<int>("rotation");
        for (int i = 0; i < 4; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void renderingAndText() {
        QFETCH(int, rotation);
        QTemporaryDir directory;
        const auto path = directory.filePath(QString::fromUtf8("book-\xc3\xa9.DJV"));
        QVERIFY(save(path, fixture(1, rotation)));
        DjvuDocument document;
        QString error;
        QVERIFY2(document.open(path, &error), qPrintable(error));
        QCOMPARE(document.sizes().size(), 1);
        QCOMPARE(document.sizes().first(), rotation % 2 ? QSizeF(288, 216) : QSizeF(216, 288));
        const QSize extent = rotation % 2 ? QSize(400, 300) : QSize(300, 400);
        const auto full = document.render(0, extent, QRect(QPoint(), extent));
        QVERIFY(!full.isNull());
        const QRect tile(20, 30, 80, 90);
        const auto cropped = document.render(0, extent, tile);
        QVERIFY(!cropped.isNull());
        int difference = 0;
        for (int y = 0; y < tile.height(); ++y)
            for (int x = 0; x < tile.width(); ++x)
                difference = std::max(difference, std::abs(cropped.pixelColor(x, y).red()
                    - full.pixelColor(x + tile.x(), y + tile.y()).red()));
        QVERIFY2(difference <= 3, qPrintable(QString("Tile difference: %1").arg(difference)));
        const auto hits = document.search(0, "ANA");
        QCOMPARE(hits.size(), 2); // Overlapping, case-insensitive substrings.
        QCOMPARE(document.search(0, "banana reader").first().size(), 2);
        const auto center = hits.first().first().center();
        QVERIFY(full.pixelColor(qRound(center.x() * extent.width()), qRound(center.y() * extent.height())).lightness() < 80);
        QVERIFY(document.search(0, "absent").isEmpty());
        const auto text = document.textPage(0);
        QCOMPARE(text.text, QString("Banana reader"));
        QCOMPARE(text.spans.size(), 2);
        PdfView view;
        view.resize(600, 600);
        view.show();
        QVERIFY2(view.open(path, {}, &error), qPrintable(error));
        view.setFit(PdfView::Fit::Page);
        const auto size = document.sizes().first() * view.zoom();
        const auto origin = QPointF((view.viewport()->width() - size.width()) / 2, 24);
        auto position = [&](int word) {
            const auto center = text.spans[word].box.center();
            return (origin + QPointF(center.x() * size.width(), center.y() * size.height())).toPoint();
        };
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, position(0));
        QTest::mouseMove(view.viewport(), position(1));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, position(1));
        QCOMPARE(view.selectedText(), QString("Banana reader"));
        view.copySelection();
        QCOMPARE(QApplication::clipboard()->text(), QString("Banana reader"));
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, position(1));
        QCOMPARE(view.selectedText(), QString("reader"));
    }
    void viewerRecoveryAndSession() {
        QTemporaryDir directory;
        const auto path = directory.filePath("book.djvu");
        const auto bad = directory.filePath("broken.djvu");
        const auto pdf = directory.filePath("other.pdf");
        const auto session = directory.filePath("session.json");
        QVERIFY(save(path, fixture()));
        QVERIFY(save(bad, QByteArray::fromHex("41542654464f524d00000004444a5655")));
        { QPdfWriter writer(pdf); QPainter painter(&writer); painter.drawText(20, 30, "PDF"); }
        {
            Window window(session);
            window.show();
            QVERIFY(window.openDocument(pdf));
            QVERIFY(window.openDocument(path));
            auto tabs = window.findChild<QTabWidget *>();
            QCOMPARE(tabs->count(), 2);
            auto view = qobject_cast<PdfView *>(tabs->currentWidget());
            QCOMPARE(view->pageCount(), 2);
            view->setZoom(2.0);
            view->goToPage(1);
            QCOMPARE(view->currentPage(), 1);
            view->search("ana");
            QTRY_VERIFY(!view->isSearching());
            QCOMPARE(view->matchCount(), 4);
            QString error;
            QVERIFY(!view->open(bad, {}, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(view->pageCount(), 2);
            QCOMPARE(view->matchCount(), 4);
            view->search({});
            view->goToPage(1);
            window.close();
        }
        {
            Window restored(session);
            restored.show();
            auto tabs = restored.findChild<QTabWidget *>();
            QTRY_COMPARE(tabs->count(), 2);
            auto view = qobject_cast<PdfView *>(tabs->currentWidget());
            QCOMPARE(view->pageCount(), 2);
            QCOMPARE(view->zoom(), 2.0);
            QCOMPARE(view->currentPage(), 1);
            QString error;
            QVERIFY(view->open(pdf, {}, &error));
            QCOMPARE(view->pageCount(), 1);
            QVERIFY(view->open(path, {}, &error));
            QCOMPARE(view->pageCount(), 2);
        }
    }
    void indirectBook() {
        QTemporaryDir directory;
        const auto path = directory.filePath("index.djvu");
        const auto data = fixture(2, 0, directory.path());
        QVERIFY(!data.isEmpty());
        QVERIFY(save(path, data));
        DjvuDocument document;
        QString error;
        QVERIFY2(document.open(path, &error), qPrintable(error));
        QCOMPARE(document.sizes().size(), 2);
        QVERIFY(!document.render(1, QSize(300, 400), QRect(0, 0, 100, 100)).isNull());
        QCOMPARE(document.search(1, "reader").size(), 1);
        QVERIFY(QFile::remove(directory.filePath("page1.djvu")));
        DjvuDocument missing;
        QVERIFY(!missing.open(path, &error));
        QVERIFY(!error.isEmpty());
    }
};
QTEST_MAIN(DjvuTests)
#include "djvu_tests.moc"
