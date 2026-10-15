#include "FpsOverlay.h"

#include <QCursor>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStringList>

#include "SettingsManager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// -1 означает «нет данных».
QString fmtPct(double v)
{
    return v < 0 ? QStringLiteral("—")
                 : QString::number(v, 'f', 0) + QStringLiteral(" %");
}

QString fmtTemp(double v)
{
    return v < 0 ? QString()
                 : QString::number(v, 'f', 0) + QStringLiteral(" °C");
}

QString fmtGb(double used, double total)
{
    if (used < 0 || total < 0)
        return QStringLiteral("—");
    return QString::number(used, 'f', 1) + QStringLiteral("/")
           + QString::number(total, 'f', 1) + QStringLiteral(" GB");
}

// kbs — килобайты в секунду.
QString fmtRate(double kbs)
{
    if (kbs < 0)
        return QStringLiteral("—");
    if (kbs < 1024.0)
        return QString::number(kbs, 'f', 0) + QStringLiteral(" KB/s");
    return QString::number(kbs / 1024.0, 'f', 1) + QStringLiteral(" MB/s");
}

} // namespace

FpsOverlay::~FpsOverlay()
{
    delete m_mirror;
}

FpsOverlay::FpsOverlay(QWidget *parent)
    : FpsOverlay(false, parent)
{
    // Зеркальное окно-дублёр для monitor=«both» (показывается только в этом
    // режиме; конфиг и данные синхронизируются из основного окна).
    m_mirror = new FpsOverlay(true, nullptr);
}

FpsOverlay::FpsOverlay(bool mirror, QWidget *parent)
    : QWidget(parent)
{
    Q_UNUSED(mirror)
    setWindowTitle(QStringLiteral("VANGUARD — FPS Overlay"));

    // Tool — не на панели задач; TransparentForInput — клики сквозь окно;
    // DoesNotAcceptFocus — не воруем фокус у игры.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint
                   | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput
                   | Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);

    resize(sizeFor(m_size));
}

void FpsOverlay::applyNativeStyles()
{
#ifdef Q_OS_WIN
    // Явная установка ex-стилей поверх Qt-флагов: WS_EX_LAYERED —
    // прозрачность, WS_EX_TRANSPARENT — click-through, WS_EX_TOOLWINDOW —
    // не на панели задач, WS_EX_NOACTIVATE — без активации.
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd)
        return;
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                      ex | WS_EX_LAYERED | WS_EX_TRANSPARENT
                              | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#endif
}

QSize FpsOverlay::sizeFor(SizePreset size) const
{
    // Ширина — по пресету; высота — по фактическому контенту (включённые
    // метрики), чтобы панель не имела пустот при выключенных строках.
    int w = 330;
    switch (size) {
    case SizePreset::S: w = 240; break;
    case SizePreset::M: w = 330; break;
    case SizePreset::L: w = 420; break;
    }

    // Геометрия согласована с paintEvent: pad=12, зазор 6, rowH=16.
    const int headerH = headerHeight();
    const int graphH = graphHeight(); // целевая высота графика по пресету
    const int rows = rowsCount();

    int contentH = headerH + 6;
    if (graphH > 0)
        contentH += graphH;
    if (rows > 0)
        contentH += rows * 16 + (graphH > 0 ? 6 : 0);
    if (contentH < 40)
        contentH = 40;
    return QSize(w, contentH + 24);
}

int FpsOverlay::graphHeight() const
{
    if (!m_showGraph)
        return 0;
    switch (m_size) {
    case SizePreset::S: return 64;
    case SizePreset::M: return 90;
    case SizePreset::L: return 112;
    }
    return 90;
}

int FpsOverlay::headerHeight() const
{
    // Согласовано с paintEvent: число FPS — px*1.25, lows — два ряда.
    if (m_showFps) {
        const int px = m_size == SizePreset::S ? 26
                       : m_size == SizePreset::M ? 32
                                                 : 38;
        return int(px * 1.25);
    }
    if (m_showLows)
        return 30;
    return 0;
}

int FpsOverlay::rowsCount() const
{
    return (m_showGpu ? 1 : 0) + (m_showCpu ? 1 : 0) + (m_showRam ? 1 : 0)
           + (m_showNet ? 1 : 0) + (m_showDisk ? 1 : 0);
}

