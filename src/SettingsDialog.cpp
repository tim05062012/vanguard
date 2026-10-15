#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "FPSMonitor.h"
#include "FpsOverlay.h"
#include "SettingsManager.h"

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Настройки VANGUARD"));
    setModal(true);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(createGeneralTab(), tr("Основные"));
    tabs->addTab(createOverlayTab(), tr("Оверлей"));

    buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply
            | QDialogButtonBox::Cancel,
        this);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &SettingsDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, [this]() {
                saveToSettings();
                emit overlaySettingsApplied();
            });

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);

    loadFromSettings();
    refreshPresentMonStatus();
}

QWidget *SettingsDialog::createGeneralTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    form->setSpacing(10);

    // v1.0: выбор темы убран — приложение всегда использует тёмную тему
    // (styles.qss, светлая light.qss удалена).

    autoOptimizeCheck = new QCheckBox(
        tr("Оптимизировать автоматически при запуске игры"), page);
    form->addRow(QString(), autoOptimizeCheck);

    closeBrowsersCheck = new QCheckBox(
        tr("Закрывать браузеры при оптимизации"), page);
    form->addRow(QString(), closeBrowsersCheck);

    auto *presentMonLabel = new QLabel(tr("Путь к PresentMon.exe:"), page);
    presentMonEdit = new QLineEdit(page);
    presentMonEdit->setPlaceholderText(tr("Оставьте пустым для автопоиска"));

    auto *presentMonBrowse = new QPushButton(tr("Обзор…"), page);
    presentMonBrowse->setCursor(Qt::PointingHandCursor);
    auto *presentMonRow = new QHBoxLayout;
    presentMonRow->setSpacing(6);
    presentMonRow->addWidget(presentMonEdit, 1);
    presentMonRow->addWidget(presentMonBrowse);
    form->addRow(presentMonLabel, presentMonRow);

    // Статус автопоиска: находит и версионные имена (PresentMon-2.6.0-x64.exe).
    presentMonStatusLabel = new QLabel(page);
    presentMonStatusLabel->setObjectName(QStringLiteral("hintLabel"));
    presentMonStatusLabel->setWordWrap(true);
    form->addRow(QString(), presentMonStatusLabel);

    auto *downloadPresentMonBtn = new QPushButton(tr("Скачать PresentMon"),
                                                  page);
    downloadPresentMonBtn->setCursor(Qt::PointingHandCursor);
    auto *presentMonActions = new QHBoxLayout;
    presentMonActions->setSpacing(6);
    presentMonActions->addWidget(downloadPresentMonBtn);
    presentMonActions->addStretch(1);
    form->addRow(QString(), presentMonActions);

    presentMonRestartAsAdminCheck = new QCheckBox(
        tr("Перезапускать PresentMon от администратора (--restart_as_admin)"),
        page);
    form->addRow(QString(), presentMonRestartAsAdminCheck);

    auto *hint = new QLabel(
        tr("Мониторинг FPS использует PresentMon (нужен член группы "
           "«Performance Log Users» или запуск от администратора)."),
        page);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("hintLabel"));
    form->addRow(QString(), hint);

    connect(presentMonBrowse, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(
            this, tr("Выберите PresentMon.exe"), presentMonEdit->text(),
            tr("PresentMon (*.exe);;Все файлы (*)"));
        if (!file.isEmpty())
            presentMonEdit->setText(QDir::toNativeSeparators(file));
    });
    connect(presentMonEdit, &QLineEdit::textChanged,
            this, &SettingsDialog::refreshPresentMonStatus);
    connect(downloadPresentMonBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral(
            "https://github.com/GameTechDev/PresentMon/releases/latest")));
    });

    return page;
}

