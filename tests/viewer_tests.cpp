#include "epubfixture.h"
#include "pdfview.h"
#include "window.h"
#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <QFontDatabase>
#include <QtCore/private/qzipwriter_p.h>
#include <QBuffer>

class ViewerTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        FPDF_InitLibrary();
        QString fontPath = qEnvironmentVariable("DOCUMENT_VIEWER_TEST_FONT");
        if (fontPath.isEmpty()) {
#ifdef Q_OS_WIN
            fontPath = qEnvironmentVariable("WINDIR") + "/Fonts/arial.ttf";
#elif defined(Q_OS_MACOS)
            fontPath = "/System/Library/Fonts/Supplemental/Arial.ttf";
#else
            fontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
        }
        const int fontId = QFontDatabase::addApplicationFont(fontPath);
        QVERIFY2(fontId >= 0, qPrintable("Could not load test font: " + fontPath));
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(), 10));
    }
    void cleanupTestCase() { FPDF_DestroyLibrary(); }
    void epubReading() {
        QTemporaryDir directory;
        const auto epubPath = directory.filePath(QString::fromUtf8("book-é.epub"));
        makeBook(epubPath, false, "3.0");
        const auto sessionPath = directory.filePath("session.json");
        {
            Window window(sessionPath);
            window.show();
            QTest::qWait(20);
            QVERIFY(window.openPdf(epubPath));
            auto view = qobject_cast<PdfView *>(window.findChild<QTabWidget *>()->currentWidget());
            QCOMPARE(view->pageCount(), 2);
            const auto image = view->viewport()->grab().toImage();
            bool hasRed = false, hasBlue = false;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const auto color = image.pixelColor(x, y);
                    hasRed |= color.red() > 220 && color.green() < 40 && color.blue() < 40;
                    hasBlue |= color.blue() > 180 && color.red() < 80 && color.green() < 80;
                }
            }
            QVERIFY(hasRed); // Archive image paths and percent decoding.
            QVERIFY(hasBlue); // Linked stylesheet applied.
            view->search("needle");
            QTRY_VERIFY(!view->isSearching());
            QCOMPARE(view->matchCount(), 2);
            QCOMPARE(view->currentPage(), 0);
            view->nextMatch();
            QCOMPARE(view->currentPage(), 1); // Spine order, not manifest/archive order.
            view->setZoom(1.8);
            view->verticalScrollBar()->setValue(1100);
            window.close();
        }
        {
            Window window(sessionPath);
            window.show();
            QTest::qWait(100);
            auto view = qobject_cast<PdfView *>(window.findChild<QTabWidget *>()->currentWidget());
            QCOMPARE(view->pageCount(), 2);
            QCOMPARE(view->zoom(), 1.8);
            QCOMPARE(view->verticalScrollBar()->value(), 1100);
            const auto broken = directory.filePath("broken.epub");
            makeBook(broken, true, "3.0");
            QString error;
            QVERIFY(!view->open(broken, {}, &error));
            QVERIFY(error.contains("missing"));
            QCOMPARE(view->pageCount(), 2); // Failure keeps the current book open.
            const auto epub2 = directory.filePath("older.epub");
            makeBook(epub2, false, "2.0");
            QVERIFY(view->open(epub2, {}, &error));
            QCOMPARE(view->pageCount(), 2);
        }
    }
    void substringSearch() {
        QTemporaryDir directory;
        const auto path = directory.filePath("search.pdf");
        {
            QPdfWriter writer(path);
            writer.setResolution(72);
            QPainter painter(&writer);
            painter.setFont(QFont("Arial", 18));
            painter.drawText(50, 100, "Alpha alphabet ALPHA");
            writer.newPage();
            painter.drawText(50, 100, "Another alpha and banana");
        }
        Window window;
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(window.openPdf(path));
        auto tabs = window.findChild<QTabWidget *>();
        auto view = qobject_cast<PdfView *>(tabs->currentWidget());
        QTest::keyClick(view, Qt::Key_F, Qt::ControlModifier);
        auto field = window.findChild<QLineEdit *>("documentSearch");
        QVERIFY(field->isVisible());
        bool navigatedWhileSearching = false;
        int partialScrollPosition = -1;
        const auto partialSearch = connect(view, &PdfView::searchChanged, &window, [&] {
            if (navigatedWhileSearching || !view->isSearching() || view->matchCount() < 2) return;
            navigatedWhileSearching = true; // Navigation emits searchChanged again.
            QCOMPARE(view->currentMatch(), 0);
            for (auto action : window.findChildren<QAction *>())
                if (action->text() == "Next" || action->text() == "Previous") QVERIFY(action->isEnabled());
            QTest::keyClick(field, Qt::Key_F3);
            QCOMPARE(view->currentMatch(), 1);
            partialScrollPosition = view->verticalScrollBar()->value();
        });
        QTest::keyClicks(field, "alpha");
        QTRY_VERIFY(!view->isSearching());
        disconnect(partialSearch);
        QVERIFY(navigatedWhileSearching);
        QCOMPARE(view->currentMatch(), 1); // Completion must preserve the user's selection and position.
        QCOMPARE(view->verticalScrollBar()->value(), partialScrollPosition);
        view->nextMatch(-1);
        QCOMPARE(view->matchCount(), 4); // Includes alphabet and different letter cases.
        QCOMPARE(view->currentMatch(), 0);
        QTest::keyClick(field, Qt::Key_Return);
        QCOMPARE(view->currentMatch(), 1);
        view->nextMatch();
        view->nextMatch();
        QCOMPARE(view->currentMatch(), 3);
        QCOMPARE(view->currentPage(), 1);
        view->nextMatch();
        QCOMPARE(view->currentMatch(), 0);
        view->nextMatch(-1);
        QCOMPARE(view->currentMatch(), 3);
        view->search("ana");
        QTRY_VERIFY(!view->isSearching());
        QCOMPARE(view->matchCount(), 2); // Overlapping substrings in banana.
        auto highlighted = view->viewport()->grab().toImage();
        view->search("");
        QVERIFY(!view->isSearching());
        QCOMPARE(view->matchCount(), 0);
        QVERIFY(highlighted != view->viewport()->grab().toImage());
        view->search("missing phrase");
        QTRY_VERIFY(!view->isSearching());
        QCOMPARE(view->matchCount(), 0);
        view->search("alpha");
        view->search("banana"); // Cancel an obsolete query before its first page is scanned.
        QTRY_VERIFY(!view->isSearching());
        QCOMPARE(view->matchCount(), 1);
        const auto otherPath = directory.filePath("other.pdf");
        QVERIFY(QFile::copy(path, otherPath));
        QVERIFY(window.openPdf(otherPath));
        auto other = qobject_cast<PdfView *>(tabs->currentWidget());
        QVERIFY(other->searchText().isEmpty());
        other->search("another");
        QTRY_VERIFY(!other->isSearching());
        tabs->setCurrentIndex(0);
        QCOMPARE(field->text(), QString("banana"));
        QCOMPARE(view->matchCount(), 1);
        QCOMPARE(other->searchText(), QString("another"));
    }
    void sessionRecovery() {
        QTemporaryDir directory;
        const auto sessionPath = directory.filePath("state/session.json");
        const auto firstPath = directory.filePath("first.pdf");
        const auto secondPath = directory.filePath("second.pdf");
        for (const auto &path : {firstPath, secondPath}) {
            QPdfWriter writer(path);
            writer.setResolution(72);
            QPainter painter(&writer);
            for (int i = 0; i < 3; ++i) {
                if (i) writer.newPage();
                painter.drawText(50, 50, QString("Page %1").arg(i + 1));
            }
        }
        {
            Window window(sessionPath, 50);
            window.show();
            QTest::qWait(20); // Let startup restoration complete before opening documents.
            window.openPdf(firstPath);
            auto tabs = window.findChild<QTabWidget *>();
            auto first = qobject_cast<PdfView *>(tabs->currentWidget());
            first->setZoom(2.0);
            first->horizontalScrollBar()->setValue(120);
            first->verticalScrollBar()->setValue(1300);
            window.openPdf(secondPath);
            auto second = qobject_cast<PdfView *>(tabs->currentWidget());
            second->setFit(PdfView::Fit::Page);
            second->goToPage(1);
            tabs->setCurrentIndex(0);
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(sessionPath), 2000);
            // Verify a subsequent periodic save contains recent changes, without closing.
            first->verticalScrollBar()->setValue(1450);
            auto savedPosition = [&] {
                QFile file(sessionPath);
                if (!file.open(QIODevice::ReadOnly)) return -1;
                return QJsonDocument::fromJson(file.readAll()).object().value("documents")
                    .toArray().at(0).toObject().value("vertical").toInt();
            };
            QTRY_COMPARE_WITH_TIMEOUT(savedPosition(), 1450, 2000);
        } // Destroy without closeEvent: recovery must use the periodic checkpoint.
        {
            Window window(sessionPath);
            window.show();
            auto tabs = window.findChild<QTabWidget *>();
            QTRY_COMPARE(tabs->count(), 2);
            QTest::qWait(30);
            QCOMPARE(tabs->currentIndex(), 0);
            QCOMPARE(tabs->tabText(0), QString("first.pdf"));
            auto first = qobject_cast<PdfView *>(tabs->widget(0));
            auto second = qobject_cast<PdfView *>(tabs->widget(1));
            QCOMPARE(first->zoom(), 2.0);
            QCOMPARE(first->horizontalScrollBar()->value(), 120);
            QCOMPARE(first->verticalScrollBar()->value(), 1450);
            QCOMPARE(second->fitMode(), PdfView::Fit::Page);
            tabs->setCurrentIndex(1);
            second->setZoom(1.7);
            second->verticalScrollBar()->setValue(800);
            window.close(); // Must save immediately, before the five-second timer.
        }
        {
            Window window(sessionPath);
            window.show();
            auto tabs = window.findChild<QTabWidget *>();
            QTRY_COMPARE(tabs->count(), 2);
            QCOMPARE(tabs->currentIndex(), 1);
            auto second = qobject_cast<PdfView *>(tabs->currentWidget());
            QCOMPARE(second->zoom(), 1.7);
            QCOMPARE(second->verticalScrollBar()->value(), 800);
        }
        QVERIFY(QFile::remove(firstPath));
        {
            Window window(sessionPath);
            window.show();
            QTest::qWait(30);
            auto tabs = window.findChild<QTabWidget *>();
            QCOMPARE(tabs->count(), 1);
            QCOMPARE(tabs->tabText(0), QString("second.pdf"));
            window.closeTab(0);
            window.close();
        }
        {
            Window window(sessionPath);
            window.show();
            QTest::qWait(30);
            auto tabs = window.findChild<QTabWidget *>();
            QCOMPARE(tabs->count(), 1);
            QCOMPARE(qobject_cast<PdfView *>(tabs->currentWidget())->pageCount(), 0);
        }
        QFile corrupt(sessionPath);
        QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
        corrupt.write("{broken");
        corrupt.close();
        Window window(sessionPath);
        window.show();
        QTest::qWait(30);
        QCOMPARE(window.findChild<QTabWidget *>()->count(), 1);
        QVERIFY(window.statusBar()->currentMessage().contains("invalid"));
    }
    void duplicateOpen_data() {
        QTest::addColumn<QString>("extension");
        QTest::newRow("PDF") << QString("pdf");
        QTest::newRow("EPUB") << QString("epub");
    }
    void duplicateOpen() {
        QFETCH(QString, extension);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("book." + extension);
        if (extension == "epub") {
            makeBook(path, false, "3.0");
        } else {
            QPdfWriter writer(path);
            writer.setResolution(72);
            QPainter painter(&writer);
            painter.drawText(50, 100, "needle on first page");
            writer.newPage();
            painter.drawText(50, 100, "needle on second page");
        }
        QVERIFY(QDir(directory.path()).mkdir("other"));
        const auto otherPath = directory.filePath("other/book." + extension);
        QVERIFY(QFile::copy(path, otherPath));
        Window window;
        window.show();
        QVERIFY(window.openPdf(path));
        auto tabs = window.findChild<QTabWidget *>();
        auto original = qobject_cast<PdfView *>(tabs->currentWidget());
        original->search("needle");
        QTRY_VERIFY(!original->isSearching());
        original->nextMatch();
        const int match = original->currentMatch();
        original->setZoom(1.8);
        original->goToPage(1);
        const int position = original->verticalScrollBar()->value();
        QVERIFY(window.openPdf(path)); // Reopening the active file also preserves state.
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(tabs->currentWidget(), original);
        QVERIFY(window.openPdf(otherPath)); // Same name and contents, different file.
        QCOMPARE(tabs->count(), 2);
        auto other = tabs->currentWidget();
        QVERIFY(other != original);
        const QStringList aliases{
            path,
            QDir::current().relativeFilePath(path),
            directory.filePath("other/../book." + extension),
#ifdef Q_OS_WIN
            QDir::toNativeSeparators(path.toUpper()),
#endif
        };
        for (const auto &alias : aliases) {
            tabs->setCurrentWidget(other);
            QVERIFY(window.openPdf(alias));
            QCOMPARE(tabs->count(), 2);
            QCOMPARE(tabs->currentWidget(), original);
            QCOMPARE(original->zoom(), 1.8);
            QCOMPARE(original->verticalScrollBar()->value(), position);
            QCOMPARE(original->searchText(), QString("needle"));
            QCOMPARE(original->currentMatch(), match);
            QCOMPARE(window.findChild<QComboBox *>()->currentText(), QString("180%"));
        }
#ifndef Q_OS_WIN
        const auto link = directory.filePath("linked." + extension);
        QVERIFY(QFile::link(path, link));
        tabs->setCurrentWidget(other);
        QVERIFY(window.openPdf(link));
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(tabs->currentWidget(), original);
#endif
        window.closeTab(tabs->indexOf(original));
        QVERIFY(window.openPdf(path)); // Closing removes the file from the open set.
        QCOMPARE(tabs->count(), 2);
        QVERIFY(tabs->currentWidget() != other);
    }
    void multipleTabs() {
        QTemporaryDir directory;
        const auto firstPath = directory.filePath("first.pdf");
        const auto secondPath = directory.filePath("second.pdf");
        for (const auto &path : {firstPath, secondPath}) {
            QPdfWriter writer(path);
            writer.setResolution(72);
            QPainter painter(&writer);
            painter.drawText(50, 50, path);
            writer.newPage();
            painter.drawText(50, 50, "Second page");
        }
        Window window;
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto tabs = window.findChild<QTabWidget *>();
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 1);
        window.openPdf(firstPath);
        QCOMPARE(tabs->count(), 1); // The welcome tab is replaced.
        auto first = qobject_cast<PdfView *>(tabs->currentWidget());
        QVERIFY(first);
        first->setZoom(1.5);
        first->goToPage(1);
        const int position = first->verticalScrollBar()->value();
        window.openPdf(secondPath);
        QCOMPARE(tabs->count(), 2);
        auto second = qobject_cast<PdfView *>(tabs->currentWidget());
        QVERIFY(second != first);
        second->setZoom(0.5);
        tabs->setCurrentIndex(0);
        QCOMPARE(first->zoom(), 1.5);
        QCOMPARE(first->verticalScrollBar()->value(), position);
        QCOMPARE(window.findChild<QComboBox *>()->currentText(), QString("150%"));
        QCOMPARE(window.findChild<QSpinBox *>()->value(), 2);
        QVERIFY(window.windowTitle().startsWith("first.pdf"));
        QTest::keyClick(tabs, Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(tabs->currentWidget(), second);
        QCOMPARE(window.findChild<QComboBox *>()->currentText(), QString("50%"));
        second->setFocus();
        QTest::keyClick(second, Qt::Key_W, Qt::ControlModifier);
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(tabs->currentWidget(), first);
        QCOMPARE(first->zoom(), 1.5);
        window.closeTab(0);
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(qobject_cast<PdfView *>(tabs->currentWidget())->pageCount(), 0);
        QVERIFY(!window.findChild<QComboBox *>()->isEnabled());
        window.openPdf(secondPath);
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(qobject_cast<PdfView *>(tabs->currentWidget())->pageCount(), 2);
    }
    void readingWorkflow() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QString::fromUtf8("sample-é.pdf"));
        {
            QPdfWriter writer(path);
            writer.setResolution(72);
            QPainter painter(&writer);
            painter.fillRect(QRect(0, 0, 400, 400), Qt::red);
            writer.newPage();
            painter.fillRect(QRect(0, 0, 400, 400), Qt::blue);
            writer.newPage();
            painter.fillRect(QRect(0, 0, 400, 400), Qt::green);
        }
        PdfView view;
        view.resize(800, 700);
        view.show();
        QString error;
        QVERIFY2(view.open(path, {}, &error), qPrintable(error));
        QTest::qWait(30);
        QCOMPARE(view.pageCount(), 3);
        QVERIFY(view.verticalScrollBar()->maximum() > view.viewport()->height());
        auto image = view.viewport()->grab().toImage();
        QVERIFY(image.pixelColor(image.width() / 2, 150).red() > 200);
        QVERIFY(image.pixelColor(image.width() / 2, 150).blue() < 50);
        view.goToPage(1);
        QCOMPARE(view.currentPage(), 1);
        image = view.viewport()->grab().toImage();
        QVERIFY(image.pixelColor(image.width() / 2, 150).blue() > 200);
        QVERIFY(image.pixelColor(image.width() / 2, 150).red() < 50);
        view.setZoom(2);
        QCOMPARE(view.zoom(), 2.0);
        QCOMPARE(view.currentPage(), 1);
        QVERIFY(view.horizontalScrollBar()->maximum() > 0);
        QWheelEvent wheel(QPointF(300, 300), QPointF(300, 300), {}, QPoint(0, 120),
                          Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view.viewport(), &wheel);
        QVERIFY(view.zoom() > 2);
        view.setZoom(100);
        QCOMPARE(view.zoom(), 5.0);
        view.setZoom(0.001);
        QCOMPARE(view.zoom(), 0.1);
        view.setFit(PdfView::Fit::Width);
        const double oldZoom = view.zoom();
        view.resize(1000, 700);
        QTest::qWait(20);
        QVERIFY(view.zoom() > oldZoom);
        view.setFit(PdfView::Fit::Page);
        QVERIFY(view.zoom() < oldZoom);
        QFile invalid(directory.filePath("invalid.pdf"));
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("not a PDF");
        invalid.close();
        QVERIFY(!view.open(invalid.fileName(), {}, &error));
        QCOMPARE(view.pageCount(), 3); // Failed opens preserve the current document.
        view.goToPage(2);
        QVERIFY(view.open(path, {}, &error));
        QCOMPARE(view.currentPage(), 0);
        QCOMPARE(view.verticalScrollBar()->value(), 0);
    }
};
QTEST_MAIN(ViewerTests)
#include "viewer_tests.moc"
