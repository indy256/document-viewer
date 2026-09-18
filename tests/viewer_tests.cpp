#include "epubfixture.h"
#include "pdfview.h"
#include "window.h"
#include "viewerstyle.h"
#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <QFontDatabase>
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QMouseEvent>
#include <QWindow>
#include <QStyle>
#include <QStyleOptionSlider>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class ViewerTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QApplication::setStyle(new ViewerStyle);
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
            QVERIFY(window.openDocument(epubPath));
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
        QVERIFY(window.openDocument(path));
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
        QVERIFY(window.openDocument(otherPath));
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
            window.openDocument(firstPath);
            auto tabs = window.findChild<QTabWidget *>();
            auto first = qobject_cast<PdfView *>(tabs->currentWidget());
            first->setZoom(2.0);
            first->horizontalScrollBar()->setValue(120);
            first->verticalScrollBar()->setValue(1300);
            window.openDocument(secondPath);
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
        QVERIFY(window.openDocument(path));
        auto tabs = window.findChild<QTabWidget *>();
        auto original = qobject_cast<PdfView *>(tabs->currentWidget());
        original->search("needle");
        QTRY_VERIFY(!original->isSearching());
        original->nextMatch();
        const int match = original->currentMatch();
        original->setZoom(1.8);
        original->goToPage(1);
        const int position = original->verticalScrollBar()->value();
        QVERIFY(window.openDocument(path)); // Reopening the active file also preserves state.
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(tabs->currentWidget(), original);
        QVERIFY(window.openDocument(otherPath)); // Same name and contents, different file.
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
            QVERIFY(window.openDocument(alias));
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
        QVERIFY(window.openDocument(link));
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(tabs->currentWidget(), original);
#endif
        window.closeTab(tabs->indexOf(original));
        QVERIFY(window.openDocument(path)); // Closing removes the file from the open set.
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
        window.openDocument(firstPath);
        QCOMPARE(tabs->count(), 1); // The welcome tab is replaced.
        auto first = qobject_cast<PdfView *>(tabs->currentWidget());
        QVERIFY(first);
        first->setZoom(1.5);
        first->goToPage(1);
        const int position = first->verticalScrollBar()->value();
        window.openDocument(secondPath);
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
        window.openDocument(secondPath);
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
    void scrollbarTrackDrag_data() {
        QTest::addColumn<bool>("horizontal");
        QTest::addColumn<int>("initialValue");
        QTest::newRow("vertical-below") << false << 0;
        QTest::newRow("vertical-above") << false << 1000;
        QTest::newRow("horizontal-after") << true << 0;
        QTest::newRow("horizontal-before") << true << 1000;
    }
    void scrollbarTrackDrag() {
        QFETCH(bool, horizontal);
        QFETCH(int, initialValue);
        QScrollBar bar(horizontal ? Qt::Horizontal : Qt::Vertical);
        bar.resize(horizontal ? QSize(400, 20) : QSize(20, 400));
        bar.setRange(0, 1000);
        bar.setPageStep(100);
        bar.setValue(initialValue);
        bar.show();
        const auto center = bar.rect().center();
        QTest::mousePress(&bar, Qt::LeftButton, Qt::NoModifier, center);
        QVERIFY(bar.isSliderDown());
        QVERIFY(bar.value() != initialValue);
        const int pressedValue = bar.value();
        const auto moved = center + (horizontal ? QPoint(60, 0) : QPoint(0, 60));
        QMouseEvent drag(QEvent::MouseMove, moved, bar.mapToGlobal(moved),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&bar, &drag);
        QVERIFY(bar.value() > pressedValue);
        QTest::mouseRelease(&bar, Qt::LeftButton, Qt::NoModifier, moved);
        QVERIFY(!bar.isSliderDown());
        const int releasedValue = bar.value();
        QTest::mouseMove(&bar, center);
        QCOMPARE(bar.value(), releasedValue);
    }
    void scrollbarAtRightEdge_data() {
        QTest::addColumn<double>("fraction");
        QTest::newRow("whole-pixel") << 0.0;
        QTest::newRow("125-percent") << 0.2;
        QTest::newRow("150-percent") << 1.0 / 3.0;
        QTest::newRow("175-percent") << 3.0 / 7.0;
        QTest::newRow("200-percent") << 0.5;
        QTest::newRow("400-percent") << 0.75;
    }
    void nativeScrollbarAtScreenEdge() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() != "windows")
            QSKIP("Requires the Windows platform plugin");
        QTemporaryDir directory;
        const auto path = directory.filePath("native-edge.pdf");
        {
            QPdfWriter writer(path);
            QPainter painter(&writer);
            painter.drawText(50, 100, "First page");
            writer.newPage();
            painter.drawText(50, 100, "Second page");
        }
        Window window;
        window.showMaximized();
        QVERIFY(window.openDocument(path));
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto scrollbar = window.findChild<PdfView *>()->verticalScrollBar();
        const auto hwnd = reinterpret_cast<HWND>(window.winId());
        MONITORINFO monitor{sizeof(MONITORINFO)};
        QVERIFY(GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor));
        RECT client;
        QVERIFY(GetClientRect(hwnd, &client));
        POINT edge{monitor.rcMonitor.right - 1, (monitor.rcWork.top + monitor.rcWork.bottom) / 2};
        POINT local = edge;
        QVERIFY(ScreenToClient(hwnd, &local));
        if (local.x != client.right - 1)
            QSKIP("The monitor's right edge is outside the maximized client area");
        scrollbar->setValue(0);
        SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, WORD(-WHEEL_DELTA)), MAKELPARAM(edge.x, edge.y));
        QCoreApplication::processEvents();
        const bool wheelScrolled = scrollbar->value() > 0;
        scrollbar->setValue(0);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(local.x, local.y));
        QCoreApplication::processEvents();
        const bool sliderDown = scrollbar->isSliderDown();
        const int afterClick = scrollbar->value();
        SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(local.x, local.y + 100));
        QCoreApplication::processEvents();
        const int afterDrag = scrollbar->value();
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(local.x, local.y + 100));
        QCoreApplication::processEvents();
        QVERIFY(wheelScrolled);
        QVERIFY(sliderDown);
        QVERIFY(afterClick > 0);
        QVERIFY(afterDrag > afterClick);
        QVERIFY(!scrollbar->isSliderDown());