QScreen *FpsOverlay::targetScreen() const
{
    const QList<QScreen *> all = QGuiApplication::screens();

    if (m_monitor == QLatin1String("primary"))
        return QGuiApplication::primaryScreen();

    if (m_monitor == QLatin1String("secondary"))
        return all.size() > 1 ? all.at(1) : QGuiApplication::primaryScreen();

    if (m_monitor == QLatin1String("active")) {
#ifdef Q_OS_WIN
        // Монитор активного окна: центр окна в физических координатах
        // сопоставляется с физическим rect'ом экранов (физ = логич. * dpr).
        if (HWND fg = GetForegroundWindow()) {
            RECT rc;
            if (GetWindowRect(fg, &rc)) {
                const POINT c{(rc.left + rc.right) / 2,
                              (rc.top + rc.bottom) / 2};
                for (QScreen *s : all) {
                    const QRect g = s->geometry();
                    const qreal dpr = s->devicePixelRatio();
                    const QRect phys(int(g.x() * dpr), int(g.y() * dpr),
                                     int(g.width() * dpr),
                                     int(g.height() * dpr));
                    if (phys.contains(QPoint(c.x, c.y)))
                        return s;
                }
            }
        }
#endif
        if (QScreen *s = QGuiApplication::screenAt(QCursor::pos()))
            return s;
    }

    // «both» (основной дублёр = primary) и fallback.
    if (m_monitor == QLatin1String("both"))
        return QGuiApplication::primaryScreen();
    return QGuiApplication::primaryScreen();
}

void FpsOverlay::reposition()
{
    const QSize sz = sizeFor(m_size);
    resize(sz);

    // Якоря — на выбранном мониторе (primary/secondary/active/both).
    QScreen *screen = targetScreen();
    const QRect ag = screen ? screen->availableGeometry()
                            : QRect(0, 0, 1920, 1080);
    const int margin = 14;
    const int w = sz.width();
    const int h = sz.height();

    int x = ag.left() + margin;
    int y = ag.top() + margin;

    switch (m_anchor) {
    case Anchor::TopLeft:
        break;
    case Anchor::TopCenter:
        x = ag.center().x() - w / 2;
        break;
    case Anchor::TopRight:
        x = ag.right() - margin - w + 1;
        break;
    case Anchor::Left:
        y = ag.center().y() - h / 2;
        break;
    case Anchor::Center:
        x = ag.center().x() - w / 2;
        y = ag.center().y() - h / 2;
        break;
    case Anchor::Right:
        x = ag.right() - margin - w + 1;
        y = ag.center().y() - h / 2;
        break;
    case Anchor::BottomLeft:
        y = ag.bottom() - margin - h + 1;
        break;
    case Anchor::BottomCenter:
        x = ag.center().x() - w / 2;
        y = ag.bottom() - margin - h + 1;
        break;
    case Anchor::BottomRight:
        x = ag.right() - margin - w + 1;
        y = ag.bottom() - margin - h + 1;
        break;
    }

    move(x, y);
}

void FpsOverlay::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // winId() доступен только после show() — стили и позиция здесь.
    reposition();
    applyNativeStyles();
}

void FpsOverlay::loadSettings()
{
    SettingsManager &s = SettingsManager::instance();
    QVariantMap cfg;
    cfg.insert(QStringLiteral("monitor"),
               s.get(QStringLiteral("overlayMonitor"),
                     QStringLiteral("active")));
    cfg.insert(QStringLiteral("anchor"),
               s.get(QStringLiteral("overlayAnchor"),
                     QStringLiteral("bottomRight")));
    cfg.insert(QStringLiteral("size"),
               s.get(QStringLiteral("overlaySize"), QStringLiteral("M")));
    cfg.insert(QStringLiteral("opacity"),
               s.get(QStringLiteral("overlayOpacity"), 85));
    cfg.insert(QStringLiteral("showFps"),
               s.get(QStringLiteral("overlayShowFps"), true));
    cfg.insert(QStringLiteral("showLows"),
               s.get(QStringLiteral("overlayShowLows"), true));
    cfg.insert(QStringLiteral("showGraph"),
               s.get(QStringLiteral("overlayShowGraph"), true));
    cfg.insert(QStringLiteral("showGpu"),
               s.get(QStringLiteral("overlayShowGpu"), true));
    cfg.insert(QStringLiteral("showCpu"),
               s.get(QStringLiteral("overlayShowCpu"), true));
    cfg.insert(QStringLiteral("showRam"),
               s.get(QStringLiteral("overlayShowRam"), true));
    cfg.insert(QStringLiteral("showNet"),
               s.get(QStringLiteral("overlayShowNet"), true));
    cfg.insert(QStringLiteral("showDisk"),
               s.get(QStringLiteral("overlayShowDisk"), true));

    m_activeProfile.clear();
    if (m_mirror)
        m_mirror->m_activeProfile.clear();
    applyConfig(cfg);
}

