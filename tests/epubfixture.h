#pragma once
#include <QtCore/private/qzipwriter_p.h>
#include <QBuffer>
#include <QImage>
inline void makeBook(const QString &path, bool broken, const QByteArray &version) {
    QZipWriter zip(path);
    zip.setCompressionPolicy(QZipWriter::NeverCompress);
    zip.addFile("mimetype", "application/epub+zip");
    zip.setCompressionPolicy(QZipWriter::AlwaysCompress);
    zip.addFile("META-INF/container.xml", QByteArray(R"(<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>)"));
    zip.addFile("OPS/book.opf", QByteArray("<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"") + version + R"("><metadata><title>Sample book</title></metadata><manifest><item id="b" href="Text/second.xhtml" media-type="application/xhtml+xml"/><item id="a" href="Text/first.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="a"/><itemref idref="b"/></spine></package>)");
    zip.addFile("OPS/Text/first.xhtml", QByteArray(R"(<html xmlns="http://www.w3.org/1999/xhtml"><head><link rel="stylesheet" href="../style.css"/></head><body><h1>Opening chapter</h1><p>Needle in the first chapter.</p><img src="../Images/pic%20one.png" width="100" height="60"/></body></html>)"));
    if (!broken) zip.addFile("OPS/Text/second.xhtml", QByteArray(R"(<html xmlns="http://www.w3.org/1999/xhtml"><body><h1>Closing chapter</h1><p>Another needle in the second chapter.</p></body></html>)"));
    zip.addFile("OPS/style.css", "p { color: #0000ff; }");
    QImage image(100, 60, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    zip.addFile("OPS/Images/pic one.png", png);
    zip.close();
}
