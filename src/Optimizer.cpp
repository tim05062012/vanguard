#include "Optimizer.h"

#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#endif

Optimizer::Optimizer() = default;

namespace {

// Запуск консольной утилиты без окна консоли и возврат stdout.
QString runTool(const QString &program, const QStringList &args)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(args);

#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) {
            args->flags |= 0x08000000; // CREATE_NO_WINDOW
        });
#endif

    process.start();
    if (!process.waitForStarted(4000))
        return QString();
    if (!process.waitForFinished(8000)) {
        process.kill();
        process.waitForFinished(1000);
        return QString();
    }

    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
}

qint64 processMemoryBytes(const QString &imageName)
{
    QProcess ps;
    ps.start(QStringLiteral("tasklist"),
             {QStringLiteral("/FI"), QStringLiteral("IMAGENAME eq ") + imageName,
              QStringLiteral("/FO"), QStringLiteral("CSV"), QStringLiteral("/NH")});
    if (!ps.waitForFinished(5000))
        return 0;

    const QString out = QString::fromLocal8Bit(ps.readAllStandardOutput());
    if (out.contains(QStringLiteral("No tasks"), Qt::CaseInsensitive)
        || out.trimmed().isEmpty())
        return 0;

    qint64 total = 0;
    static const QRegularExpression re(
        QStringLiteral("\"([^\"]+)\",\"(\\d+)\",\"[^\"]*\",\"[^\"]*\",\"([\\d,]+) K\""));
    auto it = re.globalMatch(out);
    while (it.hasNext()) {
        const auto m = it.next();
        QString mem = m.captured(3);
        mem.remove(QLatin1Char(','));
        total += mem.toLongLong() * 1024;
    }
    return total;
}

// Полный путь к exe уже запущенного процесса. Windows PowerShell надёжнее
// устаревшего wmic и присутствует на всех поддерживаемых системах.
QString processExecutablePath(const QString &imageName)
{
    QString processName = imageName;
    if (processName.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        processName.chop(4);

    const QString script = QStringLiteral(
        "(Get-Process -Name '%1' -ErrorAction SilentlyContinue"
        " | Select-Object -First 1 -ExpandProperty Path)");

    QProcess ps;
    ps.start(QStringLiteral("powershell"),
             {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
              QStringLiteral("-Command"), script.arg(processName)});
    if (!ps.waitForFinished(5000))
        return QString();

    const QString out = QString::fromLocal8Bit(ps.readAllStandardOutput()).trimmed();
    if (out.isEmpty() || out.contains(QLatin1Char('\n'))
        || out.contains(QLatin1Char('\r')))
        return QString(); // пусто или несколько строк — результат недостоверен

    return out;
}

// Категории целей (ТЗ v0.2).
QStringList browserTargets()
{
    return {QStringLiteral("chrome.exe"),  QStringLiteral("msedge.exe"),
            QStringLiteral("firefox.exe"), QStringLiteral("brave.exe"),
            QStringLiteral("opera.exe")};
}

QStringList messengerTargets()
{
    return {QStringLiteral("Discord.exe"), QStringLiteral("Telegram.exe"),
            QStringLiteral("slack.exe"),   QStringLiteral("WhatsApp.exe"),
            QStringLiteral("Zoom.exe"),    QStringLiteral("Skype.exe")};
}

QStringList torrentTargets()
{
    return {QStringLiteral("qbittorrent.exe"), QStringLiteral("utorrent.exe"),
            QStringLiteral("transmission.exe")};
}

QStringList heavyTargets()
{
    return {QStringLiteral("Dropbox.exe"),  QStringLiteral("OneDrive.exe"),
            QStringLiteral("Teams.exe"),    QStringLiteral("ms-teams.exe"),
            QStringLiteral("steamwebhelper.exe")};
}

// Процессы, которым нельзя уменьшать working set иначе система деградирует
// (и которые нельзя закрывать — античиты и служебные компоненты).
bool isProtectedProcess(const QString &imageName)
{
    static const QStringList protectedList = {
        QStringLiteral("system"),            QStringLiteral("registry"),
        QStringLiteral("memory compression"), QStringLiteral("secure system"),
        QStringLiteral("idle"),              QStringLiteral("smss.exe"),
        QStringLiteral("csrss.exe"),         QStringLiteral("wininit.exe"),
        QStringLiteral("services.exe"),      QStringLiteral("lsass.exe"),
        QStringLiteral("svchost.exe"),       QStringLiteral("fontdrvhost.exe"),
        QStringLiteral("dwm.exe"),           QStringLiteral("explorer.exe"),
        QStringLiteral("vgtray.exe"),        QStringLiteral("vgc.exe"),
        QStringLiteral("vanguard tray.exe"),
    };

    const QString lower = imageName.toLower();
    for (const QString &p : protectedList) {
        if (lower == p)
            return true;
    }
    return false;
}

} // namespace

