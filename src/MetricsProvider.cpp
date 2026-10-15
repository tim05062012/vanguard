// _WIN32_WINNT нужен для GetIfTable2 / PdhAddEnglishCounterW (Vista+) в
// заголовках winapi; ставим до первого включения windows.h.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#endif

#include "MetricsProvider.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QLibrary>
#include <QProcess>
#include <QTimer>

#include <cstdlib>

#ifdef Q_OS_WIN
// ws2tcpip/winsock2 — ДО windows.h: MinGW даёт ADDRESS_FAMILY и _WS2IPDEF_
// только отсюда, а без _WS2IPDEF_ netioapi.h прячет MIB_IF_ROW2/GetIfTable2.
#include <ws2tcpip.h>
#include <windows.h>
// CallNtPowerInformation MinGW объявляет в powerbase.h (в MS SDK — powrprof.h).
#include <powerbase.h>
// При NTDDI_VERSION ≥ Vista iphlpapi.h подтягивает netioapi.h (GetIfTable2).
#include <iphlpapi.h>
#include <pdh.h>
#endif

namespace {

// Структуры NVML объявляем локально — заголовок nvml.h не тащим в проект.
struct NvmlUtilization {
    unsigned int gpu;
    unsigned int memory;
};

struct NvmlMemory {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

// Коды NVML: 0 = Success.
constexpr int kNvmlSuccess = 0;
// nvmlTemperatureSensors_t: 0 = GPU.
constexpr int kNvmlTempGpu = 0;
// nvmlClockId_t: 0 = Graphics, 1 = SM, 2 = Memory.
constexpr int kNvmlClockGraphics = 0;

#ifdef Q_OS_WIN
// PROCESSOR_POWER_INFORMATION MinGW не объявляет (есть только в MS SDK) —
// объявляем локально: layout из MSDN, 6×ULONG, отдаётся CallNtPowerInformation.
struct PpiInfo {
    ULONG number;
    ULONG maxMhz;
    ULONG currentMhz;
    ULONG mhzLimit;
    ULONG maxIdleState;
    ULONG currentIdleState;
};
static_assert(sizeof(PpiInfo) == 6 * sizeof(ULONG), "PpiInfo layout");
#endif

} // namespace

MetricsProvider::MetricsProvider(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MetricsProvider::poll);
}

MetricsProvider::~MetricsProvider()
{
    stop();
    shutdownNvml();
}

void MetricsProvider::start(int intervalMs)
{
    if (m_timer->isActive())
        return;

    if (!m_nvmlLib)
        initNvml();

    m_netPrimed = false;
    m_prevDiskRead = -1;
    m_timer->start(qMax(200, intervalMs));
}

void MetricsProvider::stop()
{
    if (m_timer->isActive())
        m_timer->stop();
    if (m_tempProcess && m_tempProcess->state() != QProcess::NotRunning) {
        m_tempProcess->kill();
        m_tempProcess->waitForFinished(1000);
    }
}

bool MetricsProvider::isRunning() const
{
    return m_timer->isActive();
}

void MetricsProvider::poll()
{
    pollCpuLoad();
    pollCpuClock();
    pollRam();
    pollGpu();
    pollNetwork();
    pollDisk();
    pollCpuTempAsync(); // WMI-градусы приходят асинхронно, раз в ~3 с

    emit metricsUpdated(m_last);
}

