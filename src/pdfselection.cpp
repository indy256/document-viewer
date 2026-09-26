#include "pdfview.h"
#include <fpdf_text.h>
#include <QApplication>
#include <QClipboard>
#include <QMouseEvent>
#include <QScrollBar>
#include <algorithm>
#include <limits>

TextPage *PdfView::textPage(int index) {
    if (auto cached = textCache.object(index)) return cached;
    auto content = std::make_unique<TextPage>();
    if (djvu) *content = djvu->textPage(index);
    else if (document) {
        auto page = FPDF_LoadPage(document, index);
        auto text = page ? FPDFText_LoadPage(page) : nullptr;
        if (text) {
            for (int i = 0; i < FPDFText_CountChars(text); ++i) {
                const char32_t unicode = FPDFText_GetUnicode(text, i);
                if (!unicode || unicode > 0x10ffff) continue;
                const auto character = QString::fromUcs4(&unicode, 1);
                QRectF box;
                double left, right, bottom, top;
                int x1, y1, x2, y2;
                constexpr int extent = 1000000;
                if (FPDFText_GetCharBox(text, i, &left, &right, &bottom, &top)
                    && FPDF_PageToDevice(page, 0, 0, extent, extent, 0, left, top, &x1, &y1)
                    && FPDF_PageToDevice(page, 0, 0, extent, extent, 0, right, bottom, &x2, &y2)) {
                    box = QRectF(QPointF(double(x1) / extent, double(y1) / extent),
                        QPointF(double(x2) / extent, double(y2) / extent)).normalized();
                }
                content->spans.append({content->text.size(), character.size(), box});
                content->text += character;
            }
            FPDFText_ClosePage(text);
        }
        if (page) FPDF_ClosePage(page);
    }
    const auto cost = int(std::min<qsizetype>(8192,
        1 + (content->text.size() * sizeof(QChar) + content->spans.size() * sizeof(TextSpan)) / 1024));
    auto result = content.release();
    textCache.insert(index, result, cost);
    return result;
}

PdfView::TextPosition PdfView::textAt(const QPoint &position, bool nearest) {
    const QPoint point = position + QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
    auto distance = [](const QPointF &p, const QRectF &r) {
        const double dx = std::max({r.left() - p.x(), 0.0, p.x() - r.right()});
        const double dy = std::max({r.top() - p.y(), 0.0, p.y() - r.bottom()});
        return dx * dx + dy * dy;
    };
    int page = -1;
    double closest = std::numeric_limits<double>::max();
    for (int i = 0; i < pages.size(); ++i) {
        const double d = distance(point, pages[i]);
        if (d < closest) { closest = d; page = i; }
        if (d == 0) break;
    }
    if (page < 0 || (!nearest && closest > 0)) return {};
    const auto content = textPage(page);
    const auto rect = pages[page].toAlignedRect();
    TextPosition result;
    closest = nearest ? std::numeric_limits<double>::max() : 16.0;
    for (int i = 0; i < content->spans.size(); ++i) {
        const auto &box = content->spans[i].box;
        if (box.isEmpty()) continue;
        const QRectF scaled(rect.x() + box.x() * rect.width(), rect.y() + box.y() * rect.height(),
            box.width() * rect.width(), box.height() * rect.height());
        const double d = distance(point, scaled);
        if (d < closest) { closest = d; result = {page, i}; }
        if (d == 0) break;
    }
    return result;
}

QPair<int, int> PdfView::selectionRange(int page, int count) const {
    if (!selectionVisible) return {0, 0};
    const auto first = std::min(selectionAnchor, selectionEnd);
    const auto last = std::max(selectionAnchor, selectionEnd);
    if (page < first.page || page > last.page) return {0, 0};
    return {page == first.page ? first.span : 0, page == last.page ? last.span + 1 : count};
}

void PdfView::clearSelection() {
    selectionScrollTimer.stop();
    selecting = selectionVisible = false;
    selectionAnchor = selectionEnd = {};
    viewport()->update();
}

void PdfView::extendSelection() {
    const auto end = textAt(selectionPointer, true);
    if (end.valid()) selectionEnd = end;
    viewport()->update();
}

QString PdfView::selectedText() {
    if (!selectionVisible) return {};
    QStringList parts;
    const auto first = std::min(selectionAnchor, selectionEnd);
    const auto last = std::max(selectionAnchor, selectionEnd);
    for (int page = first.page; page <= last.page; ++page) {
        const auto content = textPage(page);
        const auto range = selectionRange(page, int(content->spans.size()));
        if (range.first == range.second) continue;
        const auto &start = content->spans[range.first];
        const auto &end = content->spans[range.second - 1];
        parts.append(content->text.mid(start.start, end.start + end.length - start.start));
    }
    auto text = parts.join('\n');
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    return text;
}

void PdfView::copySelection() {
    const auto text = selectedText();
    if (!text.isEmpty()) QApplication::clipboard()->setText(text);
}

void PdfView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) { QAbstractScrollArea::mouseDoubleClickEvent(event); return; }
    clearSelection();
    pressedLink = {};
    selectionAnchor = selectionEnd = textAt(event->position().toPoint());
    if (selectionAnchor.valid()) {
        const auto content = textPage(selectionAnchor.page);
        auto isWord = [&](int index) {
            const auto &span = content->spans[index];
            const auto text = QStringView(content->text).mid(span.start, span.length);
            return !text.isEmpty() && std::all_of(text.begin(), text.end(), [](QChar c) {
                return c.isLetterOrNumber() || c.isMark() || c.isSurrogate() || c == '_';
            });
        };
        if (isWord(selectionAnchor.span)) {
            // Separate OCR spans can have spaces between them in the text stream.
            while (selectionAnchor.span > 0 && isWord(selectionAnchor.span - 1)) {
                const auto &previous = content->spans[selectionAnchor.span - 1];
                if (previous.start + previous.length != content->spans[selectionAnchor.span].start) break;
                --selectionAnchor.span;
            }
            while (selectionEnd.span + 1 < content->spans.size() && isWord(selectionEnd.span + 1)) {
                const auto &current = content->spans[selectionEnd.span];
                if (current.start + current.length != content->spans[selectionEnd.span + 1].start) break;
                ++selectionEnd.span;
            }
        }
        selectionVisible = true;
    }
    setFocus(Qt::MouseFocusReason);
    viewport()->update();
    event->accept();
}
