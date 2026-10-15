#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSlider;
class QTabWidget;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void accept() override;

signals:
    // Эмитируется при «Применить» и при OK — настройки оверлея сохранены,
    // окно настроек может быть открыто: оверлей обновляется на лету.
    void overlaySettingsApplied();

private:
    void loadFromSettings();
    void refreshPresentMonStatus();
    void saveToSettings();
    void saveOverlaySettings();
    QWidget *createGeneralTab();
    QWidget *createOverlayTab();

    // Общие настройки.
    QCheckBox *autoOptimizeCheck = nullptr;
    QCheckBox *closeBrowsersCheck = nullptr;
    QCheckBox *presentMonRestartAsAdminCheck = nullptr;
    QLineEdit *presentMonEdit = nullptr;
    QLabel *presentMonStatusLabel = nullptr;

    // Оверлей (C4).
    QCheckBox *overlayEnabledCheck = nullptr;
    QComboBox *overlayMonitorCombo = nullptr;
    QComboBox *overlayAnchorCombo = nullptr;
    QComboBox *overlaySizeCombo = nullptr;
    QSlider *overlayOpacitySlider = nullptr;
    QLabel *overlayOpacityLabel = nullptr;
    QComboBox *overlayHotkeyCombo = nullptr;
    QCheckBox *overlayShowFpsCheck = nullptr;
    QCheckBox *overlayShowLowsCheck = nullptr;
    QCheckBox *overlayShowGraphCheck = nullptr;
    QCheckBox *overlayShowGpuCheck = nullptr;
    QCheckBox *overlayShowCpuCheck = nullptr;
    QCheckBox *overlayShowRamCheck = nullptr;
    QCheckBox *overlayShowNetCheck = nullptr;
    QCheckBox *overlayShowDiskCheck = nullptr;

    QDialogButtonBox *buttons = nullptr;
};

#endif // SETTINGSDIALOG_H