void FpsOverlay::applyConfig(const QVariantMap &config)
{
    if (config.contains(QStringLiteral("anchor")))
        m_anchor =
            anchorFromKey(config.value(QStringLiteral("anchor")).toString());
    if (config.contains(QStringLiteral("size")))
        m_size = sizeFromKey(config.value(QStringLiteral("size")).toString());
    if (config.contains(QStringLiteral("opacity")))
        m_opacity = qBound(0, config.value(QStringLiteral("opacity")).toInt(),
                           100);
    if (config.contains(QStringLiteral("monitor")))
        m_monitor = config.value(QStringLiteral("monitor")).toString();
    if (config.contains(QStringLiteral("showFps")))
        m_showFps = config.value(QStringLiteral("showFps")).toBool();
    if (config.contains(QStringLiteral("showLows")))
        m_showLows = config.value(QStringLiteral("showLows")).toBool();
    if (config.contains(QStringLiteral("showGraph")))
        m_showGraph = config.value(QStringLiteral("showGraph")).toBool();
    if (config.contains(QStringLiteral("showGpu")))
        m_showGpu = config.value(QStringLiteral("showGpu")).toBool();
    if (config.contains(QStringLiteral("showCpu")))
        m_showCpu = config.value(QStringLiteral("showCpu")).toBool();
    if (config.contains(QStringLiteral("showRam")))
        m_showRam = config.value(QStringLiteral("showRam")).toBool();
    if (config.contains(QStringLiteral("showNet")))
        m_showNet = config.value(QStringLiteral("showNet")).toBool();
    if (config.contains(QStringLiteral("showDisk")))
        m_showDisk = config.value(QStringLiteral("showDisk")).toBool();

    setWindowOpacity(qBound(0.0, m_opacity / 100.0, 1.0));

    // Зеркало-дублёр: тот же конфиг, но всегда вторичный монитор.
    if (m_mirror) {
        QVariantMap mirrored = config;
        mirrored.insert(QStringLiteral("monitor"),
                        QStringLiteral("secondary"));
        m_mirror->applyConfig(mirrored);
    }

    reposition();
    update();
}

QVariantMap FpsOverlay::baseConfig() const
{
    QVariantMap cfg;
    cfg.insert(QStringLiteral("monitor"), m_monitor);
    cfg.insert(QStringLiteral("anchor"), anchorKey(m_anchor));
    cfg.insert(QStringLiteral("size"), sizeKey(m_size));
    cfg.insert(QStringLiteral("opacity"), m_opacity);
    cfg.insert(QStringLiteral("showFps"), m_showFps);
    cfg.insert(QStringLiteral("showLows"), m_showLows);
    cfg.insert(QStringLiteral("showGraph"), m_showGraph);
    cfg.insert(QStringLiteral("showGpu"), m_showGpu);
    cfg.insert(QStringLiteral("showCpu"), m_showCpu);
    cfg.insert(QStringLiteral("showRam"), m_showRam);
    cfg.insert(QStringLiteral("showNet"), m_showNet);
    cfg.insert(QStringLiteral("showDisk"), m_showDisk);
    return cfg;
}

void FpsOverlay::applyProfile(const QString &name)
{
    // База — настройки приложения; профиль игры применяется поверх базы.
    loadSettings();
    if (name.isEmpty())
        return;

    const QVariantMap profiles = SettingsManager::instance()
                                     .get(QStringLiteral("overlayProfiles"))
                                     .toMap();
    if (!profiles.contains(name))
        return;

    QVariantMap cfg = baseConfig();
    const QVariantMap profile = profiles.value(name).toMap();
    for (auto it = profile.begin(); it != profile.end(); ++it)
        cfg.insert(it.key(), it.value());

    m_activeProfile = name;
    applyConfig(cfg);
}

