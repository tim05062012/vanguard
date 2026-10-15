#include "FPSMonitor.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace {

// Интервал усреднения FPS и публикации сигнала.
constexpr int kPublishIntervalMs = 500;

// Окно истории для перцентилей low и графика frame time.
constexpr double kHistorySeconds = 60.0;

// Разбор CSV-строки с поддержкой кавычек (имена процессов могут содержать
// запятые). Простого split(',') недостаточно.
QStringList splitCsvLine(const QString &line)
{
    QStringList fields;
    QString current;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    current.append(QLatin1Char('"'));
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                current.append(c);
            }
        } else if (c == QLatin1Char('"')) {
            inQuotes = true;
        } else if (c == QLatin1Char(',')) {
            fields.append(current);
            current.clear();
        } else {
            current.append(c);
        }
    }
    fields.append(current);
    return fields;
}

int indexOfColumn(const QStringList &header, const QString &name)
{
    for (int i = 0; i < header.size(); ++i) {
        if (header.at(i).trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return i;
    }
    return -1;
}

// Версия из имени вида PresentMon-2.6.0-x64.exe; -1 если версии нет.
qint64 presentMonVersion(const QString &fileName)
{
    static const QRegularExpression re(
        QStringLiteral("-(\\d+)\\.(\\d+)\\.(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = re.match(fileName);
    if (!match.hasMatch())
        return -1;
    return match.captured(1).toLongLong() * 1000000
         + match.captured(2).toLongLong() * 1000
         + match.captured(3).toLongLong();
}

// Лучший PresentMon*.exe в каталоге (максимальная версия; без версии — хуже).
QString bestPresentMonIn(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return QString();

    const QStringList exes =
        dir.entryList({QStringLiteral("PresentMon*.exe")}, QDir::Files);
    QString best;
    qint64 bestVersion = -2;
    for (const QString &exe : exes) {
        const qint64 version = presentMonVersion(exe);
        if (version > bestVersion) {
            bestVersion = version;
            best = dir.filePath(exe);
        }
    }
    return best;
}

} // namespace

FPSMonitor::FPSMonitor(QObject *parent)
    : QObject(parent)
{
    m_process = new QProcess(this);
    m_timer = new QTimer(this);
    m_timer->setInterval(kPublishIntervalMs);

    connect(m_timer, &QTimer::timeout, this, &FPSMonitor::publishSample);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &FPSMonitor::onReadyReadOutput);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &FPSMonitor::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &FPSMonitor::onProcessError);

#ifdef Q_OS_WIN
    // Не показывать всплывающее консольное окно PresentMon.
    m_process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) {
            args->flags |= 0x08000000; // CREATE_NO_WINDOW
        });
#endif
}

FPSMonitor::~FPSMonitor()
{
    if (m_running)
        stop();

    // Подстраховка: не оставляем висящий дочерний процесс.
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

int FPSMonitor::getCurrentFPS() const
{
    return m_currentFps;
}

bool FPSMonitor::isAvailable() const
{
    return !findPresentMon().isEmpty();
}

bool FPSMonitor::isRunning() const
{
    return m_running;
}

void FPSMonitor::setTargetProcess(const QString &imageName)
{
    m_targetProcess = imageName.trimmed();
}

QString FPSMonitor::targetProcess() const
{
    return m_targetProcess;
}

void FPSMonitor::setPresentMonPath(const QString &path)
{
    m_presentMonPath = path.trimmed();
}

QString FPSMonitor::presentMonPath() const
{
    return m_presentMonPath;
}

void FPSMonitor::setRestartAsAdmin(bool enabled)
{
    m_restartAsAdmin = enabled;
}

QString FPSMonitor::lastError() const
{
    return m_lastError;
}

double FPSMonitor::onePercentLow() const
{
    return m_onePercentLow;
}

double FPSMonitor::pointOnePercentLow() const
{
    return m_pointOnePercentLow;
}

QString FPSMonitor::findPresentMon(const QString &overridePath)
{
    // 1. Явный путь из настроек.
    if (!overridePath.isEmpty()) {
        const QFileInfo info(overridePath);
        if (info.exists() && info.isFile())
            return QDir::cleanPath(info.absoluteFilePath());
    }

    // 2. Каталоги вокруг exe: dev-режим (../tools из build/) и установленная
    //    версия (tools/ рядом с exe). Маска PresentMon*.exe — версионные имена
    //    вроде PresentMon-2.6.0-x64.exe.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList dirs = {
        appDir,
        QDir(appDir).filePath(QStringLiteral("tools")),
        QDir(appDir).filePath(QStringLiteral("../tools")),
        QDir(QDir::currentPath()).filePath(QStringLiteral("tools")),
        QDir::currentPath(),
    };
    for (const QString &dir : dirs) {
        const QString found = bestPresentMonIn(dir);
        if (!found.isEmpty())
            return QDir::cleanPath(found);
    }

    // 3. PATH — только классическое имя (маски в PATH не работают).
    const QString fromPath =
        QStandardPaths::findExecutable(QStringLiteral("PresentMon.exe"));
    if (!fromPath.isEmpty())
        return fromPath;

    // 4. Стандартные каталоги установки.
    const QStringList programDirs = {
        QStringLiteral("C:/Program Files/PresentMon"),
        QStringLiteral("C:/Program Files (x86)/PresentMon"),
    };
    for (const QString &dir : programDirs) {
        const QString found = bestPresentMonIn(dir);
        if (!found.isEmpty())
            return QDir::cleanPath(found);
    }

    return QString();
}

