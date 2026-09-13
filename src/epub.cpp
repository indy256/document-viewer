#include "epub.h"
#include <QtCore/private/qzipreader_p.h>
#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QPdfWriter>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QUrl>
#include <QXmlStreamReader>
#include <algorithm>

namespace {
// Resolve archive URLs without extracting files or allowing local/network resources.
QString resourcePath(const QUrl &base, const QString &reference) {
    const auto url = base.resolved(QUrl(reference));
    if (url.scheme() != "epub" || !url.host().isEmpty()) return {};
    QString path = QDir::cleanPath(url.path(QUrl::FullyDecoded));
    if (path.startsWith('/')) path.remove(0, 1);
    if (path.isEmpty() || path == ".." || path.startsWith("../") || path.contains('\\')) return {};
    return path;
}

QUrl archiveUrl(const QString &path) {
    QUrl url;
    url.setScheme("epub");
    url.setPath('/' + path);
    return url;
}

class BookDocument : public QTextDocument {
public:
    explicit BookDocument(QZipReader &zip) : archive(zip) {}
protected:
    QVariant loadResource(int type, const QUrl &url) override {
        const auto path = resourcePath(baseUrl(), url.toString());
        if (path.isEmpty()) return {};
        const auto data = archive.fileData(path);
        if (type == ImageResource) return QImage::fromData(data);
        if (type == StyleSheetResource) return QString::fromUtf8(data);
        return {}; // Never fall back to QTextDocument's filesystem resource loader.
    }
private:
    QZipReader &archive;
};
}

