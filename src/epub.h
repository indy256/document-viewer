#pragma once
#include <QByteArray>
#include <QString>

// Lay out an EPUB's reading-order content as an in-memory PDF for the common viewer.
QByteArray renderEpub(const QString &path, QString *error);
