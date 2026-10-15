#ifndef FRAMETIMEGRAPH_H
#define FRAMETIMEGRAPH_H

#include <QList>
#include <QPair>
#include <QWidget>

// График frame time: кастомный paintEvent вместо QChart (без лишней
// зависимости) — сетка, заливка под кривой, плавный сдвиг точек.
class FrameTimeGraph : public QWidget
{
    Q_OBJECT

public:
    explicit FrameTimeGraph(QWidget *parent = nullptr);

    // Точка: время (сек от старта) и frame time в мс.
    void addSample(double seconds, double frameTimeMs);

    // Батч точек за интервал публикации (один update вместо сотен).
    void addSamples(const QList<QPair<double, double>> &samples);

    // Ограничение по времени окна (по умолчанию 60 сек — см. ТЗ).
    void setWindowSeconds(double seconds);

    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QList<QPair<double, double>> m_samples;
    double m_windowSeconds = 60.0;
};

#endif // FRAMETIMEGRAPH_H
