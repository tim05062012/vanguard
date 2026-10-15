# TODO

> **Статус 15.10.2026**: фазы A, B, C, D готовы; **фаза E реализована и
> проверена живьём юзером** (сохранение geometry/lastPage/активного профиля
> оверлея, first-run приветствие, диагностика пустой библиотеки Steam/Epic,
> битый settings.json → копия .bad + диалог, битый local_games.json →
> диалог, подсказка при отсутствии NVML). **Светлая тема удалена полностью**
> (combo темы убран, ключ `theme` не используется, `light.qss` удалён из
> проекта) — подтверждено: тёмная тема применяется всегда. Осталось:
> installer.iss ждёт Inno Setup; `library.png` в docs/screenshots НЕ найден
> (ссылки в README/ARCHITECTURE вставлены — файл нужно сохранить).

## Реализовано
- [x] GameScanner: Steam (appmanifest_*.acf, libraryfolders.vdf) — проверено на машине с Steam (Mindustry найден)
- [x] GameScanner: Epic Games (Manifests/*.item, QJsonDocument)
- [x] GameScanner: Local (local_games.json) — поиск в build/, корне проекта, cwd и AppData
- [x] GameScanner: case-insensitive дедупликация библиотек Steam (убраны дубли игр)
- [x] Optimizer: optimizeBeforeGame() (taskkill для браузеров/мессенджеров)
- [x] Optimizer: restoreAfterGame() — сохранение пути exe закрытых процессов и перезапуск
- [x] FPSMonitor: интеграция с PresentMon (CSV из stdout, колонка MsBetweenPresents)
- [x] SettingsManager: singleton, settings.json в QStandardPaths::AppConfigLocation
      (autoOptimize, closeBrowsersOnOptimize, presentMonPath)
- [x] Стили через Qt-ресурсы (:/styles/styles.qss; светлая тема light.qss
      удалена перед v1.0 — только тёмная)
- [x] Запуск игр из UI (двойной клик: Steam URI / QProcess для Epic и Local)
- [x] Пустой список: "Игр не найдено. Установите Steam или добавьте игры вручную."
- [x] Sidebar: Library / Optimize / Restore / Monitor / Settings — все кнопки подключены
- [x] Диалог настроек SettingsDialog (тема, autoOptimize, closeBrowsers, путь к PresentMon)
- [x] Панель Monitor: цель процесса, крупный FPS, старт/стоп мониторинга
- [x] Настройки autoOptimize и closeBrowsersOnOptimize реально применяются
- [x] windeployqt: цель CMake `deploy` (`cmake --build build --target deploy`)
- [x] README/TODO

## Осталось (требуется внешнее окружение или доработка)
- [x] PresentMon авто-детект (фаза B v1.0): findPresentMon() по маске PresentMon*.exe
      (настройки → каталог exe → tools/ → ../tools → PATH → Program Files),
      выбор максимальной версии из версионных имён; «Обзор…» и метка «Найден: …»
      в настройках; кнопка «Скачать PresentMon» (GitHub releases); чекбокс
      --restart_as_admin; авто-подстановка --process_name под игру
      (setMonitorTarget, эвристика «единственный exe в каталоге»).
- [x] Перцентили 1% / 0.1% low + график frame time (фаза B v1.0): FPSMonitor
      хранит историю кадров за 60 с, считает low по худшим 1%/0.1%,
      батчит кадры в FrameTimeGraph через framesUpdated.
- [!] Живой прогон PresentMon 2.6.0: --output_stdout и --no_csv ВЗАИМОИСКЛЮЧАЮЩИ
      (убрано из аргументов); ETW-трассировка требует elevation — запускать
      VANGUARD от администратора для проверки FPS (группа «Performance Log
      Users» на этой машине отсутствует).
- [ ] Проверить scanEpic() на машине с Epic Games Launcher
- [ ] Протестировать запуск реальной игры (двойной клик)
- [ ] Отслеживание завершения игры: сейчас restoreAfterGame() вызывается при выходе
      из VANGUARD, а не при завершении процесса игры
- [x] Обложки/иконки игр (фаза A v1.0): CoverCache — Steam CDN (header.jpg,
      3 хоста + библиотека как fallback), Local — иконка из exe (ExtractIconExW →
      DIB → PNG), кеш в covers/ (dev) / %LOCALAPPDATA%\VANGUARD\covers
      (установка) / coversDir из настроек. Epic: фолбэк SVG, публичного CDN нет.
      GameCard показывает обложку, есть контекстное меню Edit/Delete.
- [x] Добавление игр вручную (фаза A v1.0): AddGameDialog (имя, exe, рабочая
      папка, лаунчер, AppID, обложка), LocalGamesStore (CRUD над local_games.json,
      атомарная запись + .bak), кнопка «+ Добавить игру» на LibraryPage
- [x] Дополнительные метрики GPU (NVML): загрузка, температура, память
      (+ частота, мощность) — `MetricsProvider::pollGpu()` через динамическую
      загрузку nvml.dll; без карты NVIDIA — деградация в «—», фаза C1
- [x] Системные метрики CPU/RAM/сеть/диск/температура (фаза C1): MetricsProvider
      (GetSystemTimes, CallNtPowerInformation, GlobalMemoryStatusEx, GetIfTable2,
      PDH, async PowerShell WMI) → MonitorPage::setSystemMetrics
- [x] FpsOverlay (фаза C2): отдельное top-layer-окно (WS_EX_LAYERED/TRANSPARENT/
      TOOLWINDOW/NOACTIVATE), 9 якорей по монитору курсора, размеры S/M/L,
      прозрачность, FPS + 1%/0.1% low, график frame time 10 с, строки
      CPU/GPU/RAM/NET/DISK, профили на игру через SettingsManager;
      click-through вживую ещё не проверен (C5)
- [x] Глобальный хоткей оверлея (фаза C3): `RegisterHotKey(hwnd главного
      окна, MOD_CONTROL|MOD_SHIFT, 'F')`, `WM_HOTKEY` в `MainWindow::nativeEvent`,
      fallback Ctrl+Alt+F при занятости, `overlayHotkey` в настройках,
      пере-регистрация при Apply, снятие в деструкторе; проверено живьём
- [x] Настройки оверлея (фаза C4): SettingsDialog → QTabWidget
      («Основные» / «Оверлей»): показ, монитор (Основной/Вторичный/Активный/Оба),
      9 якорей, S/M/L, слайдер прозрачности (дефолт 85 %), 8 чекбоксов метрик,
      поле хоткея, кнопка «Применить» (signal `overlaySettingsApplied` →
      `loadSettings()` + `registerOverlayHotkey()` без перезапуска);
      FpsOverlay: выбор монитора (primary/secondary/active/both через
      MonitorFromWindow-эвристику), зеркало-дублёр на 2-м мониторе при «Оба»,
      авто-высота панели по включённому контенту (согласована с paintEvent)
