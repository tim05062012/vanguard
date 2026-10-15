#ifndef ICONFACTORY_H
#define ICONFACTORY_H

#include <QColor>
#include <QIcon>
#include <QString>
#include <QSize>

// Загрузка SVG из Qt-ресурсов (: /icons/...) с перекраской в нужный цвет.
// SVG иконок в проекте однотонные, поэтому достаточно заменить альфу-канал
// исходной картинки — это быстрее, чем рендерить SVG заново под каждый цвет.
namespace IconFactory {

// Умеет принимать и плоские пути (: /icons/play.svg), и относительные от
// префикса /icons (logo.svg) — так вызовы получается короче.
QIcon tinted(const QString &path, const QColor &color, const QSize &size = QSize(20, 20));

} // namespace IconFactory

#endif // ICONFACTORY_H
