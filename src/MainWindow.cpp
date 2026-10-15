#include "MainWindow.h"

#include <QCloseEvent>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QProcess>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "SettingsManager.h"
#include "SettingsDialog.h"
#include "AddGameDialog.h"
#include "CoverCache.h"
#include "LocalGamesStore.h"
#include "FpsOverlay.h"

#include "ui/TitleBar.h"
#include "ui/Sidebar.h"
#include "ui/LibraryPage.h"
#include "ui/MonitorPage.h"
#include "ui/OptimizePage.h"
#include "ui/FrameTimeGraph.h"

namespace {
#ifdef Q_OS_WIN
// ID глобального хоткея переключения оверлея (фаза C3).
constexpr int kOverlayHotkeyId = 1;
#endif

#ifdef Q_OS_WIN
// Парсит строку вида "Ctrl+Shift+F" / "Ctrl+Alt+F" / "Win+Shift+F"
// в модификаторы и virtual-key для RegisterHotKey.
bool parseHotkey(const QString &text, DWORD &mods, DWORD &vk)
{
    const QStringList parts = text.split(QLatin1Char('+'),
                                         Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return false;

    mods = 0;
    for (int i = 0; i < parts.size() - 1; ++i) {
        const QString m = parts.at(i).trimmed().toLower();
        if (m == QLatin1String("ctrl") || m == QLatin1String("control"))
            mods |= MOD_CONTROL;
        else if (m == QLatin1String("alt"))
            mods |= MOD_ALT;
        else if (m == QLatin1String("shift"))
            mods |= MOD_SHIFT;
        else if (m == QLatin1String("win") || m == QLatin1String("windows"))
            mods |= MOD_WIN;
        else
            return false;
    }

    const QString key = parts.last().trimmed().toUpper();
    if (key.size() == 1) {
        const QChar c = key.at(0);
        if ((c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
            || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))) {
            vk = c.toLatin1();
            return true;
        }
        return false;
    }
    // F1..F12 → VK_F1..VK_F12 (0x70..0x7B).
    if (key.size() >= 2 && key.at(0) == QLatin1Char('F')) {
        bool ok = false;
        const int n = key.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 12) {
            vk = 0x70 + DWORD(n - 1);
            return true;
        }
    }
    return false;
}
#endif
} // namespace

