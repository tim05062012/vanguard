#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include <QString>
#include <QStringList>
#include <QList>

// Настройки одного запуска оптимизации: какие категорий процессов закрывать,
// чистить ли RAM и переключать ли схему питания.
struct OptimizeOptions {
    bool closeBrowsers = true;
    bool closeMessengers = true;
    bool closeTorrents = false;
    bool closeHeavyweights = true;
    bool trimWorkingSet = true;
    bool setHighPerformancePlan = true;
};

struct OptimizeReport {
    int processesClosed = 0;
    qint64 memoryFreedBytes = 0;
    QStringList closedProcesses;

    // Схема питания: какая была до и что стало после.
    QString oldPowerPlanUuid;
    QString oldPowerPlanName;
    bool powerPlanChanged = false;

    // Сколько RAM освобождено через EmptyWorkingSet (в байтах).
    qint64 ramTrimmedBytes = 0;
};

// Процесс, закрытый во время оптимизации: имя образа + полный путь к exe
// (путь нужен, чтобы восстановить запуск через restoreAfterGame()).
struct ClosedProcess {
    QString imageName;
    QString exePath;
};

class Optimizer {
public:
    Optimizer();

    // Совместимость со старым API (true = закрывать браузеры).
    OptimizeReport optimizeBeforeGame(bool closeBrowsers = true);
    OptimizeReport optimizeBeforeGame(const OptimizeOptions &options);

    void restoreAfterGame();

    bool hasClosedProcesses() const;
    QList<ClosedProcess> closedProcesses() const;

    // Вызывается после запуска игры: поднимает приоритет процесса до
    // HIGH_PRIORITY_CLASS, дожидаясь его появления (polling ~timeoutMs).
    void raiseGamePriority(const QString &imageName, int timeoutMs = 30000);

    // Была ли реально сменена схема питания (для кнопки Restore).
    bool powerPlanChanged() const;

private:
    void rememberClosed(const QString &imageName, const QString &exePath);

    int closeTargets(const QStringList &images, OptimizeReport &report);
    qint64 trimWorkingSets();
    bool switchToHighPerformance(QString &oldUuid, QString &oldName);
    void restorePowerPlan();

    QList<ClosedProcess> m_closedProcesses;

    bool m_powerPlanChanged = false;
    QString m_oldPowerPlanUuid;
    QString m_oldPowerPlanName;
};

#endif // OPTIMIZER_H

