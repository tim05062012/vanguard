#ifndef FPSOVERLAY_H
#define FPSOVERLAY_H

#include <QList>
#include <QPair>
#include <QStringList>
#include <QVariantMap>
#include <QWidget>

#include "MetricsProvider.h"

class QScreen;

// Отдельное topmost layered-окно FPS-оверлея (фаза C2):
//  - после show() выставляются WinAPI ex-стили WS_EX_LAYERED |
//    WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE — прозрачность,
//    click-through, скрытие из панели задач, без активации;
//  - полупрозрачный чёрный фон, скруглённые углы, циановая рамка;
//  - 9 якорей позиционирования, размеры S/M/L, прозрачность 0–100 %;
//  - метрики: FPS + 1 %/0.1 % low, график frame time, GPU/CPU/RAM/Net/Disk
//    — каждая включается/выключается отдельно (настройки C4);
//  - выбор монитора (основной/вторичный/активный/оба — C4); при «оба»
//    создаётся зеркальное окно-дублёр на вторичном мониторе;
//  - профили под разные игры (overlayProfiles / overlayProfileBindings).
class FpsOverlay : public QWidget
{
    Q_OBJECT

public:
    enum class Anchor {
        TopLeft, TopCenter, TopRight,
        Left, Center, Right,
        BottomLeft, BottomCenter, BottomRight
    };
    enum class SizePreset { S, M, L };

    explicit FpsOverlay(QWidget *parent = nullptr);
    ~FpsOverlay() override;

    // Читает базовые настройки из SettingsManager и применяет их.
    void loadSettings();

    // Конфиг — QVariantMap с ключами monitor/anchor/size/opacity/
    // showFps/showLows/showGraph/showGpu/showCpu/showRam/showNet/showDisk.
    void applyConfig(const QVariantMap &config);
    QVariantMap baseConfig() const;

    // Профили игр: "" — базовые настройки, иначе overlayProfiles[name]
    // поверх базы. applyProfileForGame ищет имя игры в overlayProfileBindings.
    void applyProfile(const QString &name);
    void applyProfileForGame(const QString &gameName);
    QString activeProfile() const { return m_activeProfile; }

    // Для UI настроек (фаза C4).
    static QStringList anchorKeys();
    static QStringList sizeKeys();
    static QStringList monitorKeys();

public slots:
    void toggle();
    void showOverlay();
    void hideOverlay();

    void setFps(int fps);
    void setLowStats(double p1Fps, double p01Fps);
    void addFrameSamples(const QList<QPair<double, double>> &samples);
    void setSystemMetrics(const SystemMetrics &metrics);

signals:
    void visibilityChanged(bool visible);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    // mirror=true — зеркальное окно-дублёр для monitor=«both» (создаётся
    // только основным экземпляром, само дублёра не создаёт).
    explicit FpsOverlay(bool mirror, QWidget *parent);

    void applyNativeStyles();
    void reposition();
    QSize sizeFor(SizePreset size) const;

    // Компоновка (общая для paintEvent и sizeFor): высоты блоков.
    int graphHeight() const;
    int headerHeight() const;
    int rowsCount() const;
    QScreen *targetScreen() const;

    static Anchor anchorFromKey(const QString &key);
    static QString anchorKey(Anchor anchor);
    static SizePreset sizeFromKey(const QString &key);
    static QString sizeKey(SizePreset size);

    // Отображение.
    Anchor m_anchor = Anchor::BottomRight;
    SizePreset m_size = SizePreset::M;
    int m_opacity = 85; // 0..100
    QString m_monitor = QStringLiteral("active");
    bool m_showFps = true;
    bool m_showLows = true;
    bool m_showGraph = true;
    bool m_showGpu = true;
    bool m_showCpu = true;
    bool m_showRam = true;
    bool m_showNet = true;
    bool m_showDisk = true;
    QString m_activeProfile;

    // Зеркало на вторичном мониторе (monitor=«both»); nullptr у зеркала.
    FpsOverlay *m_mirror = nullptr;

    // Данные.
    double m_fps = -1.0;
    double m_p1 = -1.0;
    double m_p01 = -1.0;
    QList<QPair<double, double>> m_frameTimes; // (t, с; MsBetweenPresents)
    SystemMetrics m_metrics;
};

#endif // FPSOVERLAY_H