#else
        QSKIP("Requires Windows");
#endif
    }
    void scrollbarAtRightEdge() {
        QFETCH(double, fraction);
        QTemporaryDir directory;
        const auto path = directory.filePath("edge.pdf");
        {
            QPdfWriter writer(path);
            QPainter painter(&writer);
            painter.drawText(50, 100, "First page");
            writer.newPage();
            painter.drawText(50, 100, "Second page");
        }
        Window window;
        window.showMaximized();
        QVERIFY(window.openDocument(path));
        QTest::qWait(50);
        auto view = window.findChild<PdfView *>();
        auto scrollbar = view->verticalScrollBar();
        QVERIFY(scrollbar->maximum() > 0);
        const QPointF wheelPosition(window.width() - 1 + fraction,
            scrollbar->mapTo(&window, scrollbar->rect().center()).y());
        auto wheel = [&](int delta) {
            QWheelEvent event(wheelPosition, window.mapToGlobal(wheelPosition), {},
                              QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window.windowHandle(), &event);
        };
        scrollbar->setValue(0);
        wheel(-120);
        QVERIFY(scrollbar->value() > 0);
        const int afterWheelDown = scrollbar->value();
        wheel(120);
        QVERIFY(scrollbar->value() < afterWheelDown);
        auto mouse = [&](QEvent::Type type, int y) {
            // Send fractional coordinates through the window, so Qt must pick
            // the target widget just as it does for native high-DPI input.
            const QPointF position(window.width() - 1 + fraction,
                                   scrollbar->mapTo(&window, QPoint(0, y)).y());
            const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
            const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
            QMouseEvent event(type, position, position, window.mapToGlobal(position),
                              button, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(window.windowHandle(), &event);
        };
        scrollbar->setValue(scrollbar->maximum());
        const int before = scrollbar->value();
        mouse(QEvent::MouseButtonPress, 5);
        mouse(QEvent::MouseButtonRelease, 5);
        QVERIFY(scrollbar->value() < before);
        scrollbar->setValue(before);
        mouse(QEvent::MouseButtonPress, scrollbar->height() / 2);
        QVERIFY(scrollbar->isSliderDown());
        QVERIFY(scrollbar->value() < before);
        const int afterTrackClick = scrollbar->value();
        mouse(QEvent::MouseMove, scrollbar->height() / 2 + 60);
        QVERIFY(scrollbar->value() > afterTrackClick);
        mouse(QEvent::MouseButtonRelease, scrollbar->height() / 2 + 60);
        QVERIFY(!scrollbar->isSliderDown());
        scrollbar->setValue(0);
        QStyleOptionSlider option;
        option.initFrom(scrollbar);
        option.orientation = Qt::Vertical;
        option.minimum = scrollbar->minimum();
        option.maximum = scrollbar->maximum();
        option.pageStep = scrollbar->pageStep();
        option.sliderPosition = option.sliderValue = 0;
        const auto thumb = scrollbar->style()->subControlRect(
            QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, scrollbar);
        mouse(QEvent::MouseButtonPress, thumb.center().y());
        mouse(QEvent::MouseMove, thumb.center().y() + 100);
        mouse(QEvent::MouseButtonRelease, thumb.center().y() + 100);
        QVERIFY(scrollbar->value() > 0);
    }
};
QTEST_MAIN(ViewerTests)
#include "viewer_tests.moc"
