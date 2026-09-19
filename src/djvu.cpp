#include "djvu.h"
#include <ddjvuapi.h>
#include <miniexp.h>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <functional>

struct DjvuDocument::Impl {
    ddjvu_context_t *context = ddjvu_context_create("DocumentViewer");
    ddjvu_document_t *document = nullptr;
    ddjvu_page_t *page = nullptr;
    int pageIndex = -1;
    QVector<QSizeF> pageSizes;
    QByteArray input;
    QDir directory;
    QString failure;
    ~Impl() {
        if (page) ddjvu_page_release(page);
        if (document) ddjvu_document_release(document);
        if (context) ddjvu_context_release(context);
    }
    bool wait(const std::function<bool()> &ready) {
        QElapsedTimer timer;
        timer.start();
        for (;;) {
            const bool done = ready();
            while (const auto *message = ddjvu_message_peek(context)) {
                if (message->m_any.tag == DDJVU_ERROR)
                    failure = QString::fromUtf8(message->m_error.message);
                if (message->m_any.tag == DDJVU_NEWSTREAM) {
                    const int id = message->m_newstream.streamid;
                    QByteArray data;
                    if (id == 0) data = std::move(input);
                    else {
                        // Indirect DjVu books may reference sibling component files.
                        // Keep those reads within the book directory, including symlinks.
                        const auto name = QString::fromUtf8(message->m_newstream.name);
                        const auto path = QFileInfo(directory.filePath(name)).canonicalFilePath();
                        const auto relative = directory.relativeFilePath(path);
                        if (path.isEmpty() || QDir::isAbsolutePath(relative) || relative == ".."
                            || relative.startsWith("../")) {
                            failure = "A DjVu component is missing or outside the book directory.";
                        } else {
                            QFile component(path);
                            if (component.open(QIODevice::ReadOnly)) data = component.readAll();
                            else failure = component.errorString();
                        }
                    }
                    if (failure.isEmpty())
                        ddjvu_stream_write(document, id, data.constData(), data.size());
                    ddjvu_stream_close(document, id, !failure.isEmpty());
                }
                ddjvu_message_pop(context);
            }
            if (!failure.isEmpty()) return false;
            if (done) return true;
            if (timer.elapsed() > 30000) {
                failure = "Timed out decoding the DjVu document.";
                return false;
            }
            QThread::msleep(1);
        }
    }
    bool loadPage(int index) {
        if (!document || index < 0 || index >= pageSizes.size()) return false;
        if (page && pageIndex == index) return true;
        if (page) ddjvu_page_release(page);
        page = ddjvu_page_create_by_pageno(document, index);
        pageIndex = index;
        if (!page) return false;
        if (!wait([&] { return ddjvu_page_decoding_done(page); }) || ddjvu_page_decoding_error(page)) {
            ddjvu_page_release(page);
            page = nullptr;
            return false;
        }
        return true;
    }
};

DjvuDocument::DjvuDocument() : impl(std::make_unique<Impl>()) {}
DjvuDocument::~DjvuDocument() = default;

bool DjvuDocument::open(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return false; }
    if (file.read(8) != QByteArray("AT&TFORM", 8)) {
        *error = "This file could not be read as DjVu. It may be damaged or unsupported.";
        return false;
    }
    auto next = std::make_unique<Impl>();
    if (!next->context) { *error = "Could not initialize the DjVu decoder."; return false; }
    file.seek(0);
    next->input = file.readAll();
    next->directory = QFileInfo(file).canonicalPath();
    next->document = ddjvu_document_create(next->context, nullptr, false);
    if (!next->document || !next->wait([&] { return ddjvu_document_decoding_done(next->document); })
        || ddjvu_document_decoding_error(next->document)) {
        *error = "Could not open DjVu document: " + next->failure;
        return false;
    }
    const int count = ddjvu_document_get_pagenum(next->document);
    if (count <= 0) { *error = "This DjVu document has no pages."; return false; }
    for (int i = 0; i < count; ++i) {
        ddjvu_pageinfo_t info{};
        ddjvu_status_t status = DDJVU_JOB_NOTSTARTED;
        if (!next->wait([&] {
                status = ddjvu_document_get_pageinfo(next->document, i, &info);
                return status >= DDJVU_JOB_OK;
            }) || status != DDJVU_JOB_OK || info.width <= 0 || info.height <= 0
            || info.width > 100000 || info.height > 100000 || info.dpi <= 0) {
            *error = "The DjVu document contains an unreadable page. " + next->failure;
            return false;
        }
        next->pageSizes.append(QSizeF(info.width * 72.0 / info.dpi, info.height * 72.0 / info.dpi));
    }
    impl = std::move(next);
    return true;
}

