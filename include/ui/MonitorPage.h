#ifndef MONITORPAGE_H
#define MONITORPAGE_H

#include <QWidget>

class FrameTimeGraph;
class QLabel;
class QLineEdit;
class QPushButton;

// Страница мониторинга: крупный FPS (72pt mono), 1% low / 0.1% low,
// график frame time, блоки метрик CPU/GPU/RAM.
class MonitorPage : public QWidget
{
    Q_OBJECT

public:
    explicit MonitorPage(QWidget *parent = nullptr);

    // Обновление основного значения FPS (-1 — данных нет).
    void setFps(int fps);

    // Перцентили, если FPSMonitor их считает (пока опционально).
    void setLowStats(double onePercent, double pointOnePercent);

    // Метрики системы — вызывается из MainWindow, тянет из MetricsProvider.
    void setSystemMetrics(const QString &cpuLoad, const QString &cpuTemp,
                          const QString &cpuClock, const QString &ramUsed,
                          const QString &gpuLoad, const QString &gpuTemp,
                          const QString &vramUsed);

    void setTargetProcess(const QString &imageName);
    QString targetProcess() const;

    void setStatusText(const QString &text);
    void setMonitoringActive(bool active);

    FrameTimeGraph *frameTimeGraph() const { return graph; }

    // Кнопки статуса переключаются извне (MainWindow).
    QPushButton *toggleButton() const { return toggleBtn; }

signals:
    void toggleMonitoringRequested();
    void targetChanged(const QString &imageName);

private:
    QLabel *makeMetric(const QString &caption, const QString &value);

    QLabel *fpsLabel = nullptr;
    QLabel *fpsCaptionLabel = nullptr;
    QLabel *lowLabel = nullptr;
    QLabel *statusLabel = nullptr;
    QLineEdit *targetEdit = nullptr;
    QPushButton *toggleBtn = nullptr;
    FrameTimeGraph *graph = nullptr;

    QLabel *cpuValue = nullptr;
    QLabel *cpuTempValue = nullptr;
    QLabel *cpuClockValue = nullptr;
    QLabel *ramValue = nullptr;
    QLabel *gpuValue = nullptr;
    QLabel *gpuTempValue = nullptr;
    QLabel *vramValue = nullptr;
};

#endif // MONITORPAGE_H
