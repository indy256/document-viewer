#pragma once
#include "pdfview.h"
#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QMimeData>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QTimer>
#include <QTabWidget>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScrollBar>
#include <memory>
#include <algorithm>
#include <cmath>

class Window : public QMainWindow {
public:
    explicit Window(const QString &sessionFile = {}, int autosaveIntervalMs = 5000)
        : sessionPath(sessionFile) {
        setWindowTitle("Document Viewer");
        resize(1100, 820);
        setMinimumSize(720, 480);
        setAcceptDrops(true);
        tabs = new QTabWidget(this);
        tabs->setTabsClosable(true);
        tabs->setMovable(true);
        tabs->setDocumentMode(true);
        setCentralWidget(tabs);
        auto toolbar = addToolBar("Reading controls");
        toolbar->setMovable(false);
        auto openAction = toolbar->addAction("Open");
        openAction->setShortcut(QKeySequence::Open);
        connect(openAction, &QAction::triggered, this, [this] {
            const auto paths = QFileDialog::getOpenFileNames(this, "Open documents", lastDirectory,
                "Documents (*.pdf *.epub);;PDF documents (*.pdf);;EPUB books (*.epub)");
            for (const auto &path : paths) openPdf(path);
        });
        toolbar->addSeparator();
        auto previous = toolbar->addAction("‹");
        previous->setToolTip("Previous page (Alt+Left)");
        previous->setShortcut(QKeySequence("Alt+Left"));
        page = new QSpinBox;
        page->setRange(1, 1);
        page->setPrefix("Page ");
        page->setKeyboardTracking(false);
        page->setAccessibleName("Page number");
        toolbar->addWidget(page);
        total = new QLabel(" / 0  ");
        toolbar->addWidget(total);
        auto next = toolbar->addAction("›");
        next->setToolTip("Next page (Alt+Right)");
        next->setShortcut(QKeySequence("Alt+Right"));
        auto spacer = new QWidget;
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        toolbar->addWidget(spacer);
        auto minus = toolbar->addAction("−");
        minus->setToolTip("Zoom out (Ctrl+-)");
        minus->setShortcut(QKeySequence("Ctrl+-"));
        zoom = new QComboBox;
        zoom->setAccessibleName("Zoom level");
        zoom->addItems({"Fit width", "Fit page", "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "500%"});
        zoom->setEditable(true);
        zoom->setInsertPolicy(QComboBox::NoInsert);
        zoom->setMinimumWidth(115);
        toolbar->addWidget(zoom);
        auto plus = toolbar->addAction("+");
        plus->setToolTip("Zoom in (Ctrl++)");
        plus->setShortcuts({QKeySequence("Ctrl++"), QKeySequence("Ctrl+=" )});
        auto fitWidth = toolbar->addAction("Fit width");
        fitWidth->setShortcut(QKeySequence("Ctrl+0"));
        connect(previous, &QAction::triggered, this, [this] { view->goToPage(view->currentPage() - 1); });
        connect(next, &QAction::triggered, this, [this] { view->goToPage(view->currentPage() + 1); });
        connect(page, &QSpinBox::valueChanged, this, [this](int n) { view->goToPage(n - 1); });
        connect(minus, &QAction::triggered, this, [this] { view->setZoom(view->zoom() / 1.2); });
        connect(plus, &QAction::triggered, this, [this] { view->setZoom(view->zoom() * 1.2); });
        connect(fitWidth, &QAction::triggered, this, [this] { view->setFit(PdfView::Fit::Width); });
        auto applyZoom = [this] {
            const QString text = zoom->currentText().trimmed();
            if (text == "Fit width") view->setFit(PdfView::Fit::Width);
            else if (text == "Fit page") view->setFit(PdfView::Fit::Page);
            else {
                QString number = text;
                number.remove('%');
                bool ok = false;
                const double value = number.toDouble(&ok);
                if (ok && std::isfinite(value)) view->setZoom(value / 100.0);
                else zoom->setCurrentText(QString::number(qRound(view->zoom() * 100)) + "%");
            }
        };
        connect(zoom, &QComboBox::textActivated, this, [applyZoom](const QString &) { applyZoom(); });
        connect(zoom->lineEdit(), &QLineEdit::returnPressed, this, applyZoom);
        controls = {previous, next, minus, plus, fitWidth};
        auto findAction = toolbar->addAction("Find");
        findAction->setShortcut(QKeySequence::Find);
        controls.append(findAction);
        addToolBarBreak();
        searchBar = addToolBar("Find in document");
        searchBar->setMovable(false);
        searchBar->addWidget(new QLabel(" Find  "));
        searchField = new QLineEdit;
        searchField->setObjectName("documentSearch");
        searchField->setAccessibleName("Find text in document");
        searchField->setPlaceholderText("Search in this document…");
        searchField->setClearButtonEnabled(true);
        searchField->setMinimumWidth(220);
        searchBar->addWidget(searchField);
        searchPrevious = searchBar->addAction("Previous");
        searchPrevious->setShortcut(QKeySequence("Shift+F3"));
        searchNext = searchBar->addAction("Next");
        searchNext->setShortcut(QKeySequence("F3"));
        searchCount = new QLabel;
        searchBar->addWidget(searchCount);
        auto hideSearch = searchBar->addAction("Close search");
        connect(hideSearch, &QAction::triggered, this, [this] { searchBar->hide(); view->setFocus(); });
        connect(findAction, &QAction::triggered, this, [this] {
            if (searchBar->isVisible()) {
                searchBar->hide();
                if (view) view->setFocus();
                return;
            }
            searchBar->show(); searchField->setFocus(); searchField->selectAll();
        });
        connect(searchField, &QLineEdit::textChanged, this, [this](const QString &text) {
            if (view) view->search(text);
        });
        connect(searchField, &QLineEdit::returnPressed, this, [this] { if (view) view->nextMatch(); });
        connect(searchNext, &QAction::triggered, this, [this] { if (view) view->nextMatch(); });
        connect(searchPrevious, &QAction::triggered, this, [this] { if (view) view->nextMatch(-1); });
        searchBar->hide();
        connect(tabs, &QTabWidget::currentChanged, this, [this] { syncControls(); });
        connect(tabs, &QTabWidget::tabCloseRequested, this, &Window::closeTab);
        auto closeAction = new QAction("Close tab", this);
        closeAction->setShortcut(QKeySequence("Ctrl+W"));
        addAction(closeAction);
        connect(closeAction, &QAction::triggered, this, [this] { closeTab(tabs->currentIndex()); });
        auto exitAction = new QAction("Exit", this);
        exitAction->setShortcut(QKeySequence(Qt::Key_Escape));
        addAction(exitAction);
        connect(exitAction, &QAction::triggered, this, [this] { close(); });
        // QTabWidget provides Ctrl+Tab and Ctrl+Shift+Tab navigation.
        addEmptyTab();
        enableControls(false);
        statusBar()->showMessage("Ready to read");
        statusBar()->addPermanentWidget(new QLabel("Continuous scrolling   ·   Ctrl+wheel to zoom  "));
        setStyleSheet(R"(
            QMainWindow { background: #f8fafc; }
            QTabWidget::pane { border: none; }
            QTabBar::tab { background: #e8ecf2; color: #66758c; padding: 10px 18px; border-right: 1px solid #dce2eb; }
            QTabBar::tab:selected { background: white; color: #26344b; }
            QToolBar { background: #ffffff; border: none; border-bottom: 1px solid #dce2eb; spacing: 7px; padding: 12px 10px; }
            QToolButton { color: #26344b; border: 1px solid transparent; border-radius: 5px; padding: 7px 10px; font-size: 14px; }
            QToolButton:hover { background: #edf2fc; border-color: #d3dff5; }
            QToolButton:disabled { color: #a7afbd; }
            QSpinBox, QComboBox { padding: 6px; border: 1px solid #dce2eb; border-radius: 5px; background: white; color: #26344b; }
            QStatusBar { background: white; color: #66758c; border-top: 1px solid #dce2eb; padding: 5px; }
        )");
        if (!sessionPath.isEmpty()) {
            auto autosave = new QTimer(this);
            autosave->setInterval(autosaveIntervalMs);
            connect(autosave, &QTimer::timeout, this, [this] { saveSession(); });
            QTimer::singleShot(0, this, [this, autosave] {
                restoreSession();
                autosave->start();
            });
        }
    }

    bool openPdf(const QString &path, bool restoring = false) {
        const QFileInfo requested(path);
        for (int i = 0; i < tabs->count(); ++i) {
            auto existing = qobject_cast<PdfView *>(tabs->widget(i));
            if (!existing || !existing->pageCount()) continue;
            const QFileInfo openedFile(existing->property("documentPath").toString());
            // QFileInfo resolves relative paths and symlinks and uses the
            // filesystem's case sensitivity. Avoid comparing missing entries.
            if (requested.absoluteFilePath() == openedFile.absoluteFilePath()
                || (requested.exists() && openedFile.exists() && requested == openedFile)) {
                tabs->setCurrentIndex(i);
                existing->setFocus();
                return true;
            }
        }
        auto candidate = std::make_unique<PdfView>();
        candidate->resize(tabs->contentsRect().size());
        QString error;
        QString password;
        while (!candidate->open(path, password, &error)) {
            if (error == "This PDF needs a valid password.") {
                bool ok = false;
                password = QInputDialog::getText(this, "Protected PDF", error, QLineEdit::Password, {}, &ok);
                if (ok) continue;
                return false;
            }
            if (!restoring) QMessageBox::warning(this, "Unable to open document", error);
            return false;
        }
        lastDirectory = QFileInfo(path).absolutePath();
        auto opened = candidate.release();
        opened->setProperty("documentPath", QFileInfo(path).absoluteFilePath());
        connectView(opened);
        const bool replaceEmpty = tabs->count() == 1 && view && !view->pageCount();
        auto empty = replaceEmpty ? view : nullptr;
        const int index = tabs->addTab(opened, QFileInfo(path).fileName());
        tabs->setTabToolTip(index, QFileInfo(path).absoluteFilePath());
        tabs->setCurrentIndex(index);
        if (empty) {
            tabs->removeTab(tabs->indexOf(empty));
            delete empty;
        }
        // Apply the initial fit after the tab has its final viewport dimensions.
        opened->setFit(PdfView::Fit::Width);
        opened->goToPage(0);
        syncControls();
        view->setFocus();
        return true;
    }
    void closeTab(int index) {
        if (index < 0 || index >= tabs->count()) return;
        auto closed = tabs->widget(index);
        tabs->removeTab(index);
        delete closed;
        if (tabs->count() == 0) addEmptyTab();
    }
    bool saveSession() {
        if (sessionPath.isEmpty() || restoringSession) return false;
        QJsonArray documents;
        int active = 0;
        for (int i = 0; i < tabs->count(); ++i) {
            auto pdf = qobject_cast<PdfView *>(tabs->widget(i));
            if (!pdf || !pdf->pageCount()) continue;
            if (pdf == view) active = int(documents.size());
            documents.append(QJsonObject{
                {"path", pdf->property("documentPath").toString()},
                {"zoom", pdf->zoom()},
                {"fit", int(pdf->fitMode())},
                {"page", pdf->currentPage()},
                {"horizontal", pdf->horizontalScrollBar()->value()},
                {"vertical", pdf->verticalScrollBar()->value()}
            });
        }
        const QJsonObject state{
            {"version", 1}, {"documents", documents}, {"activeTab", active},
            {"geometry", QString::fromLatin1(saveGeometry().toBase64())},
            {"lastDirectory", lastDirectory}
        };
        const auto data = QJsonDocument(state).toJson();
        QSaveFile file(sessionPath);
        if (!QDir().mkpath(QFileInfo(sessionPath).absolutePath())
            || !file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            statusBar()->showMessage("Could not save the reading session: " + file.errorString(), 10000);
            return false;
        }
        return true;
    }
protected:
    void closeEvent(QCloseEvent *event) override {
        saveSession();
        QMainWindow::closeEvent(event);
    }
    void dragEnterEvent(QDragEnterEvent *event) override {
        const auto urls = event->mimeData()->urls();
        if (!urls.isEmpty() && std::all_of(urls.begin(), urls.end(), [](const QUrl &url) { return url.isLocalFile(); }))
            event->acceptProposedAction();
    }
    void dropEvent(QDropEvent *event) override {
        for (const auto &url : event->mimeData()->urls())
            if (url.isLocalFile()) openPdf(url.toLocalFile());
        event->acceptProposedAction();
    }
private:
    void restoreSession() {
        QFile file(sessionPath);
        if (!file.exists()) return;
        if (!file.open(QIODevice::ReadOnly)) {
            statusBar()->showMessage("Could not read the saved session.", 10000);
            return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        const auto state = document.object();
        if (error.error != QJsonParseError::NoError || state.value("version").toInt() != 1
            || !state.value("documents").isArray()) {
            statusBar()->showMessage("The saved session is invalid; starting with an empty view.", 10000);
            return;
        }
        restoringSession = true; // Password dialogs run a nested event loop: never save a partial restore.
        restoreGeometry(QByteArray::fromBase64(state.value("geometry").toString().toLatin1()));
        lastDirectory = state.value("lastDirectory").toString();
        const auto documents = state.value("documents").toArray();
        const int active = state.value("activeTab").toInt();
        PdfView *activeView = nullptr;
        int skipped = 0;
        for (int i = 0; i < documents.size(); ++i) {
            const auto entry = documents[i].toObject();
            const auto path = entry.value("path").toString();
            if (path.isEmpty() || !openPdf(path, true)) { ++skipped; continue; }
            const double savedZoom = entry.value("zoom").toDouble(1.0);
            view->setZoom(std::clamp(savedZoom, 0.1, 5.0));
            view->goToPage(entry.value("page").toInt());
            const int fit = entry.value("fit").toInt();
            if (fit == int(PdfView::Fit::Width) || fit == int(PdfView::Fit::Page))
                view->setFit(static_cast<PdfView::Fit>(fit));
            view->horizontalScrollBar()->setValue(entry.value("horizontal").toInt());
            view->verticalScrollBar()->setValue(entry.value("vertical").toInt());
            if (i == active) activeView = view;
        }
        if (activeView) tabs->setCurrentWidget(activeView);
        syncControls();
        restoringSession = false;
        if (skipped)
            statusBar()->showMessage(QString("Restored session; %1 document(s) could not be reopened.").arg(skipped), 15000);
    }
    void addEmptyTab() {
        auto empty = new PdfView;
        connectView(empty);
        tabs->setCurrentIndex(tabs->addTab(empty, "New document"));
    }
    void connectView(PdfView *pdf) {
        connect(pdf, &PdfView::searchChanged, this, [this, pdf] { if (pdf == view) syncSearch(); });
        connect(pdf, &PdfView::pageChanged, this, [this, pdf](int n) {
            if (pdf != view) return;
            const QSignalBlocker blocker(page);
            page->setValue(n + 1);
        });
        connect(pdf, &PdfView::zoomChanged, this, [this, pdf](double value) {
            if (pdf != view) return;
            const QSignalBlocker blocker(zoom);
            zoom->setCurrentText(QString::number(qRound(value * 100)) + "%");
        });
    }
    void syncControls() {
        view = qobject_cast<PdfView *>(tabs->currentWidget());
        const bool loaded = view && view->pageCount() > 0;
        enableControls(loaded);
        const QSignalBlocker pageBlocker(page), zoomBlocker(zoom);
        page->setRange(1, loaded ? view->pageCount() : 1);
        page->setValue(loaded ? view->currentPage() + 1 : 1);
        total->setText(QString(" / %1  ").arg(loaded ? view->pageCount() : 0));
        zoom->setCurrentText(loaded ? QString::number(qRound(view->zoom() * 100)) + "%" : "Fit width");
        const auto path = loaded ? view->property("documentPath").toString() : QString();
        setWindowTitle(loaded ? QFileInfo(path).fileName() + " — Document Viewer" : "Document Viewer");
        statusBar()->showMessage(loaded ? QFileInfo(path).fileName() : "Ready to read");
        syncSearch();
    }
    void syncSearch() {
        const bool loaded = view && view->pageCount() > 0;
        const QSignalBlocker blocker(searchField);
        searchField->setEnabled(loaded);
        searchField->setText(loaded ? view->searchText() : QString());
        const bool ready = loaded && view->matchCount() > 0;
        searchNext->setEnabled(ready);
        searchPrevious->setEnabled(ready);
        if (!loaded || view->searchText().isEmpty()) searchCount->setText("  Case-insensitive substring search");
        else if (view->isSearching()) searchCount->setText(view->matchCount()
            ? QString("  %1 of %2 found · Searching…").arg(view->currentMatch() + 1).arg(view->matchCount())
            : QString("  Searching…"));
        else if (!view->matchCount()) searchCount->setText("  No matches");
        else searchCount->setText(QString("  %1 of %2").arg(view->currentMatch() + 1).arg(view->matchCount()));
    }
    void enableControls(bool enabled) {
        for (auto action : controls) action->setEnabled(enabled);
        page->setEnabled(enabled);
        zoom->setEnabled(enabled);
    }
    PdfView *view = nullptr;
    QTabWidget *tabs;
    QToolBar *searchBar;
    QLineEdit *searchField;
    QLabel *searchCount;
    QAction *searchPrevious;
    QAction *searchNext;
    QSpinBox *page;
    QComboBox *zoom;
    QLabel *total;
    QList<QAction *> controls;
    QString lastDirectory;
    QString sessionPath;
    bool restoringSession = false;
};
