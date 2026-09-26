#pragma once
#include <QAbstractScrollArea>
#include <QByteArray>
#include <QCache>
#include <QImage>
#include <QVector>
#include <QTimer>
#include <fpdfview.h>
#include "epub.h"
#include "djvu.h"

class PdfView : public QAbstractScrollArea {
    Q_OBJECT
public:
    enum class Fit { Custom, Width, Page };
    explicit PdfView(QWidget *parent = nullptr);
    ~PdfView() override;
    bool open(const QString &path, const QString &password, QString *error);
    int pageCount() const { return sizes.size(); }
    int currentPage() const;
    double zoom() const { return scale; }
    Fit fitMode() const { return fit; }
    void setZoom(double value);
    void setFit(Fit mode);
    void goToPage(int page, bool remember = true);
    void goBack();
    void goForward();
    void search(const QString &text);
    void nextMatch(int direction = 1);
    QString searchText() const { return query; }
    int matchCount() const { return matches.size(); }
    int currentMatch() const { return selectedMatch; }
    bool isSearching() const { return searchTimer.isActive(); }
    bool hasSelection() const { return selectionVisible; }
    QString selectedText();
    void copySelection();
signals:
    void pageChanged(int page);
    void zoomChanged(double zoom);
    void searchChanged();
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void scrollContentsBy(int, int) override;
private:
    struct NavigationPosition {
        int page;
        QPointF offset;
        double zoom;
        Fit fit;
        bool operator==(const NavigationPosition &other) const {
            return page == other.page && offset == other.offset && zoom == other.zoom && fit == other.fit;
        }
    };
    NavigationPosition navigationPosition() const;
    void recordNavigation(const NavigationPosition &before);
    void restoreNavigation(const NavigationPosition &position);
    QVector<NavigationPosition> backHistory, forwardHistory;
    void layoutPages();
    void applyFit();
    void searchPage();
    void revealMatch();
    struct LinkTarget {
        int page = -1;
        QPointF position;
        bool hasX = false;
        bool hasY = false;
        double zoom = 0;
        QUrl url;
        bool valid() const { return page >= 0 || !url.isEmpty(); }
        bool operator==(const LinkTarget &other) const {
            return page == other.page && position == other.position && hasX == other.hasX
                && hasY == other.hasY && zoom == other.zoom && url == other.url;
        }
    };
    LinkTarget linkAt(const QPoint &position) const;
    void activateLink(const LinkTarget &target);
    void followLink(const LinkTarget &target);
    void updateLinkCursor();
    struct TextPosition {
        int page = -1;
        int span = -1;
        bool valid() const { return page >= 0 && span >= 0; }
        bool operator<(const TextPosition &other) const {
            return page < other.page || (page == other.page && span < other.span);
        }
    };
    TextPage *textPage(int page);
    TextPosition textAt(const QPoint &position, bool nearest = false);
    QPair<int, int> selectionRange(int page, int count) const;
    void clearSelection();
    void extendSelection();
    QCache<int, TextPage> textCache{8192}; // KiB, independent of render/zoom cache.
    TextPosition selectionAnchor, selectionEnd;
    bool selecting = false;
    bool selectionVisible = false;
    QPoint selectionPointer;
    QTimer selectionScrollTimer;
    LinkTarget pressedLink;
    QPoint pressPosition;
    EpubDestinations epubDestinations;
    struct Match { int page; QVector<QRectF> rectangles; }; // Normalized display coordinates.
    QVector<Match> matches;
    QString query;
    int selectedMatch = -1;
    int nextSearchPage = 0;
    QTimer searchTimer;
    std::unique_ptr<DjvuDocument> djvu;
    FPDF_DOCUMENT document = nullptr;
    QByteArray bytes;
    QVector<QSizeF> sizes;
    QVector<QRectF> pages;
    QCache<QString, QImage> cache{64 * 1024}; // Cost in KiB, at most 64 MiB.
    double scale = 1.0;
    Fit fit = Fit::Width;
};
