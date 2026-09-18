#pragma once
#include <QByteArray>
#include <QString>
#include <QHash>
#include <QPointF>
#include <QUrl>

struct EpubDestination {
    int page;
    QPointF position; // Points from the top-left of the rendered PDF page.
};
using EpubDestinations = QHash<QUrl, EpubDestination>;

// Lay out an EPUB's reading-order content as an in-memory PDF for the common viewer.
QByteArray renderEpub(const QString &path, QString *error, EpubDestinations *destinations = nullptr);