int Optimizer::closeTargets(const QStringList &images, OptimizeReport &report)
{
    int closed = 0;

    for (const QString &image : images) {
        const qint64 before = processMemoryBytes(image);
        if (before <= 0)
            continue; // процесс не запущен — не тратим вызов PowerShell

        const QString exePath = processExecutablePath(image);

        const int code = QProcess::execute(QStringLiteral("taskkill"),
                                           {QStringLiteral("/F"),
                                            QStringLiteral("/IM"), image});
        if (code == 0) {
            ++closed;
            report.processesClosed++;
            report.memoryFreedBytes += before;
            report.closedProcesses.append(image);
            rememberClosed(image, exePath);
        }
    }

    return closed;
}

qint64 Optimizer::trimWorkingSets()
{
#ifdef Q_OS_WIN
    // Перед чисткой снимаем общий объём используемой RAM.
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status))
        return 0;

    const qint64 beforeUsed = static_cast<qint64>(status.ullTotalPhys)
                              - static_cast<qint64>(status.ullAvailPhys);

    // Перебираем все процессы через Toolhelp-снапшот.
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    int trimmed = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const QString image = QString::fromWCharArray(entry.szExeFile);
            if (image.isEmpty() || isProtectedProcess(image))
                continue;

            HANDLE process = OpenProcess(
                PROCESS_QUERY_INFORMATION | PROCESS_SET_QUOTA, FALSE,
                entry.th32ProcessID);
            if (!process)
                continue; // нет прав на системные/чужие процессы — пропускаем

            // SetProcessWorkingSetSize(h, -1, -1) выгружает рабочий набор в
            // минимально возможный — эквивалент EmptyWorkingSet.
            SetProcessWorkingSetSize(process, (SIZE_T)-1, (SIZE_T)-1);
            CloseHandle(process);
            ++trimmed;
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    if (trimmed == 0)
        return 0;

    // Даём системе несколько миллисекунд на отдачу страниц.
    Sleep(150);

    MEMORYSTATUSEX after{};
    after.dwLength = sizeof(after);
    if (!GlobalMemoryStatusEx(&after))
        return 0;

    const qint64 afterUsed = static_cast<qint64>(after.ullTotalPhys)
                             - static_cast<qint64>(after.ullAvailPhys);
    const qint64 freed = beforeUsed - afterUsed;
    return freed > 0 ? freed : 0;
#else
    return 0;
#endif
}


bool Optimizer::switchToHighPerformance(QString &oldUuid, QString &oldName)
{
    // powercfg /getactivescheme -> "Scheme GUID: <uuid>  (Имя схемы)"
    const QString current = runTool(QStringLiteral("powercfg"),
                                    {QStringLiteral("/getactivescheme")});
    if (current.isEmpty())
        return false;

    static const QRegularExpression guidRe(
        QStringLiteral("([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-"
                       "[0-9a-fA-F]{4}-[0-9a-fA-F]{12})"));
    const auto guidMatch = guidRe.match(current);
    if (guidMatch.hasMatch())
        oldUuid = guidMatch.captured(1);

    static const QRegularExpression nameRe(QStringLiteral("\\(([^\\)]+)\\)"));
    const auto nameMatch = nameRe.match(current);
    if (nameMatch.hasMatch())
        oldName = nameMatch.captured(1);

    if (oldUuid.isEmpty())
        return false;

    // SCHEME_MIN — системный alias схемы "High performance".
    runTool(QStringLiteral("powercfg"),
            {QStringLiteral("/setactive"), QStringLiteral("SCHEME_MIN")});

    // Проверяем, что схема действительно сменилась.
    const QString after = runTool(QStringLiteral("powercfg"),
                                  {QStringLiteral("/getactivescheme")});
    if (after.isEmpty())
        return false;

    return after.contains(QStringLiteral("high"), Qt::CaseInsensitive)
           || (!after.contains(oldUuid, Qt::CaseInsensitive));
}

void Optimizer::restorePowerPlan()
{
    if (m_oldPowerPlanUuid.isEmpty())
        return;

    runTool(QStringLiteral("powercfg"),
            {QStringLiteral("/setactive"), m_oldPowerPlanUuid});
}

