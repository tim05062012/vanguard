#include "ui/OptimizePage.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include "Optimizer.h"

OptimizePage::OptimizePage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 24);
    root->setSpacing(14);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("Оптимизация"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(title);
    header->addStretch(1);
    root->addLayout(header);

    auto *hint = new QLabel(
        tr("Оптимизация закрывает фоновые процессы, освобождает RAM, "
           "переключает схему питания и поднимает приоритет процесса игры."),
        this);
    hint->setObjectName(QStringLiteral("pageHint"));
    hint->setWordWrap(true);
    root->addWidget(hint);
    root->addSpacing(6);

    // --- Кнопки ---
    auto *buttons = new QHBoxLayout;
    optimizeBtn = new QPushButton(tr("Оптимизировать"), this);
    optimizeBtn->setObjectName(QStringLiteral("primaryBtn"));
    connect(optimizeBtn, &QPushButton::clicked,
            this, &OptimizePage::optimizeRequested);

    restoreBtn = new QPushButton(tr("Восстановить"), this);
    restoreBtn->setCursor(Qt::PointingHandCursor);
    connect(restoreBtn, &QPushButton::clicked,
            this, &OptimizePage::restoreRequested);

    buttons->addWidget(optimizeBtn);
    buttons->addWidget(restoreBtn);
    buttons->addStretch(1);
    root->addLayout(buttons);
    root->addSpacing(8);

    // --- Карточки статистики ---
    auto *stats = new QGridLayout;
    stats->setHorizontalSpacing(16);
    stats->setVerticalSpacing(10);

    auto addStat = [&](int column, const QString &caption, QLabel **value) {
        auto *panel = new QFrame(this);
        panel->setObjectName(QStringLiteral("panel"));
        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(6);

        *value = new QLabel(QStringLiteral("—"), panel);
        (*value)->setObjectName(QStringLiteral("reportStat"));
        (*value)->setAlignment(Qt::AlignLeft);
        layout->addWidget(*value);

        auto *captionLabel = new QLabel(caption, panel);
        captionLabel->setObjectName(QStringLiteral("reportStatCaption"));
        layout->addWidget(captionLabel);

        stats->addWidget(panel, 0, column);
        statBodies[column] = nullptr;
    };

    addStat(0, tr("ПРОЦЕССОВ ЗАКРЫТО"), &statProcesses);
    addStat(1, tr("RAM ОСВОБОЖДЕНО"), &statMemory);
    addStat(2, tr("СХЕМА ПИТАНИЯ"), &statPower);

    root->addLayout(stats);
    root->addSpacing(6);

    headline = new QLabel(tr("Отчёт появится здесь"), this);
    headline->setObjectName(QStringLiteral("reportHeadline"));
    root->addWidget(headline);

    auto *logLabel = new QLabel(tr("ПОДРОБНОСТИ"), this);
    logLabel->setObjectName(QStringLiteral("sectionTitle"));
    root->addWidget(logLabel);

    reportLog = new QTextEdit(this);
    reportLog->setReadOnly(true);
    reportLog->setPlaceholderText(tr("Здесь будет детальный отчёт: список "
                                     "процессов, схема питания, восстановление."));
    root->addWidget(reportLog, 1);
}

void OptimizePage::showReport(const OptimizeReport &report)
{
    const double freedMb =
        static_cast<double>(report.memoryFreedBytes) / (1024.0 * 1024.0);

    statProcesses->setText(QString::number(report.processesClosed));
    statMemory->setText(freedMb >= 1.0
                            ? QStringLiteral("%1 MB")
                                  .arg(freedMb, 0, 'f', 1)
                            : QStringLiteral("—"));

    if (!report.powerPlanChanged) {
        statPower->setText(tr("—"));
        headline->setText(report.processesClosed > 0
                              ? tr("Оптимизация завершена")
                              : tr("Нечего закрывать — всё уже чисто"));
    } else {
        statPower->setText(tr("HIGH"));
        headline->setText(
            tr("Оптимизация завершена · питание: %1 → High performance")
                .arg(report.oldPowerPlanUuid.isEmpty()
                         ? tr("прежняя схема")
                         : report.oldPowerPlanUuid));
    }

    QString body;
    body += tr("Закрыто процессов: %1\n").arg(report.processesClosed);
    body += tr("Освобождено памяти: %1 МБ\n").arg(freedMb, 0, 'f', 1);

    if (report.powerPlanChanged) {
        body += tr("Схема питания: %1 → High performance "
                   "(прежняя схема будет восстановлена кнопкой Restore)\n")
                    .arg(report.oldPowerPlanUuid);
    }

    if (!report.closedProcesses.isEmpty())
        body += tr("Закрытые процессы:\n  %1\n")
                    .arg(report.closedProcesses.join(QStringLiteral("\n  ")));

    body += tr("\nRestore вернёт процессы и прежнюю схему питания.");

    reportLog->setPlainText(body);
}
