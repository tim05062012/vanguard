#include "ui/FrameTimeGraph.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

FrameTimeGraph::FrameTimeGraph(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("graphFrame"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumHeight(160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void FrameTimeGraph::addSample(double seconds, double frameTimeMs)
{
    m_samples.append(qMakePair(seconds, frameTimeMs));

    // Отбрасываем всё, что вышло за окно в 60 секунд.
    const double cutoff = seconds - m_windowSeconds;
    while (!m_samples.isEmpty() && m_samples.first().first < cutoff)
        m_samples.removeFirst();

    update();
}

void FrameTimeGraph::addSamples(const QList<QPair<double, double>> &samples)
{
    if (samples.isEmpty())
        return;

    m_samples += samples;

    const double cutoff = samples.last().first - m_windowSeconds;
    while (!m_samples.isEmpty() && m_samples.first().first < cutoff)
        m_samples.removeFirst();

    update();
}

void FrameTimeGraph::setWindowSeconds(double seconds)
{
    m_windowSeconds = qMax(5.0, seconds);
    update();
}

void FrameTimeGraph::clear()
{
    m_samples.clear();
    update();
}

void FrameTimeGraph::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF area = QRectF(rect()).adjusted(10, 10, -10, -18);

    // Сетка: 4 горизонтальных + 6 вертикальных линий.
    painter.setPen(QPen(QColor(QStringLiteral("#23232E")), 1));
    for (int i = 1; i <= 4; ++i) {
        const qreal y = area.top() + area.height() * i / 5.0;
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }
    for (int i = 1; i <= 6; ++i) {
        const qreal x = area.left() + area.width() * i / 7.0;
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }

    if (m_samples.size() < 2) {
        painter.setPen(QColor(QStringLiteral("#4A4A58")));
        painter.drawText(rect().adjusted(0, 0, 0, -8),
                         Qt::AlignCenter,
                         tr("Ожидание данных frame time…"));
        return;
    }

    const double minTime = m_samples.first().first;
    const double maxTime = m_samples.last().first;
    const double span = qMax(0.001, maxTime - minTime);

    // Ось Y: от 0 до максимума + 20 %, минимум 16.7 мс (60 FPS).
    double maxValue = 0.0;
    for (const auto &sample : m_samples)
        maxValue = qMax(maxValue, sample.second);
    maxValue = qMax(maxValue * 1.2, 16.7 * 1.2);

    auto toPoint = [&](const QPair<double, double> &sample) {
        const qreal x = area.left()
                        + area.width() * (sample.first - minTime) / span;
        // Инвертируем: меньший frame time — ниже (лучше).
        const qreal y = area.bottom()
                        - area.height() * (sample.second / maxValue);
        return QPointF(x, y);
    };

    QPainterPath line;
    QPainterPath fill;
    bool first = true;
    for (const auto &sample : m_samples) {
        const QPointF p = toPoint(sample);
        if (first) {
            line.moveTo(p);
            fill.moveTo(p);
            first = false;
        } else {
            line.lineTo(p);
            fill.lineTo(p);
        }
    }

    fill.lineTo(area.right(), area.bottom());
    fill.lineTo(area.left(), area.bottom());
    fill.closeSubpath();

    // Градиентная заливка под кривой.
    QLinearGradient gradient(area.topLeft(), area.bottomLeft());
    gradient.setColorAt(0.0, QColor(0, 229, 255, 70));
    gradient.setColorAt(1.0, QColor(0, 229, 255, 0));
    painter.fillPath(fill, gradient);

    painter.setPen(QPen(QColor(QStringLiteral("#00E5FF")), 2));
    painter.drawPath(line);

    // Метка горизонта 16.7 мс.
    const qreal targetY = area.bottom()
                          - area.height() * (16.7 / maxValue);
    if (targetY >= area.top()) {
        painter.setPen(QPen(QColor(255, 140, 60, 160), 1, Qt::DashLine));
        painter.drawLine(QPointF(area.left(), targetY),
                         QPointF(area.right(), targetY));
        painter.setPen(QColor(255, 140, 60, 200));
        painter.drawText(QRectF(area.left() + 4, targetY - 16,
                                area.width() - 8, 14),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("16.7 ms"));
    }

    // Подписи осей (мс).
    painter.setPen(QColor(QStringLiteral("#5C5C6B")));
    painter.drawText(QRectF(area.left(), height() - 16, area.width(), 14),
                     Qt::AlignLeft,
                     tr("−%1 с").arg(qRound(span)));
    painter.drawText(QRectF(area.left(), height() - 16, area.width(), 14),
                     Qt::AlignRight,
                     QStringLiteral("0 с"));
}