bool Optimizer::powerPlanChanged() const
{
    return m_powerPlanChanged;
}


OptimizeReport Optimizer::optimizeBeforeGame(bool closeBrowsers)
{
    OptimizeOptions options;
    options.closeBrowsers = closeBrowsers;
    options.closeMessengers = true;
    options.closeTorrents = false;
    options.closeHeavyweights = true;
    options.trimWorkingSet = false;       // старое поведение — только закрытие
    options.setHighPerformancePlan = false;
    return optimizeBeforeGame(options);
}

OptimizeReport Optimizer::optimizeBeforeGame(const OptimizeOptions &options)
{
    OptimizeReport report;

    // 1. Закрытие процессов по категориям.
    if (options.closeBrowsers)
        closeTargets(browserTargets(), report);
    if (options.closeMessengers)
        closeTargets(messengerTargets(), report);
    if (options.closeTorrents)
        closeTargets(torrentTargets(), report);
    if (options.closeHeavyweights)
        closeTargets(heavyTargets(), report);

    // 2. Очистка RAM через SetProcessWorkingSetSize для процессов, кроме
    //    системных и защищённых (античиты / служебные).
    if (options.trimWorkingSet) {
        const qint64 freed = trimWorkingSets();
        report.ramTrimmedBytes = freed;
        report.memoryFreedBytes += freed;
    }

    // 3. Переключение схемы питания на High performance.
    if (options.setHighPerformancePlan) {
        QString oldUuid;
        QString oldName;
        if (switchToHighPerformance(oldUuid, oldName)) {
            m_oldPowerPlanUuid = oldUuid;
            m_oldPowerPlanName = oldName;
            m_powerPlanChanged = true;

            report.oldPowerPlanUuid = oldUuid;
            report.oldPowerPlanName = oldName;
            report.powerPlanChanged = true;
        }
    }

    return report;
}


void Optimizer::restoreAfterGame()
{
    const QList<ClosedProcess> closed = m_closedProcesses;
    m_closedProcesses.clear();

    for (const ClosedProcess &process : closed) {
        // Предпочитаем полный путь; если его нет — пробуем имя через PATH.
        const QString target = process.exePath.isEmpty() ? process.imageName
                                                         : process.exePath;
        if (target.isEmpty())
            continue;
        QProcess::startDetached(target, {});
    }

    // Возврат схемы питания, если мы её меняли.
    if (m_powerPlanChanged) {
        restorePowerPlan();
        m_powerPlanChanged = false;
        m_oldPowerPlanUuid.clear();
        m_oldPowerPlanName.clear();
    }
}


void Optimizer::raiseGamePriority(const QString &imageName, int timeoutMs)
{
#ifdef Q_OS_WIN
    if (imageName.isEmpty())
        return;

    QString processName = imageName;
    if (processName.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        processName.chop(4);

    // Ждём появления процесса (игра может подниматься несколько секунд).
    const int stepMs = 250;
    int waited = 0;
    for (;;) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            bool found = false;
            DWORD pid = 0;

            if (Process32FirstW(snapshot, &entry)) {
                do {
                    const QString current =
                        QString::fromWCharArray(entry.szExeFile);
                    if (current.compare(processName, Qt::CaseInsensitive) == 0) {
                        pid = entry.th32ProcessID;
                        found = true;
                        break;
                    }
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);

            if (found && pid != 0) {
                HANDLE process = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
                if (process) {
                    SetPriorityClass(process, HIGH_PRIORITY_CLASS);
                    CloseHandle(process);
                }
                return;
            }
        }

        if (waited >= timeoutMs)
            return;
        QThread::msleep(stepMs);
        waited += stepMs;
    }
#else
    Q_UNUSED(imageName)
    Q_UNUSED(timeoutMs)
#endif
}


bool Optimizer::hasClosedProcesses() const
{
    return !m_closedProcesses.isEmpty();
}

QList<ClosedProcess> Optimizer::closedProcesses() const
{
    return m_closedProcesses;
}

void Optimizer::rememberClosed(const QString &imageName, const QString &exePath)
{
    for (ClosedProcess &closed : m_closedProcesses) {
        if (closed.imageName.compare(imageName, Qt::CaseInsensitive) == 0) {
            // Не затираем уже известный рабочий путь пустым значением.
            if (!exePath.isEmpty())
                closed.exePath = exePath;
            return;
        }
    }
    m_closedProcesses.append({imageName, exePath});
}