MainWindow::~MainWindow()
{
#ifdef Q_OS_WIN
    // Снимаем глобальный хоткей до уничтожения hwnd.
    if (m_hotkeyWin)
        UnregisterHotKey(reinterpret_cast<HWND>(m_hotkeyWin),
                         kOverlayHotkeyId);
#endif
    // Оверлей — отдельное top-level окно (parent = nullptr), удаляем сами.
    delete fpsOverlay;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    SettingsManager::instance().load();

    // Фаза E: битый settings.json — предупредить один раз (диалог после
    // показа окна; копия данных уже сохранена в settings.json.bad).
    if (SettingsManager::instance().lastLoadFailed()) {
        QTimer::singleShot(500, this, [this]() {
            QMessageBox::warning(
                this, tr("Настройки повреждены"),
                tr("Файл настроек не удалось прочитать — используются "
                   "значения по умолчанию.\n\nИсходный файл сохранён как:\n%1")
                    .arg(SettingsManager::settingsPath() + ".bad"));
        });
    }

    connect(&fpsMonitor, &FPSMonitor::fpsUpdated,
            this, &MainWindow::updateFps);
    connect(&fpsMonitor, &FPSMonitor::percentilesUpdated, this,
            [this](double one, double pointOne) {
                if (monitorPage)
                    monitorPage->setLowStats(one, pointOne);
            });
    connect(&fpsMonitor, &FPSMonitor::framesUpdated, this,
            [this](const QList<QPair<double, double>> &frames) {
                if (monitorPage && monitorPage->frameTimeGraph())
                    monitorPage->frameTimeGraph()->addSamples(frames);
            });
    connect(&fpsMonitor, &FPSMonitor::monitoringStopped, this, [this]() {
        if (monitorPage) {
            monitorPage->setMonitoringActive(false);
            monitorPage->setLowStats(-1, -1);
            if (monitorPage->frameTimeGraph())
                monitorPage->frameTimeGraph()->clear();
            const QString error = fpsMonitor.lastError();
            monitorPage->setStatusText(error.isEmpty()
                                           ? tr("Мониторинг остановлен.")
                                           : error);
        }
        if (fpsOverlay) {
            fpsOverlay->setFps(-1);
            fpsOverlay->setLowStats(-1, -1);
        }
    });

    // Системные метрики (CPU/RAM/GPU/сеть/диск) на страницу Мониторинг.
    // -1 из SystemMetrics означает «нет данных» — рисуем тире.
    connect(&metricsProvider, &MetricsProvider::metricsUpdated, this,
            [this](const SystemMetrics &m) {
                if (!monitorPage)
                    return;

                auto fmt = [](double v, const QString &unit, int decimals) {
                    return v < 0 ? QStringLiteral("—")
                                 : QString::number(v, 'f', decimals) + unit;
                };
                auto fmtPair = [](double used, double total,
                                  const QString &unit) {
                    if (used < 0 || total < 0)
                        return QStringLiteral("—");
                    return QStringLiteral("%1 / %2%3")
                        .arg(used, 0, 'f', 1)
                        .arg(total, 0, 'f', 1)
                        .arg(unit);
                };

                monitorPage->setSystemMetrics(
                    fmt(m.cpuLoad, QStringLiteral("%"), 0),
                    fmt(m.cpuTemp, QStringLiteral(" °C"), 0),
                    fmt(m.cpuClock, QStringLiteral(" MHz"), 0),
                    fmtPair(m.ramUsedGb, m.ramTotalGb, QStringLiteral(" GB")),
                    fmt(m.gpuLoad, QStringLiteral("%"), 0),
                    fmt(m.gpuTemp, QStringLiteral(" °C"), 0),
                    fmtPair(m.vramUsedGb, m.vramTotalGb,
                            QStringLiteral(" GB")));
            });

    setupUi();
    setupWindowChrome();

    // Фаза E: восстановление геометрии окна и последней страницы
    // (saveGeometry включает и состояние «развёрнуто»).
    {
        const auto &sm = SettingsManager::instance();
        const QByteArray geometry =
            sm.get(QStringLiteral("windowGeometry")).toByteArray();
        if (!geometry.isEmpty())
            restoreGeometry(geometry);
        const int lastPage = sm.get(QStringLiteral("lastPage"), 0).toInt();
        if (pages && lastPage > 0 && lastPage < pages->count())
            pages->setCurrentIndex(lastPage);
    }

    refreshGameList();
    applyTheme();

    // FPS-оверлей (фаза C2): topmost click-through окно поверх игр.
    fpsOverlay = new FpsOverlay();
    fpsOverlay->loadSettings();

    // Фаза E: активный профиль оверлея переживает перезапуск
    // (loadSettings() его сбрасывает — восстанавливаем из настроек).
    const QString savedProfile =
        SettingsManager::instance()
            .get(QStringLiteral("overlayActiveProfile"))
            .toString();
    if (!savedProfile.isEmpty())
        fpsOverlay->applyProfile(savedProfile);

    connect(&fpsMonitor, &FPSMonitor::fpsUpdated,
            fpsOverlay, &FpsOverlay::setFps);
    connect(&fpsMonitor, &FPSMonitor::percentilesUpdated,
            fpsOverlay, &FpsOverlay::setLowStats);
    connect(&fpsMonitor, &FPSMonitor::framesUpdated,
            fpsOverlay, &FpsOverlay::addFrameSamples);
    connect(&metricsProvider, &MetricsProvider::metricsUpdated,
            fpsOverlay, &FpsOverlay::setSystemMetrics);

    if (SettingsManager::instance()
            .get(QStringLiteral("overlayEnabled"), true)
            .toBool())
        fpsOverlay->showOverlay();

    registerOverlayHotkey();

    metricsProvider.start(500);

    // Фаза E: подсказка, если NVML недоступен (initNvml() уже отработал
    // внутри start() — GPU-метрики будут «—»).
    if (!metricsProvider.nvmlAvailable() && monitorPage)
        monitorPage->setStatusText(
            tr("NVML не найден: температура и загрузка GPU будут «—» "
               "(нужна карта NVIDIA с установленным драйвером)."));
}