QWidget *SettingsDialog::createOverlayTab()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setSpacing(10);

    overlayEnabledCheck = new QCheckBox(tr("Показывать оверлей"), page);
    layout->addWidget(overlayEnabledCheck);

    auto *form = new QFormLayout;
    form->setSpacing(10);

    // Монитор.
    overlayMonitorCombo = new QComboBox(page);
    overlayMonitorCombo->addItem(tr("Основной"), QStringLiteral("primary"));
    overlayMonitorCombo->addItem(tr("Вторичный"),
                                 QStringLiteral("secondary"));
    overlayMonitorCombo->addItem(tr("Активный"), QStringLiteral("active"));
    overlayMonitorCombo->addItem(tr("Оба"), QStringLiteral("both"));
    form->addRow(tr("Монитор:"), overlayMonitorCombo);

    // Позиция (9 якорей).
    overlayAnchorCombo = new QComboBox(page);
    const QStringList anchorLabels{
        tr("Слева сверху"),    tr("По центру сверху"), tr("Справа сверху"),
        tr("Слева по центру"), tr("По центру"),        tr("Справа по центру"),
        tr("Слева снизу"),     tr("По центру снизу"),  tr("Справа снизу")};
    const QStringList anchorKeys = FpsOverlay::anchorKeys();
    for (int i = 0; i < anchorKeys.size(); ++i)
        overlayAnchorCombo->addItem(anchorLabels.value(i, anchorKeys.at(i)),
                                    anchorKeys.at(i));
    form->addRow(tr("Позиция:"), overlayAnchorCombo);

    // Размер S/M/L.
    overlaySizeCombo = new QComboBox(page);
    overlaySizeCombo->addItem(tr("S (малый)"), QStringLiteral("S"));
    overlaySizeCombo->addItem(tr("M (средний)"), QStringLiteral("M"));
    overlaySizeCombo->addItem(tr("L (большой)"), QStringLiteral("L"));
    form->addRow(tr("Размер:"), overlaySizeCombo);

    // Прозрачность 0–100 %.
    auto *opacityRow = new QHBoxLayout;
    opacityRow->setSpacing(8);
    overlayOpacitySlider = new QSlider(Qt::Horizontal, page);
    overlayOpacitySlider->setRange(0, 100);
    overlayOpacitySlider->setFixedWidth(180);
    overlayOpacityLabel = new QLabel(page);
    overlayOpacityLabel->setMinimumWidth(44);
    connect(overlayOpacitySlider, &QSlider::valueChanged, this,
            [this](int v) {
                overlayOpacityLabel->setText(tr("%1 %").arg(v));
            });
    opacityRow->addWidget(overlayOpacitySlider, 1);
    opacityRow->addWidget(overlayOpacityLabel);
    form->addRow(tr("Прозрачность:"), opacityRow);

    // Хоткей переключения оверлея.
    overlayHotkeyCombo = new QComboBox(page);
    overlayHotkeyCombo->setEditable(true);
    overlayHotkeyCombo->addItems({QStringLiteral("Ctrl+Shift+F"),
                                  QStringLiteral("Ctrl+Alt+F"),
                                  QStringLiteral("Ctrl+Shift+O"),
                                  QStringLiteral("Alt+F12")});
    form->addRow(tr("Хоткей:"), overlayHotkeyCombo);

    layout->addLayout(form);

    // Метрики: каждая секция оверлея включается отдельно.
    auto *metricsGroup = new QGroupBox(tr("Метрики"), page);
    auto *metricsGrid = new QGridLayout;
    metricsGrid->setSpacing(8);
    overlayShowFpsCheck = new QCheckBox(tr("FPS"), metricsGroup);
    overlayShowLowsCheck = new QCheckBox(tr("1 % / 0.1 % low"),
                                         metricsGroup);
    overlayShowGraphCheck = new QCheckBox(tr("График frame time"),
                                          metricsGroup);
    overlayShowGpuCheck = new QCheckBox(tr("GPU"), metricsGroup);
    overlayShowCpuCheck = new QCheckBox(tr("CPU"), metricsGroup);
    overlayShowRamCheck = new QCheckBox(tr("RAM"), metricsGroup);
    overlayShowNetCheck = new QCheckBox(tr("Network"), metricsGroup);
    overlayShowDiskCheck = new QCheckBox(tr("Disk"), metricsGroup);
    metricsGrid->addWidget(overlayShowFpsCheck, 0, 0);
    metricsGrid->addWidget(overlayShowLowsCheck, 0, 1);
    metricsGrid->addWidget(overlayShowGraphCheck, 0, 2);
    metricsGrid->addWidget(overlayShowGpuCheck, 1, 0);
    metricsGrid->addWidget(overlayShowCpuCheck, 1, 1);
    metricsGrid->addWidget(overlayShowRamCheck, 1, 2);
    metricsGrid->addWidget(overlayShowNetCheck, 2, 0);
    metricsGrid->addWidget(overlayShowDiskCheck, 2, 1);
    metricsGroup->setLayout(metricsGrid);
    layout->addWidget(metricsGroup);

    auto *hint = new QLabel(
        tr("«Применить» перестраивает оверлей сразу, без перезапуска. "
           "Хоткей действует глобально — даже когда в фокусе игра."),
        page);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(hint);
    layout->addStretch(1);

    return page;
}

void SettingsDialog::refreshPresentMonStatus()
{
    const QString found =
        FPSMonitor::findPresentMon(presentMonEdit->text().trimmed());
    presentMonStatusLabel->setText(
        found.isEmpty()
            ? tr("PresentMon не найден — нажмите «Скачать PresentMon» (GitHub).")
            : tr("Найден: %1").arg(found));
}