void MetricsProvider::initNvml()
{
#ifdef Q_OS_WIN
    // nvml.dll рядом с драйвером NVIDIA (System32). QLibrary ищет в стандартных
    // путях; если драйвера/карты нет — деградируем в «GPU: —», не роняем ничего.
    auto *lib = new QLibrary(QStringLiteral("nvml.dll"), this);
    if (!lib->load()) {
        delete lib;
        return;
    }

    nvmlInit_v2 = reinterpret_cast<int (*)()>(lib->resolve("nvmlInit_v2"));
    nvmlShutdown = reinterpret_cast<int (*)()>(lib->resolve("nvmlShutdown"));
    nvmlDeviceGetCount_v2 =
        reinterpret_cast<int (*)(unsigned *)>(lib->resolve("nvmlDeviceGetCount_v2"));
    nvmlDeviceGetHandleByIndex_v2 =
        reinterpret_cast<int (*)(unsigned, void **)>(
            lib->resolve("nvmlDeviceGetHandleByIndex_v2"));
    nvmlDeviceGetUtilizationRates =
        reinterpret_cast<int (*)(void *, void *)>(
            lib->resolve("nvmlDeviceGetUtilizationRates"));
    nvmlDeviceGetTemperature =
        reinterpret_cast<int (*)(void *, int, unsigned *)>(
            lib->resolve("nvmlDeviceGetTemperature"));
    nvmlDeviceGetMemoryInfo =
        reinterpret_cast<int (*)(void *, void *)>(
            lib->resolve("nvmlDeviceGetMemoryInfo"));
    nvmlDeviceGetClockInfo =
        reinterpret_cast<int (*)(void *, int, unsigned *)>(
            lib->resolve("nvmlDeviceGetClockInfo"));
    nvmlDeviceGetPowerUsage =
        reinterpret_cast<int (*)(void *, unsigned *)>(
            lib->resolve("nvmlDeviceGetPowerUsage"));
    nvmlDeviceGetName =
        reinterpret_cast<int (*)(void *, char *, unsigned)>(
            lib->resolve("nvmlDeviceGetName"));

    // Нужен минимальный набор; остальное может отсутствовать на старых драйверах.
    if (!nvmlInit_v2 || !nvmlDeviceGetCount_v2 || !nvmlDeviceGetHandleByIndex_v2
        || !nvmlDeviceGetUtilizationRates || !nvmlDeviceGetTemperature
        || !nvmlDeviceGetMemoryInfo) {
        delete lib;
        nvmlInit_v2 = nullptr;
        return;
    }

    m_nvmlLib = lib;
    if (nvmlInit_v2() != kNvmlSuccess) {
        shutdownNvml();
        return;
    }

    if (nvmlDeviceGetCount_v2(&m_nvmlDeviceCount) != kNvmlSuccess
        || m_nvmlDeviceCount == 0) {
        shutdownNvml();
        return;
    }

    // Берём первую NVIDIA GPU (NVML видит только свои устройства).
    if (nvmlDeviceGetHandleByIndex_v2(0, &m_nvmlDevice) != kNvmlSuccess) {
        shutdownNvml();
        return;
    }

    m_nvmlAvailable = true;
#endif
}

void MetricsProvider::shutdownNvml()
{
    if (!m_nvmlLib)
        return;

    if (m_nvmlAvailable && nvmlShutdown)
        nvmlShutdown();

    m_nvmlAvailable = false;
    m_nvmlDevice = nullptr;
    m_nvmlDeviceCount = 0;

    auto *lib = static_cast<QLibrary *>(m_nvmlLib);
    lib->unload();
    lib->deleteLater();
    m_nvmlLib = nullptr;
}

void MetricsProvider::pollCpuLoad()
{
#ifdef Q_OS_WIN
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user))
        return;

    auto toInt64 = [](const FILETIME &t) {
        return (static_cast<qint64>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    };
    const qint64 idleNow = toInt64(idle);
    const qint64 kernelNow = toInt64(kernel); // включает idle
    const qint64 userNow = toInt64(user);

    if (m_prevIdle >= 0) {
        const qint64 dIdle = idleNow - m_prevIdle;
        const qint64 dTotal =
            (kernelNow - m_prevKernel) + (userNow - m_prevUser);
        if (dTotal > 0) {
            const double busy = static_cast<double>(dTotal - dIdle);
            m_last.cpuLoad = qBound(0.0, busy * 100.0 / static_cast<double>(dTotal),
                                    100.0);
        }
    }

    m_prevIdle = idleNow;
    m_prevKernel = kernelNow;
    m_prevUser = userNow;
#endif
}

