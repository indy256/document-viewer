#pragma once
#include <QMainWindow>
#include <QList>
#include <QString>

class PdfView;
class QAction;
class QCloseEvent;
class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTabWidget;
class QToolBar;

class Window : public QMainWindow {
public:
    explicit Window(const QString &sessionFile = {}, int autosaveIntervalMs = 5000);
    bool openDocument(const QString &path, bool restoring = false);
    void closeTab(int index);
    bool saveSession();

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void setupToolbars();
    void applyStyle();
    void restoreSession();
    void addEmptyTab();
    void connectView(PdfView *pdf);
    void syncControls();
    void syncSearch();
    void enableControls(bool enabled);

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
