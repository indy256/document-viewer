#pragma once
#include <QAbstractScrollArea>
#include <QByteArray>
#include <QCache>
#include <QImage>
#include <QVector>
#include <QTimer>
#include <fpdfview.h>
#include "epub.h"

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
    void goToPage(int page);
    void search(const QString &text);
    void nextMatch(int direction = 1);
    QString searchText() const { return query; }
    int matchCount() const { return matches.size(); }
    int currentMatch() const { return selectedMatch; }
    bool isSearching() const { return searchTimer.isActive(); }
signals:
    void pageChanged(int page);
    void zoomChanged(double zoom);
    void searchChanged();
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void scrollContentsBy(int, int) override;
private:
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
    void updateLinkCursor();
    LinkTarget pressedLink;
    QPoint pressPosition;
    EpubDestinations epubDestinations;
    struct Match { int page; QVector<QRectF> rectangles; }; // Normalized display coordinates.
    QVector<Match> matches;
    QString query;
    int selectedMatch = -1;
    int nextSearchPage = 0;
    QTimer searchTimer;
    FPDF_DOCUMENT document = nullptr;
    QByteArray bytes;
    QVector<QSizeF> sizes;
    QVector<QRectF> pages;
    QCache<QString, QImage> cache{64 * 1024}; // Cost in KiB, at most 64 MiB.
    double scale = 1.0;
    Fit fit = Fit::Width;
};
