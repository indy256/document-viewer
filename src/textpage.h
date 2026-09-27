#pragma once
#include <QRectF>
#include <QString>
#include <QVector>
#include <QUrl>

struct TextSpan {
    qsizetype start;
    qsizetype length;
    QRectF box; // Normalized page coordinates, with a top-left origin.
};

struct TextLink {
    QUrl url;
    QVector<QRectF> boxes;
};

struct TextPage {
    QString text;
    QVector<TextSpan> spans; // PDF characters or DjVu OCR words, in reading order.
    QVector<TextLink> links;
};
