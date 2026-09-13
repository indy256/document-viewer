#pragma once
#include <QAbstractScrollArea>
#include <QByteArray>
#include <QCache>
#include <QImage>
#include <QVector>
#include <QTimer>
#include <fpdfview.h>

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
    void scrollContentsBy(int, int) override;
private:
    void layoutPages();
    void applyFit();
    void searchPage();
    void revealMatch();
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
