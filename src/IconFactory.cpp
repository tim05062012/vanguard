#include "IconFactory.h"

#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

namespace IconFactory {

QIcon tinted(const QString &path, const QColor &color, const QSize &size)
{
    // Принимаем как "logo.svg", так и ":/icons/logo.svg".
    const QString full = path.startsWith(QLatin1Char(':'))
                             ? path
                             : QStringLiteral(":/icons/") + path;

    QSvgRenderer renderer(full);
    if (!renderer.isValid())
        return QIcon();

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    renderer.render(&painter);
    painter.end();

    // Перекраска: все непрозрачные пиксели получают целевой цвет,
    // альфа-канал исходной иконки сохраняется.
    QPainter tint(&image);
    tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tint.fillRect(image.rect(), color);
    tint.end();

    QIcon icon(QPixmap::fromImage(image));

    // Тот же pixmap в 1x — чтобы кнопки не размывались при обычном DPI.
    if (size != QSize(16, 16)) {
        QSvgRenderer renderer16(full);
        if (renderer16.isValid()) {
            QImage small(16, 16, QImage::Format_ARGB32_Premultiplied);
            small.fill(Qt::transparent);
            QPainter p(&small);
            renderer16.render(&p);
            p.end();
            QPainter t(&small);
            t.setCompositionMode(QPainter::CompositionMode_SourceIn);
            t.fillRect(small.rect(), color);
            t.end();
            icon.addPixmap(QPixmap::fromImage(small));
        }
    }

    return icon;
}

} // namespace IconFactory