void SettingsDialog::loadFromSettings()
{
    SettingsManager &settings = SettingsManager::instance();

    autoOptimizeCheck->setChecked(
        settings.get(QStringLiteral("autoOptimize"), false).toBool());
    closeBrowsersCheck->setChecked(
        settings.get(QStringLiteral("closeBrowsersOnOptimize"), true).toBool());
    presentMonEdit->setText(
        settings.get(QStringLiteral("presentMonPath")).toString());
    presentMonRestartAsAdminCheck->setChecked(
        settings.get(QStringLiteral("presentMonRestartAsAdmin"), false).toBool());

    // Оверлей (C4).
    auto setCombo = [](QComboBox *combo, const QString &value) {
        const int idx = combo->findData(value);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
    };
    overlayEnabledCheck->setChecked(
        settings.get(QStringLiteral("overlayEnabled"), true).toBool());
    setCombo(overlayMonitorCombo,
             settings.get(QStringLiteral("overlayMonitor"),
                          QStringLiteral("active")).toString());
    setCombo(overlayAnchorCombo,
             settings.get(QStringLiteral("overlayAnchor"),
                          QStringLiteral("bottomRight")).toString());
    setCombo(overlaySizeCombo,
             settings.get(QStringLiteral("overlaySize"),
                          QStringLiteral("M")).toString());
    overlayOpacitySlider->setValue(
        settings.get(QStringLiteral("overlayOpacity"), 85).toInt());
    overlayHotkeyCombo->setCurrentText(
        settings.get(QStringLiteral("overlayHotkey"),
                     QStringLiteral("Ctrl+Shift+F")).toString());
    overlayShowFpsCheck->setChecked(
        settings.get(QStringLiteral("overlayShowFps"), true).toBool());
    overlayShowLowsCheck->setChecked(
        settings.get(QStringLiteral("overlayShowLows"), true).toBool());
    overlayShowGraphCheck->setChecked(
        settings.get(QStringLiteral("overlayShowGraph"), true).toBool());
    overlayShowGpuCheck->setChecked(
        settings.get(QStringLiteral("overlayShowGpu"), true).toBool());
    overlayShowCpuCheck->setChecked(
        settings.get(QStringLiteral("overlayShowCpu"), true).toBool());
    overlayShowRamCheck->setChecked(
        settings.get(QStringLiteral("overlayShowRam"), true).toBool());
    overlayShowNetCheck->setChecked(
        settings.get(QStringLiteral("overlayShowNet"), true).toBool());
    overlayShowDiskCheck->setChecked(
        settings.get(QStringLiteral("overlayShowDisk"), true).toBool());
}

void SettingsDialog::saveOverlaySettings()
{
    SettingsManager &settings = SettingsManager::instance();

    settings.set(QStringLiteral("overlayEnabled"),
                 overlayEnabledCheck->isChecked());
    settings.set(QStringLiteral("overlayMonitor"),
                 overlayMonitorCombo->currentData());
    settings.set(QStringLiteral("overlayAnchor"),
                 overlayAnchorCombo->currentData());
    settings.set(QStringLiteral("overlaySize"),
                 overlaySizeCombo->currentData());
    settings.set(QStringLiteral("overlayOpacity"),
                 overlayOpacitySlider->value());
    const QString hotkey = overlayHotkeyCombo->currentText().trimmed();
    settings.set(QStringLiteral("overlayHotkey"),
                 hotkey.isEmpty() ? QStringLiteral("Ctrl+Shift+F")
                                  : hotkey);
    settings.set(QStringLiteral("overlayShowFps"),
                 overlayShowFpsCheck->isChecked());
    settings.set(QStringLiteral("overlayShowLows"),
                 overlayShowLowsCheck->isChecked());
    settings.set(QStringLiteral("overlayShowGraph"),
                 overlayShowGraphCheck->isChecked());
    settings.set(QStringLiteral("overlayShowGpu"),
                 overlayShowGpuCheck->isChecked());
    settings.set(QStringLiteral("overlayShowCpu"),
                 overlayShowCpuCheck->isChecked());
    settings.set(QStringLiteral("overlayShowRam"),
                 overlayShowRamCheck->isChecked());
    settings.set(QStringLiteral("overlayShowNet"),
                 overlayShowNetCheck->isChecked());
    settings.set(QStringLiteral("overlayShowDisk"),
                 overlayShowDiskCheck->isChecked());
}

void SettingsDialog::saveToSettings()
{
    SettingsManager &settings = SettingsManager::instance();

    settings.set(QStringLiteral("autoOptimize"),
                 autoOptimizeCheck->isChecked());
    settings.set(QStringLiteral("closeBrowsersOnOptimize"),
                 closeBrowsersCheck->isChecked());
    settings.set(QStringLiteral("presentMonPath"),
                 presentMonEdit->text().trimmed());
    settings.set(QStringLiteral("presentMonRestartAsAdmin"),
                 presentMonRestartAsAdminCheck->isChecked());

    saveOverlaySettings();
    settings.save();
}

void SettingsDialog::accept()
{
    saveToSettings();
    emit overlaySettingsApplied();
    QDialog::accept();
}