void MainWindow::registerOverlayHotkey()
{
#ifdef Q_OS_WIN
    if (!m_hotkeyWin)
        m_hotkeyWin = winId();
    HWND hwnd = reinterpret_cast<HWND>(m_hotkeyWin);

    // Снимаем предыдущую регистрацию (если была) перед повторной.
    UnregisterHotKey(hwnd, kOverlayHotkeyId);

    const QString preferred = SettingsManager::instance()
                                  .get(QStringLiteral("overlayHotkey"),
                                       QStringLiteral("Ctrl+Shift+F"))
                                  .toString();
    // Fallback на Ctrl+Alt+F, если основной хоткей занят (например,
    // Win+Shift+F держит Logi Options+).
    const QStringList attempts{preferred, QStringLiteral("Ctrl+Alt+F")};

    for (const QString &candidate : attempts) {
        DWORD mods = 0, vk = 0;
        if (!parseHotkey(candidate, mods, vk))
            continue;
        if (RegisterHotKey(hwnd, kOverlayHotkeyId, mods, vk)) {
            qDebug("Оверлей: хоткей %s", qPrintable(candidate));
            return;
        }
    }

    qWarning("Оверлей: не удалось зарегистрировать хоткей '%s' "
             "(занят другим приложением)",
             qPrintable(preferred));
#endif
}

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("VANGUARD — Game Launcher"));

    // Корневой виджет с топбаром и содержимым.
    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("appRoot"));
    root->setAttribute(Qt::WA_StyledBackground, true);

    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(1, 1, 1, 1);
    rootLayout->setSpacing(0);

    titleBar = new TitleBar(root);
    rootLayout->addWidget(titleBar);

    auto *body = new QWidget(root);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    sidebar = new Sidebar(body);
    bodyLayout->addWidget(sidebar);

    pages = new QStackedWidget(body);
    pages->setObjectName(QStringLiteral("pages"));

    libraryPage = new LibraryPage(pages);
    monitorPage = new MonitorPage(pages);
    optimizePage = new OptimizePage(pages);

    pages->addWidget(libraryPage);  // index 0
    pages->addWidget(monitorPage);  // index 1
    pages->addWidget(optimizePage); // index 2

    bodyLayout->addWidget(pages, 1);
    rootLayout->addWidget(body, 1);

    setCentralWidget(root);

    // Навигация сайдбара <-> страницы.
    sidebar->attach(pages);

    connect(sidebar, &Sidebar::refreshRequested,
            this, &MainWindow::refreshGameList);
    connect(sidebar, &Sidebar::settingsRequested,
            this, &MainWindow::openSettings);

    connect(libraryPage, &LibraryPage::launchGameRequested,
            this, &MainWindow::launchGame);
    connect(libraryPage, &LibraryPage::refreshRequested,
            this, &MainWindow::refreshGameList);
    connect(libraryPage, &LibraryPage::addGameRequested,
            this, &MainWindow::addGame);
    connect(libraryPage, &LibraryPage::editGameRequested,
            this, &MainWindow::editGame);
    connect(libraryPage, &LibraryPage::deleteGameRequested,
            this, &MainWindow::deleteGame);

    // Обложка скачана/извлечена — подставляем на карточку.
    connect(&CoverCache::instance(), &CoverCache::coverReady, this,
            [this](const QString &key, const QString &path) {
                if (!path.isEmpty() && libraryPage)
                    libraryPage->setCover(key, path);
            });

    connect(monitorPage, &MonitorPage::toggleMonitoringRequested,
            this, &MainWindow::toggleMonitoring);

    connect(optimizePage, &OptimizePage::optimizeRequested,
            this, &MainWindow::runOptimize);
    connect(optimizePage, &OptimizePage::restoreRequested,
            this, &MainWindow::restoreProcesses);

    // Анимация смены страницы: opacity 0 -> 1 после установки индекса.
    connect(pages, &QStackedWidget::currentChanged, this, [this](int index) {
        Q_UNUSED(index)
        switchPage(index, true);
    });

    resize(1320, 800);
}

