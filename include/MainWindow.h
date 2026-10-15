#pragma once

#include <QMainWindow>
#include <QCloseEvent>
#include <QLabel>
#include <QStackedWidget>

#include "GameScanner.h"
#include "Optimizer.h"
#include "FPSMonitor.h"
#include "MetricsProvider.h"

class TitleBar;
class Sidebar;
class LibraryPage;
class MonitorPage;
class OptimizePage;
class FpsOverlay;
class QPropertyAnimation;

// Корень приложения: frameless-окно с кастомным топбаром, сайдбаром
// 220px и тремя страницами (Библиотека / Мониторинг / Оптимизация).
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // (Пере)регистрация глобального хоткея переключения оверлея (C3):
    // читает SettingsManager::overlayHotkey (дефолт Ctrl+Shift+F),
    // при занятости пробует fallback Ctrl+Alt+F. Вызывается из ctor
    // и после Apply в настройках (C4).
    void registerOverlayHotkey();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message,
                     qintptr *result) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void setupWindowChrome();
    void applyTheme();
    void switchPage(int index, bool animate);
    void refreshGameList();
    void launchGame(const GameInfo &game);
    void runOptimize();
    void applyAutoOptimize();
    void restoreProcesses();

    void openSettings();
    void toggleMonitoring();
    void updateFps(int fps);

    // Управление локальными играми (v1.0): добавление, правка, удаление.
    void addGame();
    void editGame(const GameInfo &game);
    void deleteGame(const GameInfo &game);
    void requestGameCovers();

    // Авто-подстановка цели для PresentMon под запускаемую игру.
    void setMonitorTarget(const QString &imageName);

    // Резолвит имя exe процесса игры (для PresentMon и приоритета).
    QString resolveGameProcessName(const GameInfo &game) const;

    TitleBar *titleBar = nullptr;
    Sidebar *sidebar = nullptr;
    LibraryPage *libraryPage = nullptr;
    MonitorPage *monitorPage = nullptr;
    OptimizePage *optimizePage = nullptr;
    QStackedWidget *pages = nullptr;

    // HWND главного окна, которому привязан глобальный хоткей переключения
    // оверлея (фаза C3, дефолт Ctrl+Shift+F — Win+Shift+F занят Logi Options+).
    // Храним WId, чтобы UnregisterHotKey не зависел от windows.h в заголовке.
    WId m_hotkeyWin = 0;

    GameScanner scanner;
    Optimizer optimizer;
    FPSMonitor fpsMonitor;
    MetricsProvider metricsProvider; // метрики системы для MonitorPage
    FpsOverlay *fpsOverlay = nullptr; // topmost click-through оверлей (C2)
    QList<GameInfo> games;

    // Анимация fade-переключения страниц (гасится при новом переключении).
    QPropertyAnimation *m_pageFade = nullptr;

    int m_pendingPage = -1;

    // Фаза E: одноразовые подсказки/диагностика.
    bool m_scannedOnce = false;     // первый refresh — в ctor, без диалогов
    bool m_localJsonWarned = false; // битый local_games.json предупреждён
};