void MetricsProvider::pollCpuClock()
{
#ifdef Q_OS_WIN
    // PROCESSOR_POWER_INFORMATION * число ядер; CurrentMhz — текущая частота.
    const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (count == 0 || count > 512)
        return;

    const DWORD bytes = count * sizeof(PpiInfo);
    auto *info = static_cast<PpiInfo *>(std::malloc(bytes));
    if (!info)
        return;

    if (CallNtPowerInformation(ProcessorInformation, nullptr, 0, info, bytes)
        == 0) {
        double sum = 0.0;
        for (DWORD i = 0; i < count; ++i)
            sum += info[i].currentMhz;
        const double avg = sum / count;
        if (avg > 0)
            m_last.cpuClock = avg;
    }
    std::free(info);
#endif
}

void MetricsProvider::pollRam()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status))
        return;

    const double totalGb =
        static_cast<double>(status.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
    const double availGb =
        static_cast<double>(status.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);

    m_last.ramTotalGb = totalGb;
    m_last.ramUsedGb = totalGb - availGb;
    m_last.ramPercent = static_cast<double>(status.dwMemoryLoad);
#endif
}

void MetricsProvider::pollGpu()
{
    if (!m_nvmlAvailable)
        return;

    NvmlUtilization util{};
    if (nvmlDeviceGetUtilizationRates(m_nvmlDevice, &util) == kNvmlSuccess)
        m_last.gpuLoad = util.gpu;

    unsigned int temp = 0;
    if (nvmlDeviceGetTemperature(m_nvmlDevice, kNvmlTempGpu, &temp)
            == kNvmlSuccess && temp > 0)
        m_last.gpuTemp = temp;

    NvmlMemory memory{};
    if (nvmlDeviceGetMemoryInfo(m_nvmlDevice, &memory) == kNvmlSuccess) {
        m_last.vramTotalGb =
            static_cast<double>(memory.total) / (1024.0 * 1024.0 * 1024.0);
        m_last.vramUsedGb =
            static_cast<double>(memory.used) / (1024.0 * 1024.0 * 1024.0);
    }

    if (nvmlDeviceGetClockInfo) {
        unsigned int clock = 0;
        if (nvmlDeviceGetClockInfo(m_nvmlDevice, kNvmlClockGraphics, &clock)
                == kNvmlSuccess && clock > 0)
            m_last.gpuClock = clock;
    }

    if (nvmlDeviceGetPowerUsage) {
        unsigned int milliwatts = 0;
        if (nvmlDeviceGetPowerUsage(m_nvmlDevice, &milliwatts) == kNvmlSuccess)
            m_last.gpuPowerW = milliwatts / 1000.0;
    }
}

void MetricsProvider::pollNetwork()
{
#ifdef Q_OS_WIN
    // GetIfTable2: суммарные октеты по всем интерфейсам. Между тиками таймера
    // берём дельту и переводим байты/с в КБ/с. Первый тик — только baseline.
    MIB_IF_TABLE2 *table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table)
        return;

    quint64 inTotal = 0;
    quint64 outTotal = 0;
    constexpr DWORD kIfTypeLoopback = 24; // IF_TYPE_SOFTWARE_LOOPBACK
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2 &row = table->Table[i];
        if (row.Type == kIfTypeLoopback)
            continue;
        inTotal += row.InOctets;
        outTotal += row.OutOctets;
    }
    FreeMibTable(table);

    if (!m_netPrimed) {
        m_prevNetIn = inTotal;
        m_prevNetOut = outTotal;
        m_netPrimed = true;
        return;
    }

    const double secs = m_timer->interval() / 1000.0;
    if (secs > 0.0 && inTotal >= m_prevNetIn && outTotal >= m_prevNetOut) {
        m_last.netDownKbs =
            static_cast<double>(inTotal - m_prevNetIn) / secs / 1024.0;
        m_last.netUpKbs =
            static_cast<double>(outTotal - m_prevNetOut) / secs / 1024.0;
    }
    m_prevNetIn = inTotal;
    m_prevNetOut = outTotal;
