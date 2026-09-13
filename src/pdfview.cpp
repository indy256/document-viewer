#include "pdfview.h"
#include "epub.h"
#include <fpdf_text.h>
#include <QFile>
#include <QFileInfo>
#include <QApplication>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

PdfView::PdfView(QWidget *parent) : QAbstractScrollArea(parent) {
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    verticalScrollBar()->setSingleStep(60); // Three times Qt's default wheel scrolling distance.
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAutoFillBackground(false);
    connect(&searchTimer, &QTimer::timeout, this, &PdfView::searchPage);
}

PdfView::~PdfView() {
    if (document) FPDF_CloseDocument(document);
}

bool PdfView::open(const QString &path, const QString &password, QString *error) {
    QByteArray data;
    if (QFileInfo(path).suffix().compare("epub", Qt::CaseInsensitive) == 0) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        data = renderEpub(path, error);
        QApplication::restoreOverrideCursor();
        if (data.isEmpty()) return false;
    } else {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = file.errorString();
            return false;
        }
        data = file.readAll();
    }
    const auto pass = password.toUtf8();
    auto next = FPDF_LoadMemDocument64(data.constData(), size_t(data.size()), pass.constData());
    if (!next) {
        *error = FPDF_GetLastError() == FPDF_ERR_PASSWORD
            ? QStringLiteral("This PDF needs a valid password.")
            : QStringLiteral("This file could not be read as a PDF. It may be damaged or unsupported.");
        return false;
    }
    QVector<QSizeF> nextSizes;
    const int count = FPDF_GetPageCount(next);
    for (int i = 0; i < count; ++i) {
        FS_SIZEF size{};
        if (!FPDF_GetPageSizeByIndexF(next, i, &size) || !std::isfinite(size.width)
            || !std::isfinite(size.height) || size.width <= 0 || size.height <= 0
            || size.width > 100000 || size.height > 100000) {
            FPDF_CloseDocument(next);
            *error = QStringLiteral("The PDF contains an invalid page size.");
            return false;
        }
        nextSizes.append(QSizeF(size.width, size.height));
    }
    if (nextSizes.isEmpty()) {
        FPDF_CloseDocument(next);
        *error = QStringLiteral("This PDF has no pages.");
        return false;
    }
    if (document) FPDF_CloseDocument(document);
    document = next;
    bytes = std::move(data); // PDFium borrows this memory until the document closes.
    sizes = std::move(nextSizes);
    search(QString());
    pages.clear();
    cache.clear();
    fit = Fit::Width;
    applyFit();
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
    emit pageChanged(0);
    return true;
}

int PdfView::currentPage() const {
    const double y = verticalScrollBar()->value() + viewport()->height() / 2.0;
    for (int i = 0; i < pages.size(); ++i)
        if (pages[i].bottom() >= y) return i;
    return std::max(0, int(pages.size()) - 1);
}

void PdfView::layoutPages() {
    pages.clear();
    double width = viewport()->width();
    for (const auto &size : sizes) width = std::max(width, size.width() * scale + 48);
    double y = 24;
    for (const auto &size : sizes) {
        const QSizeF scaled = size * scale;
        pages.append(QRectF(QPointF((width - scaled.width()) / 2, y), scaled));
        y += scaled.height() + 24;
    }
    verticalScrollBar()->setRange(0, int(std::min(2000000000.0, std::max(0.0, y - viewport()->height()))));
    verticalScrollBar()->setPageStep(viewport()->height());
    horizontalScrollBar()->setRange(0, int(std::max(0.0, width - viewport()->width())));
    horizontalScrollBar()->setPageStep(viewport()->width());
    viewport()->update();
}