void MainWindow::setupWindowChrome()
{
    setWindowFlag(Qt::FramelessWindowHint, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMinimumSize(1100, 700);

    connect(titleBar, &TitleBar::minimizeRequested,
            this, &MainWindow::showMinimized);
    connect(titleBar, &TitleBar::maximizeRequested, this, [this]() {
        if (isMaximized())
            showNormal();
        else
            showMaximized();
        titleBar->setWindowMaximized(isMaximized());
    });
    connect(titleBar, &TitleBar::closeRequested, this, &MainWindow::close);
}


void MainWindow::applyTheme()
{
    // v1.0: только тёмная тема — светлая light.qss удалена из проекта,
    // styles.qss применяется всегда (и при старте, и после «Настроек»).
    QString css;
    QFile base(QStringLiteral(":/styles/styles.qss"));
    if (base.open(QFile::ReadOnly | QFile::Text))
        css = QTextStream(&base).readAll();

    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance()))
        app->setStyleSheet(css);
}

void MainWindow::switchPage(int index, bool animate)
{
    if (!pages)
        return;

    // Первый рендер (index == 0) не анимируем, иначе окно стартует с пустым
    // содержимым.
    if (!animate || m_pendingPage == index || index < 0) {
        m_pendingPage = index;
        return;
    }
    m_pendingPage = index;

    // Плавный fade-переход. Владение: Qt сам удаляет QGraphicsEffect при
    // setGraphicsEffect(nullptr)/замене — НЕ вызываем deleteLater() по нему
    // (double-free → крэш 0xC0000005). Предыдущую анимацию гасим явно,
    // иначе она продолжит писать в уже удалённый effect при быстрых кликах.
    if (m_pageFade) {
        m_pageFade->stop();
        m_pageFade->disconnect();
        m_pageFade->deleteLater();
        m_pageFade = nullptr;
    }
    if (pages->graphicsEffect())
        pages->setGraphicsEffect(nullptr);

    auto *effect = new QGraphicsOpacityEffect(pages);
    effect->setOpacity(0.0);
    pages->setGraphicsEffect(effect);

    auto *fade = new QPropertyAnimation(effect, "opacity", this);
    fade->setDuration(190);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    m_pageFade = fade;
    connect(fade, &QPropertyAnimation::finished, this, [this, effect]() {
        if (pages && pages->graphicsEffect() == effect)
            pages->setGraphicsEffect(nullptr);
        m_pageFade = nullptr;
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::refreshGameList()
{
    games = scanner.scanAll();
    if (libraryPage)
        libraryPage->setGames(games);
    requestGameCovers();

    auto &settings = SettingsManager::instance();

    // Фаза E: приветствие на первом запуске (один раз; только при пустой
    // библиотеке — если игры найдены, подсказка не нужна).
    if (!settings.get(QStringLiteral("firstRunDone"), false).toBool()) {
        settings.set(QStringLiteral("firstRunDone"), true);
        if (games.isEmpty()) {
            const QString hotkey =
                settings.get(QStringLiteral("overlayHotkey"),
                             QStringLiteral("Ctrl+Shift+F"))
                    .toString();
            QTimer::singleShot(450, this, [this, hotkey]() {
                QMessageBox::information(
                    this, tr("Добро пожаловать в VANGUARD!"),
                    tr("Библиотека пока пуста.\n\n"
                       "• «Обновить библиотеку» — найти игры Steam и Epic;\n"
                       "• «+ Добавить игру» — добавить игру вручную.\n\n"
                       "FPS-оверлей переключается хоткеем %1 "
                       "(настраивается в «Настройках» → вкладка «Оверлей»).")
                        .arg(hotkey));
            });
        }
    }

    // Фаза E: битый local_games.json — предупредить один раз.
    if (!m_localJsonWarned && LocalGamesStore::instance().hadParseError()) {
        m_localJsonWarned = true;
        const QString detail = LocalGamesStore::instance().lastError();
        QTimer::singleShot(700, this, [this, detail]() {
            QMessageBox::warning(
                this, tr("Файл локальных игр повреждён"),
                tr("%1\n\nРучные игры не будут показаны. Файл можно "
                   "починить вручную или удалить — при следующем "
                   "добавлении игры будет создан новый.")
                    .arg(detail));
        });
    }

    // Фаза E: ручное обновление не нашло игр — объяснить почему
    // (первый скан в ctor идёт без диалогов, там приветствие).
    if (m_scannedOnce && games.isEmpty()) {
        const QString steamLine =
            scanner.steamInstalled()
                ? tr("Steam установлен, но игры не найдены")
                : tr("Steam не установлен");
        const QString epicLine =
            scanner.epicInstalled()
                ? tr("Epic Games Launcher установлен, но игры не найдены")
                : tr("Epic Games Launcher не установлен");
        QMessageBox::information(
            this, tr("Обновление библиотеки"),
            tr("Игры не найдены:\n• %1\n• %2\n\nЕсли игра установлена, "
               "но не отображается — добавьте её кнопкой «+ Добавить игру».")
                .arg(steamLine, epicLine));
    }
    m_scannedOnce = true;
}

void MainWindow::requestGameCovers()
{
    // Асинхронно: Steam — CDN, Local — иконка из exe, Epic — фолбэк SVG.
    for (const GameInfo &game : games)
        CoverCache::instance().request(game);
}

void MainWindow::addGame()
{
    AddGameDialog dialog(GameInfo(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    GameInfo entry = dialog.game();
    const QString localId = LocalGamesStore::instance().add(entry);
    if (localId.isEmpty()) {
        QMessageBox::warning(
            this, tr("Добавить игру"),
            tr("Не удалось сохранить игру:\n%1")
                .arg(LocalGamesStore::instance().lastError()));
        return;
    }

    entry.localId = localId;
    // request() сам скопирует пользовательскую обложку в кеш или
    // скачает/извлечёт обложку автоматически.
    CoverCache::instance().request(entry);
    refreshGameList();
}

void MainWindow::editGame(const GameInfo &game)
{
    if (game.launcher != QStringLiteral("Local"))
        return;

    AddGameDialog dialog(game, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    GameInfo updated = dialog.game();
    if (!LocalGamesStore::instance().update(game.localId, updated)) {
        QMessageBox::warning(
            this, tr("Редактирование игры"),
            tr("Не удалось сохранить изменения:\n%1")
                .arg(LocalGamesStore::instance().lastError()));
        return;
    }

    updated.localId = game.localId;
    CoverCache::instance().request(updated);
    refreshGameList();
}

void MainWindow::deleteGame(const GameInfo &game)
{
    if (game.launcher != QStringLiteral("Local"))
        return;

    const auto answer = QMessageBox::question(
        this, tr("Удалить игру"),
        tr("Удалить «%1» из библиотеки?\nЗапись удаляется из local_games.json.")
            .arg(game.name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    if (!LocalGamesStore::instance().remove(game.localId)) {
        QMessageBox::warning(
            this, tr("Удалить игру"),
            tr("Не удалось удалить запись:\n%1")
                .arg(LocalGamesStore::instance().lastError()));
        return;
    }

    // Подчищаем кеш обложки — но только файл кеша, не исходник пользователя.
    const QString cached =
        CoverCache::instance().coverPathFor(game);
    if (!cached.isEmpty()
        && QFileInfo(cached).absolutePath()
            == CoverCache::instance().coverDir())
        QFile::remove(cached);

    refreshGameList();
}


QString MainWindow::resolveGameProcessName(const GameInfo &game) const
{
    if (game.path.isEmpty())
        return QString();

    const QFileInfo info(game.path);
    if (info.isFile())
        return info.fileName();

    if (info.isDir()) {
        // Каталог игры (Epic/Local): единственный exe — почти наверняка
        // основной процесс; их несколько — оставляем пусто, PresentMon будет
        // слушать все процессы (точное имя узнается только по манифесту).
        const QStringList exes =
            QDir(game.path).entryList({QStringLiteral("*.exe")}, QDir::Files);
        if (exes.size() == 1)
            return exes.first();
        return QString();
    }

    return QString();
}

void MainWindow::setMonitorTarget(const QString &imageName)
{
    if (monitorPage)
        monitorPage->setTargetProcess(imageName);

    // PresentMon принимает --process_name только на старте: если мониторинг
    // уже идёт, перезапускаем его с новой целью.
    if (fpsMonitor.isRunning()) {
        fpsMonitor.stop();
        fpsMonitor.setTargetProcess(imageName);
        fpsMonitor.start();

        if (monitorPage) {
            monitorPage->setMonitoringActive(fpsMonitor.isRunning());
            monitorPage->setStatusText(
                imageName.isEmpty()
                    ? tr("Мониторинг запущен для всех процессов (игра запущена).")
                    : tr("Цель мониторинга: %1 (из игры)").arg(imageName));
        }
    }
}

void MainWindow::launchGame(const GameInfo &game)
{
    if (game.launcher.isEmpty())
        return;

    // Персональный профиль оверлея для этой игры (overlayProfileBindings).
    if (fpsOverlay)
        fpsOverlay->applyProfileForGame(game.name);

    // Настройка autoOptimize: подчищаем систему перед стартом игры.
    applyAutoOptimize();

    if (game.launcher == QStringLiteral("Steam")) {
        if (game.appId.isEmpty()) {
            QMessageBox::warning(this, tr("VANGUARD"),
                                 tr("Не удалось определить AppID для игры \"%1\".")
                                     .arg(game.name));
            return;
        }
        if (!QDesktopServices::openUrl(QUrl(QStringLiteral("steam://rungameid/")
                                             + game.appId)))
            QMessageBox::warning(this, tr("VANGUARD"),
                                 tr("Не удалось запустить Steam для \"%1\".")
                                     .arg(game.name));
        setMonitorTarget(resolveGameProcessName(game));
        return;
    }

    if (game.path.isEmpty()) {
        QMessageBox::warning(this, tr("VANGUARD"),
                             tr("Путь к игре \"%1\" не задан.").arg(game.name));
        return;
    }

    const QFileInfo pathInfo(game.path);

    if (pathInfo.isDir()) {
        // Каталог игры: ищем главный exe по имени установщика.
        const QStringList exes = QDir(game.path).entryList(
            {QStringLiteral("*.exe")}, QDir::Files, QDir::Name);
        if (exes.isEmpty()) {
            QMessageBox::warning(this, tr("VANGUARD"),
                                 tr("В папке игры нет exe:\n%1").arg(game.path));
            return;
        }
        setMonitorTarget(exes.first());
        if (!QProcess::startDetached(QDir(game.path).filePath(exes.first()), {}))
            QMessageBox::critical(this, tr("VANGUARD"),
                                  tr("Не удалось запустить \"%1\".")
                                      .arg(game.name));
        return;
    }

    if (!pathInfo.isFile()) {
        QMessageBox::warning(this, tr("VANGUARD"),
                             tr("Путь к игре \"%1\" не найден.\n%2")
                                 .arg(game.name, game.path));
        return;
    }

    setMonitorTarget(pathInfo.fileName());

    if (!QProcess::startDetached(pathInfo.absoluteFilePath(), {}))
        QMessageBox::critical(this, tr("VANGUARD"),
                              tr("Не удалось запустить \"%1\".\n%2")
                                  .arg(game.name, game.path));
}

void MainWindow::runOptimize()
{
    SettingsManager &settings = SettingsManager::instance();

    // Настройки категорий: по умолчанию все включены (см. TODO/SettingsDialog).
    const bool closeBrowsers =
        settings.get(QStringLiteral("closeBrowsersOnOptimize"), true).toBool();
    const bool closeMessengers =
        settings.get(QStringLiteral("closeMessengersOnOptimize"), true).toBool();
    const bool closeTorrents =
        settings.get(QStringLiteral("closeTorrentsOnOptimize"), true).toBool();
    const bool closeHeavy =
        settings.get(QStringLiteral("closeHeavyOnOptimize"), true).toBool();

    OptimizeOptions options;
    options.closeBrowsers = closeBrowsers;
    options.closeMessengers = closeMessengers;
    options.closeTorrents = closeTorrents;
    options.closeHeavyweights = closeHeavy;
    options.trimWorkingSet =
        settings.get(QStringLiteral("optimizeTrimRam"), true).toBool();
    options.setHighPerformancePlan =
        settings.get(QStringLiteral("optimizePowerPlan"), true).toBool();

    const OptimizeReport report = optimizer.optimizeBeforeGame(options);

    // Сразу показываем отчёт на странице Оптимизация.
    if (optimizePage)
        optimizePage->showReport(report);

    sidebar->setCurrentPage(Sidebar::OptimizePage);
    pages->setCurrentIndex(Sidebar::OptimizePage);
}

void MainWindow::applyAutoOptimize()
{
    if (!SettingsManager::instance()
             .get(QStringLiteral("autoOptimize"), false)
             .toBool())
        return;

    // Без диалога — не прерываем запуск игры.
    runOptimize();
}

void MainWindow::restoreProcesses()
{
    if (!optimizer.hasClosedProcesses() && !optimizer.powerPlanChanged()) {
        if (optimizePage)
            optimizePage->showReport(OptimizeReport{});
        QMessageBox::information(this, tr("Восстановление"),
                                 tr("Нет закрытых процессов для восстановления."));
        return;
    }

    optimizer.restoreAfterGame();

    if (optimizePage)
        optimizePage->showReport(OptimizeReport{});

    QMessageBox::information(this, tr("Восстановление"),
                             tr("Закрытые процессы перезапущены, "
                                "схема питания восстановлена."));
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(this);
    // «Применить» / OK: оверлей перестраивается и хоткей перерегистрируется
    // без перезапуска и без закрытия диалога.
    connect(&dialog, &SettingsDialog::overlaySettingsApplied, this, [this]() {
        if (fpsOverlay)
            fpsOverlay->loadSettings();
        registerOverlayHotkey();
    });
    if (dialog.exec() != QDialog::Accepted)
        return;

    applyTheme();
    fpsMonitor.setPresentMonPath(
        SettingsManager::instance().get(QStringLiteral("presentMonPath")).toString());

    // Если мониторинг активен — подхватываем новый путь/цель.
    if (fpsMonitor.isRunning()) {
        fpsMonitor.stop();
        toggleMonitoring();
    }
}

void MainWindow::toggleMonitoring()
{
    if (fpsMonitor.isRunning()) {
        fpsMonitor.stop();
        if (monitorPage) {
            monitorPage->setMonitoringActive(false);
            monitorPage->setStatusText(tr("Мониторинг остановлен."));
        }
        return;
    }

    fpsMonitor.setPresentMonPath(
        SettingsManager::instance().get(QStringLiteral("presentMonPath")).toString());
    fpsMonitor.setRestartAsAdmin(
        SettingsManager::instance()
            .get(QStringLiteral("presentMonRestartAsAdmin"), false)
            .toBool());
    fpsMonitor.setTargetProcess(monitorPage ? monitorPage->targetProcess()
                                             : QString());

    fpsMonitor.start();

    if (monitorPage) {
        monitorPage->setMonitoringActive(fpsMonitor.isRunning());
        if (fpsMonitor.isRunning()) {
            monitorPage->setStatusText(
                fpsMonitor.targetProcess().isEmpty()
                    ? tr("Мониторинг запущен для всех процессов.")
                    : tr("Мониторинг процесса: %1")
                          .arg(fpsMonitor.targetProcess()));
        } else {
            monitorPage->setStatusText(fpsMonitor.lastError());
        }
    }
}

void MainWindow::updateFps(int fps)
{
    if (!monitorPage)
        return;

    monitorPage->setFps(fps);
    monitorPage->setMonitoringActive(fpsMonitor.isRunning());
    // 1% / 0.1% low приходят отдельным сигналом percentilesUpdated.
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message,
                             qintptr *result)
{
    Q_UNUSED(eventType)

#ifdef Q_OS_WIN
    // WM_NCHITTEST — возврат зон ресайза для frameless-окна.
    // WM_NCHITTEST (0x0084) определён в <winuser.h>.
    auto *msg = static_cast<MSG *>(message);

    // WM_HOTKEY (0x0312) — глобальный хоткей переключения оверлея
    // (фаза C3, дефолт Ctrl+Shift+F). wParam — ID зарегистрированного хоткея.
    if (msg && msg->message == WM_HOTKEY
        && static_cast<int>(msg->wParam) == kOverlayHotkeyId) {
        if (fpsOverlay)
            fpsOverlay->toggle();
        *result = 0;
        return true;
    }

    if (msg && msg->message == WM_NCHITTEST && !isMaximized()) {
        const QRect bounds = frameGeometry();
        const QPoint pos = QCursor::pos() - bounds.topLeft();

        constexpr int border = 8;
        const bool left = pos.x() <= border;
        const bool right = pos.x() >= bounds.width() - border;
        const bool top = pos.y() <= border;
        const bool bottom = pos.y() >= bounds.height() - border;

        if (top && left) {
            *result = 13; // HTTOPLEFT
            return true;
        }
        if (top && right) {
            *result = 14; // HTTOPRIGHT
            return true;
        }
        if (bottom && left) {
            *result = 16; // HTBOTTOMLEFT
            return true;
        }
        if (bottom && right) {
            *result = 17; // HTBOTTOMRIGHT
            return true;
        }
        if (top) {
            *result = 12; // HTTOP
            return true;
        }
        if (bottom) {
            *result = 15; // HTBOTTOM
            return true;
        }
        if (left) {
            *result = 10; // HTLEFT
            return true;
        }
        if (right) {
            *result = 11; // HTRIGHT
            return true;
        }
    }
#endif

    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // Пересчитываем сетку карточек под новую ширину.
    if (libraryPage)
        libraryPage->reflow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (fpsMonitor.isRunning())
        fpsMonitor.stop();

    // Оверлей — отдельное окно: прячем, иначе quitOnLastWindowClosed
    // не завершит приложение после закрытия главного окна.
    if (fpsOverlay)
        fpsOverlay->hide();

    // Область действия оптимизации — сессия лаунчера: при выходе
    // возвращаем процессы и схему питания, закрытые/сменённые перед игрой.
    optimizer.restoreAfterGame();

    // Фаза E: состояние между запусками — геометрия окна (включая
    // «развёрнуто»), последняя открытая страница, активный профиль оверлея.
    SettingsManager::instance().set(QStringLiteral("windowGeometry"),
                                    saveGeometry());
    if (pages)
        SettingsManager::instance().set(QStringLiteral("lastPage"),
                                        pages->currentIndex());
    if (fpsOverlay)
        SettingsManager::instance().set(QStringLiteral("overlayActiveProfile"),
                                        fpsOverlay->activeProfile());

    SettingsManager::instance().save();
    QMainWindow::closeEvent(event);
}
