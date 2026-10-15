#ifndef METRICSPROVIDER_H
#define METRICSPROVIDER_H

#include <QObject>
#include <QString>

class QProcess;
class QTimer;

// Снимок системных метрик для оверлея и страницы Мониторинг.
// -1 — метрика недоступна (нет NVML, нет прав WMI и т.п.).
struct SystemMetrics {
    double cpuLoad = -1;     // %
    double cpuTemp = -1;     // °C (WMI, асинхронно)
    double cpuClock = -1;    // MHz (CallNtPowerInformation)
    double ramUsedGb = -1;
    double ramTotalGb = -1;
    double ramPercent = -1;
    double gpuLoad = -1;     // % (NVML)
    double gpuTemp = -1;     // °C (NVML)
    double vramUsedGb = -1;  // NVML
    double vramTotalGb = -1; // NVML
    double gpuClock = -1;    // MHz (NVML)
    double gpuPowerW = -1;   // NVML
    double netDownKbs = -1;
    double netUpKbs = -1;
    double diskReadMbs = -1;
    double diskWriteMbs = -1;
};

// Периодический сбор метрик (по умолчанию 500 мс):
//   CPU — GetSystemTimes + CallNtPowerInformation + WMI (temp, раз в 3 с);
//   RAM — GlobalMemoryStatusEx;
//   GPU — NVML через динамическую загрузку nvml.dll (без жёсткой линковки);
//   сеть — GetIfTable2 (дельты октетов);
//   диск — PDH \\PhysicalDisk(_Total)\\Disk {Read,Write} Bytes/sec.
class MetricsProvider : public QObject
{
    Q_OBJECT

public:
    explicit MetricsProvider(QObject *parent = nullptr);
    ~MetricsProvider() override;

    void start(int intervalMs = 500);
    void stop();
    bool isRunning() const;

    SystemMetrics lastMetrics() const { return m_last; }
    bool nvmlAvailable() const { return m_nvmlAvailable; }

signals:
    void metricsUpdated(const SystemMetrics &metrics);

private:
    void poll();
    void initNvml();
    void shutdownNvml();
    void pollCpuLoad();
    void pollCpuClock();
    void pollRam();
    void pollGpu();
    void pollNetwork();
    void pollDisk();
    void pollCpuTempAsync();

    QTimer *m_timer = nullptr;
    SystemMetrics m_last;

    // CPU load — дельты GetSystemTimes.
    qint64 m_prevIdle = -1;
    qint64 m_prevKernel = -1;
    qint64 m_prevUser = -1;

    // Сеть — дельты октетов.
    quint64 m_prevNetIn = 0;
    quint64 m_prevNetOut = 0;
    bool m_netPrimed = false;

    // Диск — дельты PDH.
    qint64 m_prevDiskRead = -1;
    qint64 m_prevDiskWrite = -1;
    bool m_diskReady = false;

    // WMI-температура CPU: раз в 3 с асинхронным PowerShell.
    QProcess *m_tempProcess = nullptr;
    qint64 m_lastTempRequestMs = 0;

    // NVML (динамическая загрузка nvml.dll).
    void *m_nvmlLib = nullptr;
    bool m_nvmlAvailable = false;
    unsigned m_nvmlDeviceCount = 0;
    void *m_nvmlDevice = nullptr;

    // Указатели на функции NVML (заполняются в initNvml).
    int (*nvmlInit_v2)() = nullptr;
    int (*nvmlShutdown)() = nullptr;
    int (*nvmlDeviceGetCount_v2)(unsigned *) = nullptr;
    int (*nvmlDeviceGetHandleByIndex_v2)(unsigned, void **) = nullptr;
    int (*nvmlDeviceGetUtilizationRates)(void *, void *) = nullptr;
    int (*nvmlDeviceGetTemperature)(void *, int, unsigned *) = nullptr;
    int (*nvmlDeviceGetMemoryInfo)(void *, void *) = nullptr;
    int (*nvmlDeviceGetClockInfo)(void *, int, unsigned *) = nullptr;
    int (*nvmlDeviceGetPowerUsage)(void *, unsigned *) = nullptr;
    int (*nvmlDeviceGetName)(void *, char *, unsigned) = nullptr;
};

#endif // METRICSPROVIDER_H