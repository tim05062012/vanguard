#ifndef FPSMONITOR_H
#define FPSMONITOR_H

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPair>
#include <QProcess>
#include <QString>

class QTimer;

// Мониторинг FPS через внешний инструмент Intel PresentMon.
// PresentMon пишет CSV в stdout; мы находим колонку MsBetweenPresents
// по заголовку и усредняем 1000/ms за интервал опроса.
//
// Требования к окружению (см. README PresentMon):
//  - PresentMon.exe доступен (рядом с exe, в tools/ или в PATH);
//  - пользователь входит в группу "Performance Log Users"
//    или приложение запущено от администратора.
class FPSMonitor : public QObject
{
    Q_OBJECT

public:
    explicit FPSMonitor(QObject *parent = nullptr);
    ~FPSMonitor() override;

    // -1 — данных нет (нет PresentMon, нет кадров или мониторинг выключен).
    int getCurrentFPS() const;
    bool isAvailable() const;

    bool isRunning() const;
    void start();
    void stop();

    // Имя exe целевого процесса, например "notepad.exe". Пусто — все процессы.
    void setTargetProcess(const QString &imageName);
    QString targetProcess() const;

    // Явный путь к PresentMon.exe (ключ настроек presentMonPath).
    void setPresentMonPath(const QString &path);
    QString presentMonPath() const;

    // Флаг --restart_as_admin: PresentMon сам перезапускается с UAC,
    // когда прав не хватает (ключ настроек presentMonRestartAsAdmin).
    void setRestartAsAdmin(bool enabled);

    QString lastError() const;

    // Перцентили за окно kHistorySeconds (60 с); -1 — данных нет.
    double onePercentLow() const;
    double pointOnePercentLow() const;

    // Поиск бинаря PresentMon: путь из настроек → каталог exe → tools/ →
    // PATH → Program Files. Распознаёт версионные имена (PresentMon-2.6.0-x64.exe)
    // и выбирает самую новую версию. static — используется SettingsDialog
    // для метки «Найден: …».
    static QString findPresentMon(const QString &overridePath = QString());

signals:
    void fpsUpdated(int fps);
    // Перцентили low пересчитаны (-1 при пустом окне/остановке).
    void percentilesUpdated(double onePercentLow, double pointOnePercentLow);
    // Батч кадров за интервал публикации: (секунды от старта, ms).
    void framesUpdated(const QList<QPair<double, double>> &frames);
    void availabilityChanged(bool available);
    void monitoringStopped();

private slots:
    void onReadyReadOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void publishSample();

private:
    void handleLine(const QString &line);
    void resetCaptureState();
    void computePercentiles();

    QProcess *m_process = nullptr;
    QTimer *m_timer = nullptr;

    QString m_presentMonPath;
    QString m_targetProcess;
    QString m_lastError;
    bool m_restartAsAdmin = false;

    QString m_outputBuffer;
    bool m_headerFound = false;
    int m_msBetweenPresentsIndex = -1;

    QList<double> m_window;
    int m_currentFps = -1;
    bool m_running = false;

    // --- История кадров за 60 с (перцентили + график) ---
    QElapsedTimer m_elapsed;                 // отсчёт от start()
    QList<QPair<double, double>> m_history;  // (секунды, ms) — окно 60 с
    QList<QPair<double, double>> m_pending;  // накоплено с прошлой публикации
    double m_onePercentLow = -1;
    double m_pointOnePercentLow = -1;
};

#endif // FPSMONITOR_H