#endif
}

void MetricsProvider::pollDisk()
{
#ifdef Q_OS_WIN
    // PDH-счётчики «Disk {Read,Write} Bytes/sec» уже отдают скорость; первый
    // PdhCollectQueryData задаёт baseline, значения читаем со второго тика.
    // Query живёт в статике — у PDH нет смысла открывать её на каждый poll.
    static PDH_HQUERY query = nullptr;
    static PDH_HCOUNTER readCounter = nullptr;
    static PDH_HCOUNTER writeCounter = nullptr;
    static bool initFailed = false;

    if (initFailed)
        return;

    if (!m_diskReady) {
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
            initFailed = true;
            return;
        }
        if (PdhAddEnglishCounterW(query,
                                  L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",
                                  0, &readCounter)
                    != ERROR_SUCCESS
            || PdhAddEnglishCounterW(
                   query, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec",
                   0, &writeCounter)
                   != ERROR_SUCCESS) {
            PdhCloseQuery(query);
            query = nullptr;
            initFailed = true;
            return;
        }
        PdhCollectQueryData(query);
        m_diskReady = true;
        return;
    }

    if (PdhCollectQueryData(query) != ERROR_SUCCESS)
        return;

    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(readCounter, PDH_FMT_DOUBLE, nullptr,
                                    &value)
            == ERROR_SUCCESS)
        m_last.diskReadMbs = value.doubleValue / (1024.0 * 1024.0);

    if (PdhGetFormattedCounterValue(writeCounter, PDH_FMT_DOUBLE, nullptr,
                                    &value)
            == ERROR_SUCCESS)
        m_last.diskWriteMbs = value.doubleValue / (1024.0 * 1024.0);
#endif
}

void MetricsProvider::pollCpuTempAsync()
{
#ifdef Q_OS_WIN
    // WMI-температура (MSAcpi_ThermalZoneTemperature, отдаёт децикельвины)
    // собирается отдельным процессом PowerShell раз в 3 с. Без прав
    // администратора WMI не отвечает — метрика остаётся -1, приложение живёт.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastTempRequestMs < 3000)
        return;
    if (m_tempProcess && m_tempProcess->state() != QProcess::NotRunning)
        return;
    m_lastTempRequestMs = now;

    if (!m_tempProcess) {
        m_tempProcess = new QProcess(this);
        connect(m_tempProcess, &QProcess::finished, this,
                [this](int code, QProcess::ExitStatus) {
                    if (code != 0)
                        return;
                    // Зон бывает несколько — берём первую строку ответа.
                    const QString out =
                        QString::fromLocal8Bit(m_tempProcess->readAllStandardOutput())
                            .trimmed()
                            .section(QLatin1Char('\n'), 0, 0);
                    bool ok = false;
                    const double deciKelvin = out.toDouble(&ok);
                    if (!ok)
                        return;
                    const double celsius = deciKelvin / 10.0 - 273.15;
                    if (celsius > 0.0 && celsius < 150.0)
                        m_last.cpuTemp = celsius;
                });
    }

    m_tempProcess->start(
        QStringLiteral("powershell.exe"),
        {QStringLiteral("-NoProfile"),
         QStringLiteral("-NonInteractive"),
         QStringLiteral("-Command"),
         QStringLiteral(
             "(Get-CimInstance -Namespace root/wmi "
             "-ClassName MSAcpi_ThermalZoneTemperature | "
             "Select-Object -First 1).CurrentTemperature")});
#endif
}

