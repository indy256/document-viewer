#pragma once
#include <QImage>
#include <QRectF>
#include <QString>
#include <QVector>
#include <memory>
#include "textpage.h"

class DjvuDocument {
public:
    DjvuDocument();
    ~DjvuDocument();
    bool open(const QString &path, QString *error);
    QVector<QSizeF> sizes() const;
    QImage render(int page, const QSize &fullSize, const QRect &tile);
    QVector<QVector<QRectF>> search(int page, const QString &query);
    TextPage textPage(int page);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