void FPSMonitor::resetCaptureState()
{
    m_outputBuffer.clear();
    m_headerFound = false;
    m_msBetweenPresentsIndex = -1;
    m_window.clear();
}

void FPSMonitor::start()
{
    if (m_running)
        return;

    emit availabilityChanged(isAvailable());

    if (!isAvailable()) {
        m_lastError = m_presentMonPath.isEmpty()
            ? tr("PresentMon не найден. Нужен PresentMon*.exe рядом с VANGUARD.exe, "
                 "в папке tools/, в PATH или в C:\\Program Files\\PresentMon\\. "
                 "Кнопка «Скачать PresentMon» в настройках откроет страницу "
                 "загрузки с GitHub.")
            : tr("PresentMon не найден по указанному пути: %1").arg(m_presentMonPath);
        m_currentFps = -1;
        emit fpsUpdated(-1);
        return;
    }

    QStringList args;
    // ВАЖНО: --output_stdout и --no_csv взаимоисключающи (проверено на
    // PresentMon 2.6.0: выводит warning и игнорирует --no_csv).
    args << QStringLiteral("--output_stdout")
         << QStringLiteral("--no_console_stats")
         << QStringLiteral("--session_name") << QStringLiteral("VANGUARD")
         << QStringLiteral("--stop_existing_session");
    if (!m_targetProcess.isEmpty())
        args << QStringLiteral("--process_name") << m_targetProcess;
    if (m_restartAsAdmin)
        args << QStringLiteral("--restart_as_admin");

    resetCaptureState();
    m_lastError.clear();
    m_history.clear();
    m_pending.clear();
    m_onePercentLow = -1;
    m_pointOnePercentLow = -1;
    m_elapsed.restart();

    m_process->setProgram(findPresentMon());
    m_process->setArguments(args);
    m_process->start();

    if (!m_process->waitForStarted(5000)) {
        m_lastError = tr("Не удалось запустить PresentMon: %1")
                          .arg(m_process->errorString());
        m_currentFps = -1;
        emit fpsUpdated(-1);
        return;
    }

    m_running = true;
    m_timer->start();
}

void FPSMonitor::stop()
{
    if (!m_running)
        return;

    m_running = false;
    m_timer->stop();

    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
            // Дожидаемся фактического завершения, иначе на деструкторе
            // будет предупреждение "QProcess: Destroyed while running".
            m_process->waitForFinished(1000);
        }
    }

    resetCaptureState();
    m_currentFps = -1;
    m_history.clear();
    m_pending.clear();
    m_onePercentLow = -1;
    m_pointOnePercentLow = -1;
    emit percentilesUpdated(-1, -1);
    emit fpsUpdated(-1);
    emit monitoringStopped();
}

void FPSMonitor::onReadyReadOutput()
{
    m_outputBuffer += QString::fromUtf8(m_process->readAllStandardOutput());

    int newlineIndex = m_outputBuffer.indexOf(QLatin1Char('\n'));
    while (newlineIndex >= 0) {
        QString line = m_outputBuffer.left(newlineIndex);
        m_outputBuffer.remove(0, newlineIndex + 1);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        handleLine(line);
        newlineIndex = m_outputBuffer.indexOf(QLatin1Char('\n'));
    }
}