void FpsOverlay::applyProfileForGame(const QString &gameName)
{
    const QVariantMap bindings =
        SettingsManager::instance()
            .get(QStringLiteral("overlayProfileBindings"))
            .toMap();
    applyProfile(bindings.value(gameName).toString());
}

void FpsOverlay::toggle()
{
    if (isVisible())
        hideOverlay();
    else
        showOverlay();
}

void FpsOverlay::showOverlay()
{
    if (!isVisible()) {
        show();
        emit visibilityChanged(true);
    }
    // Зеркало — только в режиме «оба» и при наличии второго монитора.
    if (m_mirror && m_monitor == QLatin1String("both")
        && QGuiApplication::screens().size() > 1) {
        m_mirror->show();
        emit visibilityChanged(true);
    }
}

void FpsOverlay::hideOverlay()
{
    if (isVisible()) {
        hide();
        emit visibilityChanged(false);
    }
    if (m_mirror && m_mirror->isVisible()) {
        m_mirror->hide();
        emit visibilityChanged(false);
    }
}

void FpsOverlay::setFps(int fps)
{
    m_fps = fps;
    if (m_mirror)
        m_mirror->setFps(fps);
    update();
}

void FpsOverlay::setLowStats(double p1Fps, double p01Fps)
{
    m_p1 = p1Fps;
    m_p01 = p01Fps;
    if (m_mirror)
        m_mirror->setLowStats(p1Fps, p01Fps);
    update();
}

void FpsOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // Прозрачность оверлея (0–100 %) — общий множитель всех отрисовок.
    p.setOpacity(qBound(0, m_opacity, 100) / 100.0);

    const QColor cyan(QStringLiteral("#00E5FF"));
    const QColor textMain(QStringLiteral("#E4E4E4"));
    const QColor textDim(QStringLiteral("#8A8F98"));

    // Фон: чёрный rounded rect + 1px циановая рамка.
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(frame, 10.0, 10.0);
    p.fillPath(path, QColor(0, 0, 0, 190));
    p.setPen(QPen(cyan, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    const int pad = 12;
    const QRectF content = frame.adjusted(pad, pad, -pad, -pad);
    if (content.width() < 60 || content.height() < 24)
        return;

    QFont fpsFont(QStringLiteral("Consolas"));
    fpsFont.setBold(true);
    fpsFont.setPixelSize(m_size == SizePreset::S ? 26
                         : m_size == SizePreset::M ? 32
                                                   : 38);
    QFont smallFont(QStringLiteral("Consolas"));
    smallFont.setPixelSize(11);
    QFont labelFont(QStringLiteral("Consolas"));
    labelFont.setPixelSize(10);

    // --- Шапка: крупный FPS слева, 1 % / 0.1 % low справа ---
    double headerH = 0;
    if (m_showFps)
        headerH = fpsFont.pixelSize() * 1.25;
    else if (m_showLows)
        headerH = 30;
    const QString fpsText = m_fps >= 0
        ? QString::number(m_fps, 'f', 0)
        : QStringLiteral("—");

    if (m_showFps) {
        p.setFont(fpsFont);
        p.setPen(cyan);
        p.drawText(QRectF(content.left(), content.top(),
                          content.width() * 0.5, headerH),
                   Qt::AlignLeft | Qt::AlignVCenter, fpsText);

        p.setFont(labelFont);
        p.setPen(textDim);
        const double fpsW = QFontMetrics(fpsFont).horizontalAdvance(fpsText);
        p.drawText(QRectF(content.left() + fpsW + 6, content.top(),
                          40, headerH),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("FPS"));
    }

    if (m_showLows) {
        p.setFont(smallFont);
        const QRectF right(
            m_showFps
                ? content.left() + content.width() * 0.45
                : content.left(),
            content.top(),
            m_showFps ? content.width() * 0.55 : content.width(),
            headerH);
        p.setPen(textMain);
        p.drawText(QRectF(right.left(), right.top(), right.width(),
                          headerH * 0.5),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("1%  ")
                       + (m_p1 >= 0 ? QString::number(m_p1, 'f', 0)
                                    : QStringLiteral("—")));
        p.setPen(textDim);
        p.drawText(QRectF(right.left(), right.top() + headerH * 0.5,
                          right.width(), headerH * 0.5),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("0.1%  ")
                       + (m_p01 >= 0 ? QString::number(m_p01, 'f', 0)
                                     : QStringLiteral("—")));
    }

    // --- Системные строки (CPU / GPU / RAM / NET / DISK) по флагам ---
    const SystemMetrics &m = m_metrics;
    QStringList rows;
    if (m_showCpu) {
        QString cpu = fmtPct(m.cpuLoad);
        if (m.cpuTemp >= 0)
            cpu += QStringLiteral("  ·  ") + fmtTemp(m.cpuTemp);
        rows << QStringLiteral("CPU") << cpu;
    }
    if (m_showGpu) {
        QString gpu = fmtPct(m.gpuLoad);
        if (m.gpuTemp >= 0)
            gpu += QStringLiteral("  ·  ") + fmtTemp(m.gpuTemp);
        if (m.vramTotalGb >= 0)
            gpu += QStringLiteral("  ·  ")
                   + fmtGb(m.vramUsedGb, m.vramTotalGb);
        rows << QStringLiteral("GPU") << gpu;
    }
    if (m_showRam) {
        QString ram = fmtGb(m.ramUsedGb, m.ramTotalGb);
        if (m.ramPercent >= 0)
            ram += QStringLiteral(" (%1 %)")
                       .arg(m.ramPercent, 0, 'f', 0);
        rows << QStringLiteral("RAM") << ram;
    }
    if (m_showNet)
        rows << QStringLiteral("NET")
             << QStringLiteral("↓ ") + fmtRate(m.netDownKbs)
                    + QStringLiteral("   ↑ ") + fmtRate(m.netUpKbs);
    if (m_showDisk)
        rows << QStringLiteral("DISK")
             << QStringLiteral("↓ ") + fmtRate(m.diskReadMbs * 1024.0)
                    + QStringLiteral("   ↑ ")
                    + fmtRate(m.diskWriteMbs * 1024.0);
    const int rowCount = rows.size() / 2;

    // --- Секции: шапка, график frame time, системные строки ---
    double y = content.top() + headerH + 6;
    const double rowH = 16.0;
    const double rowsH = rowCount * rowH;

    double graphH = 0;
    if (m_showGraph)
        graphH = content.bottom() - y - rowsH - (rowCount > 0 ? 6.0 : 0.0);
    if (graphH < 30.0)
        graphH = 0.0;

    // --- График frame time (окно 10 с, сетка 16.7/33.4 мс) ---
    if (graphH > 0) {
        const QRectF gr(content.left(), y, content.width(), graphH);

        double maxMs = 33.4;
        for (const auto &s : m_frameTimes)
            maxMs = qMax(maxMs, s.second);
        maxMs = qMin(maxMs, 100.0);

        auto yFor = [&](double ms) {
            return gr.bottom()
                   - qBound(0.0, ms, maxMs) / maxMs * gr.height();
        };

        p.setPen(QPen(QColor(255, 255, 255, 28), 1));
        for (double ms : {16.7, 33.4}) {
            if (ms < maxMs)
                p.drawLine(QPointF(gr.left(), yFor(ms)),
                           QPointF(gr.right(), yFor(ms)));
        }

        if (m_frameTimes.size() >= 2) {
            const double t0 = m_frameTimes.first().first;
            const double span =
                qMax(m_frameTimes.last().first - t0, 0.001);

            QPainterPath line;
            for (int i = 0; i < m_frameTimes.size(); ++i) {
                const auto &s = m_frameTimes.at(i);
                const QPointF pt(
                    gr.left() + (s.first - t0) / span * gr.width(),
                    yFor(s.second));
                if (i == 0)
                    line.moveTo(pt);
                else
                    line.lineTo(pt);
            }

            QPainterPath area = line;
            area.lineTo(gr.right(), gr.bottom());
            area.lineTo(gr.left(), gr.bottom());
            area.closeSubpath();

            QLinearGradient grad(gr.left(), gr.top(),
                                 gr.left(), gr.bottom());
            grad.setColorAt(0.0, QColor(0, 229, 255, 90));
            grad.setColorAt(1.0, QColor(0, 229, 255, 0));
            p.fillPath(area, grad);
            p.setPen(QPen(cyan, 1.5));
            p.drawPath(line);
        } else {
            p.setPen(textDim);
            p.setFont(smallFont);
            p.drawText(gr, Qt::AlignCenter, QStringLiteral("—"));
        }
        y += graphH + 6;
    }

    // --- Отрисовка системных строк (rows построены по флагам выше) ---
    if (rowCount > 0) {
        p.setFont(smallFont);
        for (int i = 0; i < rowCount; ++i) {
            const QRectF row(content.left(), y + i * rowH,
                             content.width(), rowH);
            p.setPen(textDim);
            p.drawText(QRectF(row.left(), row.top(), 44, row.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, rows.at(i * 2));
            p.setPen(textMain);
            p.drawText(QRectF(row.left() + 46, row.top(),
                              row.width() - 46, row.height()),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       rows.at(i * 2 + 1));
        }
    }
}

void FpsOverlay::addFrameSamples(const QList<QPair<double, double>> &samples)
{
    if (samples.isEmpty())
        return;
    if (m_mirror)
        m_mirror->addFrameSamples(samples);
    m_frameTimes.append(samples);

    // Окно истории компактного графика — 10 с.
    const double lastT = m_frameTimes.last().first;
    int drop = 0;
    while (drop < m_frameTimes.size()
           && m_frameTimes.at(drop).first < lastT - 10.0)
        ++drop;
    if (drop > 0)
        m_frameTimes.remove(0, drop);
    if (m_frameTimes.size() > 2000)
        m_frameTimes.remove(0, m_frameTimes.size() - 2000);

    update();
}

void FpsOverlay::setSystemMetrics(const SystemMetrics &metrics)
{
    m_metrics = metrics;
    if (m_mirror)
        m_mirror->setSystemMetrics(metrics);
    update();
}

QStringList FpsOverlay::monitorKeys()
{
    return {QStringLiteral("primary"), QStringLiteral("secondary"),
            QStringLiteral("active"), QStringLiteral("both")};
}

QStringList FpsOverlay::anchorKeys()
{
    return {QStringLiteral("topLeft"),   QStringLiteral("topCenter"),
            QStringLiteral("topRight"),  QStringLiteral("left"),
            QStringLiteral("center"),    QStringLiteral("right"),
            QStringLiteral("bottomLeft"),
            QStringLiteral("bottomCenter"),
            QStringLiteral("bottomRight")};
}

QStringList FpsOverlay::sizeKeys()
{
    return {QStringLiteral("S"), QStringLiteral("M"),
            QStringLiteral("L")};
}

FpsOverlay::Anchor FpsOverlay::anchorFromKey(const QString &key)
{
    if (key == QLatin1String("topLeft"))
        return Anchor::TopLeft;
    if (key == QLatin1String("topCenter"))
        return Anchor::TopCenter;
    if (key == QLatin1String("topRight"))
        return Anchor::TopRight;
    if (key == QLatin1String("left"))
        return Anchor::Left;
    if (key == QLatin1String("center"))
        return Anchor::Center;
    if (key == QLatin1String("right"))
        return Anchor::Right;
    if (key == QLatin1String("bottomLeft"))
        return Anchor::BottomLeft;
    if (key == QLatin1String("bottomCenter"))
        return Anchor::BottomCenter;
    return Anchor::BottomRight;
}

QString FpsOverlay::anchorKey(Anchor anchor)
{
    switch (anchor) {
    case Anchor::TopLeft: return QStringLiteral("topLeft");
    case Anchor::TopCenter: return QStringLiteral("topCenter");
    case Anchor::TopRight: return QStringLiteral("topRight");
    case Anchor::Left: return QStringLiteral("left");
    case Anchor::Center: return QStringLiteral("center");
    case Anchor::Right: return QStringLiteral("right");
    case Anchor::BottomLeft: return QStringLiteral("bottomLeft");
    case Anchor::BottomCenter: return QStringLiteral("bottomCenter");
    case Anchor::BottomRight: break;
    }
    return QStringLiteral("bottomRight");
}

FpsOverlay::SizePreset FpsOverlay::sizeFromKey(const QString &key)
{
    if (key == QLatin1String("S"))
        return SizePreset::S;
    if (key == QLatin1String("L"))
        return SizePreset::L;
    return SizePreset::M;
}

QString FpsOverlay::sizeKey(SizePreset size)
{
    switch (size) {
    case SizePreset::S: return QStringLiteral("S");
    case SizePreset::M: return QStringLiteral("M");
    case SizePreset::L: break;
    }
    return QStringLiteral("L");
}