void PdfView::setZoom(double value) {
    if (sizes.isEmpty()) return;
    const int page = currentPage();
    const QPointF center(horizontalScrollBar()->value() + viewport()->width() / 2.0,
                         verticalScrollBar()->value() + viewport()->height() / 2.0);
    const QPointF anchor = (center - pages[page].topLeft()) / scale;
    fit = Fit::Custom;
    scale = std::clamp(value, 0.1, 5.0);
    cache.clear();
    layoutPages();
    const QPointF target = pages[page].topLeft() + anchor * scale;
    horizontalScrollBar()->setValue(qRound(target.x() - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(qRound(target.y() - viewport()->height() / 2.0));
    emit zoomChanged(scale);
}

void PdfView::applyFit() {
    if (sizes.isEmpty()) return;
    const int page = currentPage();
    const Fit mode = fit;
    double width = 0;
    for (const auto &size : sizes) width = std::max(width, size.width());
    double value = std::max(1, viewport()->width() - 48) / width;
    if (mode == Fit::Page)
        value = std::min(std::max(1, viewport()->width() - 48) / sizes[page].width(),
                         std::max(1, viewport()->height() - 48) / sizes[page].height());
    if (pages.isEmpty()) layoutPages();
    setZoom(value);
    fit = mode;
}

void PdfView::setFit(Fit mode) {
    fit = mode;
    if (fit != Fit::Custom) applyFit();
}

void PdfView::goToPage(int page) {
    if (page < 0 || page >= pages.size()) return;
    verticalScrollBar()->setValue(qRound(pages[page].top() - 24));
}

void PdfView::search(const QString &text) {
    searchTimer.stop();
    query = text;
    matches.clear();
    selectedMatch = -1;
    nextSearchPage = 0;
    if (document && !query.isEmpty()) searchTimer.start(200); // Debounce typing.
    viewport()->update();
    emit searchChanged();
}

void PdfView::searchPage() {
    // Yield between pages so typing a new query or switching tabs can cancel/update a search.
    const int pageIndex = nextSearchPage++;
    auto page = FPDF_LoadPage(document, pageIndex);
    if (page) {
        auto text = FPDFText_LoadPage(page);
        if (text) {
            auto handle = FPDFText_FindStart(text, query.utf16(), FPDF_CONSECUTIVE, 0);
            if (handle) {
                while (FPDFText_FindNext(handle)) {
                    Match match{pageIndex, {}};
                    const int count = FPDFText_CountRects(text, FPDFText_GetSchResultIndex(handle), FPDFText_GetSchCount(handle));
                    for (int i = 0; i < count; ++i) {
                        double left, top, right, bottom;
                        if (!FPDFText_GetRect(text, i, &left, &top, &right, &bottom)) continue;
                        int x1, y1, x2, y2;
                        // PDFium performs the crop-box and page-rotation coordinate transform.
                        constexpr int extent = 1000000;
                        if (FPDF_PageToDevice(page, 0, 0, extent, extent, 0, left, top, &x1, &y1)
                            && FPDF_PageToDevice(page, 0, 0, extent, extent, 0, right, bottom, &x2, &y2)) {
                            match.rectangles.append(QRectF(QPointF(double(x1) / extent, double(y1) / extent),
                                QPointF(double(x2) / extent, double(y2) / extent)).normalized());
                        }
                    }
                    matches.append(match);
                }
                FPDFText_FindClose(handle);
            }
            FPDFText_ClosePage(text);
        }
        FPDF_ClosePage(page);
    }
    if (nextSearchPage >= pageCount()) {
        searchTimer.stop();
    } else searchTimer.start(1);
    if (selectedMatch < 0 && !matches.isEmpty()) {
        selectedMatch = 0;
        revealMatch();
    }
    viewport()->update();
    emit searchChanged();
}

void PdfView::nextMatch(int direction) {
    if (matches.isEmpty()) return;
    selectedMatch = (selectedMatch + direction + matches.size()) % matches.size();
    revealMatch();
    viewport()->update();
    emit searchChanged();
}

void PdfView::revealMatch() {
    if (selectedMatch < 0 || selectedMatch >= matches.size()) return;
    const auto &match = matches[selectedMatch];
    if (match.rectangles.isEmpty()) { goToPage(match.page); return; }
    const auto &page = pages[match.page];
    const auto point = match.rectangles.first().center();
    horizontalScrollBar()->setValue(qRound(page.x() + point.x() * page.width() - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(qRound(page.y() + point.y() * page.height() - viewport()->height() / 2.0));
}

void PdfView::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    if (fit != Fit::Custom) applyFit(); else layoutPages();
}

void PdfView::wheelEvent(QWheelEvent *event) {
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const double delta = event->angleDelta().y() ? event->angleDelta().y() : event->pixelDelta().y();
        setZoom(scale * std::pow(1.15, delta / 120.0));
        event->accept();
    } else QAbstractScrollArea::wheelEvent(event);
}

void PdfView::scrollContentsBy(int, int) {
    viewport()->update();
    emit pageChanged(currentPage());
}

void PdfView::paintEvent(QPaintEvent *) {
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), QColor("#e8ecf2"));
    if (!document) {
        painter.setPen(QColor("#26344b"));
        painter.setFont(QFont("Segoe UI", 23, QFont::DemiBold));
        painter.drawText(viewport()->rect().adjusted(0, -50, 0, -50), Qt::AlignCenter, "A little space to read.");
        painter.setFont(QFont("Segoe UI", 11));
        painter.setPen(QColor("#66758c"));
        painter.drawText(viewport()->rect().adjusted(0, 40, 0, 40), Qt::AlignCenter,
                         "Open a PDF or EPUB, or drop one here\nCtrl+O to open  ·  Ctrl+wheel to zoom");
        return;
    }
    const QPoint offset(horizontalScrollBar()->value(), verticalScrollBar()->value());
    constexpr int tileSize = 512;
    const double dpr = devicePixelRatioF();
    for (int i = 0; i < pages.size(); ++i) {
        const QRect rect = pages[i].translated(-offset).toAlignedRect();
        if (rect.top() > viewport()->height()) break;
        if (rect.bottom() < 0) continue;
        painter.fillRect(rect.translated(2, 3), QColor("#cbd2dc"));
        painter.fillRect(rect, Qt::white);
        const QRect visible = rect.intersected(viewport()->rect()).translated(-rect.topLeft());
        if (visible.isEmpty()) continue;
        FPDF_PAGE page = nullptr;
        for (int y = visible.top() / tileSize; y <= visible.bottom() / tileSize; ++y) {
            for (int x = visible.left() / tileSize; x <= visible.right() / tileSize; ++x) {
                const QString key = QString("%1/%2/%3/%4").arg(i).arg(x).arg(y).arg(dpr);
                QImage *image = cache.object(key);
                if (!image) {
                    if (!page) page = FPDF_LoadPage(document, i);
                    if (!page) continue;
                    const int w = qCeil(std::min(tileSize, rect.width() - x * tileSize) * dpr);
                    const int h = qCeil(std::min(tileSize, rect.height() - y * tileSize) * dpr);
                    auto rendered = new QImage(w, h, QImage::Format_ARGB32);
                    if (rendered->isNull()) { delete rendered; continue; }
                    rendered->fill(Qt::white);
                    auto bitmap = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA,
                        rendered->bits(), rendered->bytesPerLine());
                    if (!bitmap) { delete rendered; continue; }
                    FPDF_RenderPageBitmap(bitmap, page, -qRound(x * tileSize * dpr), -qRound(y * tileSize * dpr),
                        qCeil(rect.width() * dpr), qCeil(rect.height() * dpr), 0, FPDF_ANNOT);
                    FPDFBitmap_Destroy(bitmap);
                    rendered->setDevicePixelRatio(dpr);
                    const int cost = int(rendered->sizeInBytes() / 1024) + 1;
                    cache.insert(key, rendered, cost);
                    image = cache.object(key);
                }
                if (image) painter.drawImage(rect.topLeft() + QPoint(x * tileSize, y * tileSize), *image);
            }
        }
        if (page) FPDF_ClosePage(page);
        for (int m = 0; m < matches.size(); ++m) {
            const auto &match = matches[m];
            if (match.page < i) continue;
            if (match.page > i) break;
            for (const auto &box : match.rectangles) {
                const QRectF highlight(rect.x() + box.x() * rect.width(), rect.y() + box.y() * rect.height(),
                                       box.width() * rect.width(), box.height() * rect.height());
                painter.fillRect(highlight, m == selectedMatch ? QColor(255, 140, 30, 125) : QColor(255, 220, 30, 100));
                if (m == selectedMatch) {
                    painter.setPen(QColor(210, 100, 15));
                    painter.drawRect(highlight);
                }
            }
        }
    }
}