QByteArray renderEpub(const QString &path, QString *error) {
    auto fail = [error](const QString &message) { *error = message; return QByteArray(); };
    QZipReader archive(path);
    if (!archive.isReadable()) return fail("Could not read the EPUB archive.");
    const auto entries = archive.fileInfoList();
    qint64 total = 0;
    if (entries.size() > 20000) return fail("This EPUB contains too many resources.");
    for (const auto &entry : entries) {
        total += entry.size;
        if (entry.size < 0 || entry.size > 64 * 1024 * 1024 || total > 256 * 1024 * 1024)
            return fail("This EPUB exceeds the supported resource size (64 MiB per file, 256 MiB total).");
    }
    if (archive.status() != QZipReader::NoError || archive.fileData("mimetype").trimmed() != "application/epub+zip")
        return fail("This file is not a valid EPUB archive.");

    QXmlStreamReader container(archive.fileData("META-INF/container.xml"));
    QString packagePath;
    while (!container.atEnd()) {
        container.readNext();
        if (container.isStartElement() && container.name() == "rootfile" && packagePath.isEmpty())
            packagePath = resourcePath(archiveUrl(""), container.attributes().value("full-path").toString());
    }
    if (container.hasError() || packagePath.isEmpty()) return fail("The EPUB has no valid package document.");

    struct Item { QString path; QString mediaType; };
    QHash<QString, Item> manifest;
    QStringList spine;
    QString title;
    QXmlStreamReader package(archive.fileData(packagePath));
    while (!package.atEnd()) {
        package.readNext();
        if (!package.isStartElement()) continue;
        const auto attrs = package.attributes();
        if (package.name() == "item") {
            manifest.insert(attrs.value("id").toString(), {
                resourcePath(archiveUrl(packagePath), attrs.value("href").toString()),
                attrs.value("media-type").toString()});
        } else if (package.name() == "itemref") {
            spine.append(attrs.value("idref").toString());
        } else if (package.name() == "title" && title.isEmpty()) {
            title = package.readElementText();
        }
    }
    if (package.hasError() || spine.isEmpty()) return fail("The EPUB has no valid reading order.");

    const auto encryptionData = archive.fileData("META-INF/encryption.xml");
    if (!encryptionData.isEmpty()) {
        QXmlStreamReader encryption(encryptionData);
        while (!encryption.atEnd()) {
            encryption.readNext();
            if (encryption.isStartElement() && encryption.name() == "EncryptionMethod") {
                const auto algorithm = encryption.attributes().value("Algorithm");
                // Font obfuscation does not prevent reading: the renderer uses installed fonts.
                if (algorithm != "http://www.idpf.org/2008/embedding"
                    && algorithm != "http://ns.adobe.com/pdf/enc#RC")
                    return fail("Encrypted / DRM-protected EPUBs are not supported.");
            }
        }
        if (encryption.hasError()) return fail("The EPUB encryption metadata is invalid.");
    }

    QByteArray output;
    QBuffer buffer(&output);
    buffer.open(QIODevice::WriteOnly);
    {
        QPdfWriter writer(&buffer);
        writer.setResolution(72);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(18, 18, 18, 18));
        writer.setTitle(title);
        QPainter painter(&writer);
        if (!painter.isActive()) return fail("Could not prepare the EPUB pages.");
        bool firstPage = true;
        for (const auto &id : spine) {
            const auto item = manifest.value(id);
            if (item.path.isEmpty()) return fail("The EPUB reading order references a missing item.");
            const auto data = archive.fileData(item.path);
            if (data.isEmpty()) return fail("An EPUB chapter is missing or empty: " + item.path);
            QString html;
            QString css = "body { color: #202020; } p { margin-top: 0; margin-bottom: 10px; }";
            if (item.mediaType == "application/xhtml+xml" || item.mediaType == "text/html") {
                html = QString::fromUtf8(data);
                QXmlStreamReader chapter(data);
                while (!chapter.atEnd()) {
                    chapter.readNext();
                    if (chapter.isStartElement() && chapter.name() == "link"
                        && chapter.attributes().value("rel").toString().split(' ').contains("stylesheet")) {
                        const auto cssPath = resourcePath(archiveUrl(item.path), chapter.attributes().value("href").toString());
                        if (!cssPath.isEmpty()) css += '\n' + QString::fromUtf8(archive.fileData(cssPath));
                    }
                }
                // Let the HTML renderer handle named entities such as &nbsp; even when
                // the XML stylesheet scan cannot resolve an external XHTML DTD.
            } else if (item.mediaType.startsWith("image/")) {
                html = QString("<html><body><img src=\"%1\" width=\"%2\" /></body></html>")
                    .arg(archiveUrl(item.path).toString().toHtmlEscaped()).arg(writer.width() - 16);
            } else return fail("Unsupported EPUB chapter format: " + item.mediaType);

            BookDocument document(archive);
            document.setBaseUrl(archiveUrl(item.path));
            document.setDefaultFont(QFont("Segoe UI", 12));
            document.setDefaultStyleSheet(css);
            document.documentLayout()->setPaintDevice(&writer);
            document.setHtml(html);
            // Keep cover art and illustrations inside the printable page.
            for (auto block = document.begin(); block.isValid(); block = block.next()) {
                for (auto it = block.begin(); !it.atEnd(); ++it) {
                    const auto fragment = it.fragment();
                    if (!fragment.charFormat().isImageFormat()) continue;
                    auto format = fragment.charFormat().toImageFormat();
                    const auto image = qvariant_cast<QImage>(document.resource(QTextDocument::ImageResource, QUrl(format.name())));
                    double width = format.width() > 0 ? format.width() : image.width();
                    double height = format.height() > 0 ? format.height() : image.height();
                    if (width <= 0 || height <= 0) continue;
                    if (format.width() > 0 && format.height() <= 0 && image.width() > 0)
                        height = image.height() * width / image.width();
                    const double factor = std::min({1.0, (writer.width() - 16.0) / width, (writer.height() - 16.0) / height});
                    format.setWidth(width * factor);
                    format.setHeight(height * factor);
                    QTextCursor cursor(&document);
                    cursor.setPosition(fragment.position());
                    cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                    cursor.setCharFormat(format);
                }
            }
            document.setPageSize(QSizeF(writer.width(), writer.height()));
            const int pages = document.pageCount();
            for (int page = 0; page < pages; ++page) {
                if (!firstPage && !writer.newPage()) return fail("Could not lay out the EPUB pages.");
                firstPage = false;
                painter.save();
                painter.setClipRect(QRectF(0, 0, writer.width(), writer.height()));
                painter.translate(0, -page * writer.height());
                document.drawContents(&painter, QRectF(0, page * writer.height(), writer.width(), writer.height()));
                painter.restore();
            }
        }
    }
    return output;
}