QVector<QSizeF> DjvuDocument::sizes() const { return impl->pageSizes; }

QImage DjvuDocument::render(int page, const QSize &fullSize, const QRect &tile) {
    if (fullSize.isEmpty() || tile.isEmpty() || !QRect(QPoint(), fullSize).contains(tile)) return {};
    if (!impl->loadPage(page)) return {};
    QImage image(tile.size(), QImage::Format_RGB32);
    if (image.isNull()) return {};
    unsigned int masks[] = {0xff0000, 0xff00, 0xff, 0xff000000};
    auto format = ddjvu_format_create(DDJVU_FORMAT_RGBMASK32, 4, masks);
    if (!format) return {};
    ddjvu_format_set_row_order(format, true);
    ddjvu_format_set_y_direction(format, true);
    const ddjvu_rect_t full{0, 0, unsigned(fullSize.width()), unsigned(fullSize.height())};
    const ddjvu_rect_t part{tile.x(), tile.y(), unsigned(tile.width()), unsigned(tile.height())};
    const bool ok = ddjvu_page_render(impl->page, DDJVU_RENDER_COLOR, &full, &part,
        format, image.bytesPerLine(), reinterpret_cast<char *>(image.bits()));
    ddjvu_format_release(format);
    return ok ? image : QImage();
}

QVector<QVector<QRectF>> DjvuDocument::search(int page, const QString &query) {
    QVector<QVector<QRectF>> matches;
    if (query.isEmpty() || !impl->loadPage(page)) return matches;
    miniexp_t expression = miniexp_dummy;
    if (!impl->wait([&] {
        expression = ddjvu_document_get_pagetext(impl->document, page, "word");
        return expression != miniexp_dummy;
    })) return matches;
    QString text;
    QVector<QRectF> boxes;
    const double width = ddjvu_page_get_width(impl->page);
    const double height = ddjvu_page_get_height(impl->page);
    const int rotation = ddjvu_page_get_rotation(impl->page);
    const double originalWidth = rotation % 2 ? height : width;
    const double originalHeight = rotation % 2 ? width : height;
    auto point = [&](double x, double y) {
        x /= originalWidth; y /= originalHeight;
        switch (rotation) {
        case 1: return QPointF(1 - y, 1 - x);
        case 2: return QPointF(1 - x, y);
        case 3: return QPointF(y, x);
        default: return QPointF(x, 1 - y);
        }
    };
    std::function<void(miniexp_t, int)> visit = [&](miniexp_t node, int depth) {
        if (depth > 32 || miniexp_length(node) < 6) return;
        const auto box = QRectF(point(miniexp_to_int(miniexp_nth(1, node)), miniexp_to_int(miniexp_nth(2, node))),
            point(miniexp_to_int(miniexp_nth(3, node)), miniexp_to_int(miniexp_nth(4, node)))).normalized();
        const auto tail = miniexp_nth(5, node);
        if (miniexp_stringp(tail)) {
            const auto word = QString::fromUtf8(miniexp_to_str(tail));
            if (!text.isEmpty()) { text += ' '; boxes.append(QRectF()); }
            text += word;
            for (qsizetype i = 0; i < word.size(); ++i) boxes.append(box);
            return;
        }
        for (int i = 5; i < miniexp_length(node); ++i) visit(miniexp_nth(i, node), depth + 1);
    };
    visit(expression, 0);
    ddjvu_miniexp_release(impl->document, expression);
    for (qsizetype start = 0; (start = text.indexOf(query, start, Qt::CaseInsensitive)) >= 0; ++start) {
        QVector<QRectF> rectangles;
        for (qsizetype i = start; i < start + query.size(); ++i)
            if (!boxes[i].isEmpty() && !rectangles.contains(boxes[i])) rectangles.append(boxes[i]);
        matches.append(rectangles);
    }
    return matches;
}