void FPSMonitor::handleLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return;

    if (!m_headerFound) {
        // Ищем CSV-заголовок: PresentMon смешивает его с диагностическими
        // сообщениями в stdout, поэтому определяем по известным колонкам.
        if (!trimmed.contains(QLatin1String("Application"), Qt::CaseInsensitive))
            return;

        const QStringList header = splitCsvLine(trimmed);
        int index = indexOfColumn(header, QStringLiteral("MsBetweenPresents"));
        if (index < 0)
            index = indexOfColumn(header, QStringLiteral("FrameTime"));
        if (index < 0)
            return;

        m_msBetweenPresentsIndex = index;
        m_headerFound = true;
        return;
    }

    const QStringList fields = splitCsvLine(trimmed);
    if (m_msBetweenPresentsIndex < 0 || m_msBetweenPresentsIndex >= fields.size())
        return;

    bool ok = false;
    const double ms = fields.at(m_msBetweenPresentsIndex).trimmed().toDouble(&ok);
    if (!ok || ms <= 0.0)
        return;

    m_window.append(1000.0 / ms);

    // История кадров (перцентили low + батч для графика frame time).
    const double seconds =
        m_elapsed.isValid() ? m_elapsed.elapsed() / 1000.0 : 0.0;
    m_pending.append(qMakePair(seconds, ms));
    m_history.append(qMakePair(seconds, ms));
    while (!m_history.isEmpty()
           && m_history.first().first < seconds - kHistorySeconds)
        m_history.removeFirst();
}

void FPSMonitor::computePercentiles()
{
    if (m_history.isEmpty()) {
        m_onePercentLow = -1;
        m_pointOnePercentLow = -1;
        return;
    }

    // Методика low: сортируем frame times по убыванию (худшие первыми),
    // берём худшие 1% / 0.1%, FPS = 1000 / среднее по этой группе.
    QList<double> times;
    times.reserve(m_history.size());
    for (const auto &frame : m_history)
        times.append(frame.second);
    std::sort(times.begin(), times.end(), std::greater<double>());

    auto lowFps = [](const QList<double> &sorted, double fraction) {
        const int count =
            qMax(1, static_cast<int>(std::ceil(sorted.size() * fraction)));
        double sum = 0.0;
        for (int i = 0; i < count; ++i)
            sum += sorted.at(i);
        const double mean = sum / count;
        return mean > 0.0 ? 1000.0 / mean : -1.0;
    };

    m_onePercentLow = lowFps(times, 0.01);
    m_pointOnePercentLow = lowFps(times, 0.001);
}

void FPSMonitor::publishSample()
{
    if (m_window.isEmpty()) {
        m_currentFps = -1;
    } else {
        double sum = 0.0;
        for (double value : m_window)
            sum += value;
        m_currentFps = qRound(sum / static_cast<double>(m_window.size()));
    }
    m_window.clear();

    emit fpsUpdated(m_currentFps);

    computePercentiles();
    emit percentilesUpdated(m_onePercentLow, m_pointOnePercentLow);

    // Батч кадров графику — один сигнал вместо сотен update() по кадру.
    if (!m_pending.isEmpty()) {
        emit framesUpdated(m_pending);
        m_pending.clear();
    }
}

void FPSMonitor::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status)
    if (!m_running)
        return;

    // PresentMon завершился сам: либо закрылся целевой процесс,
    // либо ошибка трассировки (нет прав и т.п.). Последние строки
    // stdout/stderr подсказывают причину.
    const QString tail =
        (QString::fromUtf8(m_process->readAllStandardOutput())
         + QString::fromUtf8(m_process->readAllStandardError()))
            .right(2000);

    const bool permissionIssue =
        tail.contains(QLatin1String("administrator"), Qt::CaseInsensitive)
        || tail.contains(QLatin1String("permission"), Qt::CaseInsensitive)
        || tail.contains(QLatin1String("access is denied"), Qt::CaseInsensitive)
        || tail.contains(QLatin1String("Performance Log Users"),
                         Qt::CaseInsensitive);

    m_lastError = permissionIssue
        ? tr("PresentMon: нет прав на ETW-трассировку (код %1). Запустите VANGUARD "
             "от администратора или добавьте учётку в группу «Performance Log "
             "Users». Альтернатива — включите флаг «--restart_as_admin» "
             "в настройках PresentMon.").arg(exitCode)
        : tr("PresentMon неожиданно завершился (код %1). Проверьте членство "
             "в группе «Performance Log Users» или запуск от администратора.")
              .arg(exitCode);
    stop();
}

void FPSMonitor::onProcessError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return;

    m_lastError = tr("Не удалось запустить PresentMon: %1")
                      .arg(m_process->errorString());
    if (m_running) {
        m_running = false;
        m_timer->stop();
        m_currentFps = -1;
        emit fpsUpdated(-1);
        emit monitoringStopped();
    }
    emit availabilityChanged(false);
}
