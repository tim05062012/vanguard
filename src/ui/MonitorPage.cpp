#include "ui/MonitorPage.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include "ui/FrameTimeGraph.h"

namespace {

QLabel *makeCaption(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("metricLabel"));
    return label;
}

} // namespace

MonitorPage::MonitorPage(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 24);
    root->setSpacing(14);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("Мониторинг"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    header->addWidget(title);
    header->addStretch(1);
    root->addLayout(header);

    auto *hint = new QLabel(
        tr("FPS считается через Intel PresentMon. Оверлей включается "
           "глобальным хоткеем (по умолчанию Ctrl+Shift+F, "
           "меняется в настройках)."),
        this);
    hint->setObjectName(QStringLiteral("pageHint"));
    root->addWidget(hint);
    root->addSpacing(8);

    // --- Центральный блок: FPS ---
    auto *fpsPanel = new QFrame(this);
    fpsPanel->setObjectName(QStringLiteral("panel"));
    auto *panelLayout = new QVBoxLayout(fpsPanel);
    panelLayout->setContentsMargins(24, 20, 24, 20);
    panelLayout->setSpacing(2);

    fpsCaptionLabel = new QLabel(tr("КАДРЫ В СЕКУНДУ"), fpsPanel);
    fpsCaptionLabel->setObjectName(QStringLiteral("fpsCaption"));
    fpsCaptionLabel->setAlignment(Qt::AlignCenter);
    panelLayout->addWidget(fpsCaptionLabel);

    fpsLabel = new QLabel(QStringLiteral("—"), fpsPanel);
    fpsLabel->setObjectName(QStringLiteral("fpsValue"));
    fpsLabel->setAlignment(Qt::AlignCenter);
    panelLayout->addWidget(fpsLabel);

    lowLabel = new QLabel(tr("1% low —  ·  0.1% low —"), fpsPanel);
    lowLabel->setObjectName(QStringLiteral("lowStat"));
    lowLabel->setAlignment(Qt::AlignCenter);
    panelLayout->addWidget(lowLabel);

    root->addWidget(fpsPanel);

    // --- График frame time ---
    auto *graphHeader = new QHBoxLayout;
    auto *graphCaption = new QLabel(tr("FRAME TIME · 60 СЕК"), this);
    graphCaption->setObjectName(QStringLiteral("graphCaption"));
    graphHeader->addWidget(graphCaption);
    graphHeader->addStretch(1);
    root->addLayout(graphHeader);

    graph = new FrameTimeGraph(this);
    graph->setMinimumHeight(170);
    root->addWidget(graph);

    // --- Метрики системы ---
    // Пары «подпись + значение» в отдельных колонках (раньше все складывались
    // в одну ячейку (row,0)/(row,1) и визуально налагались друг на друга).
    auto *metricsGrid = new QGridLayout;
    metricsGrid->setHorizontalSpacing(24);
    metricsGrid->setVerticalSpacing(12);

    auto addCell = [&](int row, int col, const QString &caption,
                       QLabel **value) {
        auto *captionLabel = makeCaption(caption);
        metricsGrid->addWidget(captionLabel, row, col * 2);
        *value = new QLabel(QStringLiteral("—"), this);
        (*value)->setObjectName(QStringLiteral("metricValue"));
        metricsGrid->addWidget(*value, row, col * 2 + 1);
        metricsGrid->setColumnStretch(col * 2 + 1, 1);
    };

    addCell(0, 0, tr("CPU"), &cpuValue);
    addCell(0, 1, tr("TEMP"), &cpuTempValue);
    addCell(0, 2, tr("CLOCK"), &cpuClockValue);
    addCell(1, 0, tr("RAM"), &ramValue);
    addCell(1, 1, tr("GPU"), &gpuValue);
    addCell(1, 2, tr("GPU TEMP"), &gpuTempValue);
    addCell(2, 0, tr("VRAM"), &vramValue);

    root->addLayout(metricsGrid);
    root->addStretch(1);

    // --- Целевой процесс ---
    auto *targetRow = new QHBoxLayout;
    targetRow->addWidget(new QLabel(tr("Процесс игры (имя exe):"), this));
    targetEdit = new QLineEdit(this);
    targetEdit->setPlaceholderText(tr("например, notepad.exe"));
    targetRow->addWidget(targetEdit, 1);
    root->addLayout(targetRow);

    connect(targetEdit, &QLineEdit::textChanged,
            this, &MonitorPage::targetChanged);

    statusLabel = new QLabel(this);
    statusLabel->setObjectName(QStringLiteral("fpsStatus"));
    statusLabel->setWordWrap(true);
    root->addWidget(statusLabel);

    toggleBtn = new QPushButton(tr("Начать мониторинг"), this);
    toggleBtn->setObjectName(QStringLiteral("primaryBtn"));
    connect(toggleBtn, &QPushButton::clicked,
            this, &MonitorPage::toggleMonitoringRequested);
    root->addWidget(toggleBtn);
}

void MonitorPage::setFps(int fps)
{
    fpsLabel->setText(fps < 0 ? QStringLiteral("—") : QString::number(fps));
}

void MonitorPage::setLowStats(double onePercent, double pointOnePercent)
{
    if (onePercent <= 0.0 && pointOnePercent <= 0.0) {
        lowLabel->setText(tr("1% low —  ·  0.1% low —"));
        return;
    }

    lowLabel->setText(tr("1% low %1  ·  0.1% low %2")
                          .arg(onePercent, 0, 'f', 1)
                          .arg(pointOnePercent, 0, 'f', 1));
}

void MonitorPage::setSystemMetrics(const QString &cpuLoad, const QString &cpuTemp,
                                   const QString &cpuClock, const QString &ramUsed,
                                   const QString &gpuLoad, const QString &gpuTemp,
                                   const QString &vramUsed)
{
    cpuValue->setText(cpuLoad);
    cpuTempValue->setText(cpuTemp);
    cpuClockValue->setText(cpuClock);
    ramValue->setText(ramUsed);
    gpuValue->setText(gpuLoad);
    gpuTempValue->setText(gpuTemp);
    vramValue->setText(vramUsed);
}

void MonitorPage::setTargetProcess(const QString &imageName)
{
    if (targetEdit->text() != imageName)
        targetEdit->setText(imageName);
}

QString MonitorPage::targetProcess() const
{
    return targetEdit ? targetEdit->text().trimmed() : QString();
}

void MonitorPage::setStatusText(const QString &text)
{
    statusLabel->setText(text);
}

void MonitorPage::setMonitoringActive(bool active)
{
    toggleBtn->setText(active ? tr("Остановить мониторинг")
                              : tr("Начать мониторинг"));
    toggleBtn->setObjectName(active ? QStringLiteral("secondaryBtn")
                                    : QStringLiteral("primaryBtn"));
    toggleBtn->style()->unpolish(toggleBtn);
    toggleBtn->style()->polish(toggleBtn);
}
