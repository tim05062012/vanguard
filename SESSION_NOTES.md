# SESSION_NOTES

Дата: 15.10.2026 · Ветка работ: v1.0 — фазы A и B готовы, фаза C в работе

## Состояние на конец сессии (для следующего чата)

### Сборка / среда
- Последняя зелёная сборка: 15.10.2026 0:54, `[100%] Built target VANGUARD`
  (`$env:PATH="C:\msys64\ucrt64\bin;C:\Program Files\CMake\bin;$env:PATH"; cmake --build build`).
  В сборке фазы A+B; `build\VANGUARD.exe` стартует, stderr пуст.
- `CMakeLists.txt`: `project(VANGUARD VERSION 0.2.0)` — **1.0.0 поднять в фазе D**;
  Qt6: `Widgets Svg Network`; deploy-цель не менялась.
- Среда: Inno Setup **не установлен**; `windres.exe` и `qico.dll` есть;
  `nvml.dll` в System32 (RTX 5080 Laptop); PresentMon 2.6.0 ETW требует
  elevation (группы «Performance Log Users» на машине нет).
- `TitleBar` всё ещё пишет «v0.2» — поднять в фазе D с версией.
- Мелочь: ниже секция «Фаза B» идёт перед «Фаза A» — при следующем отчёте
  переставить (на код не влияет).

### Готово (детали — в секциях фаз ниже)
- **Фаза A — игры + обложки**: GameInfo (localId/workingDir/coverPath/addedAt),
  LocalGamesStore (CRUD, атомарная запись + .bak, localId из md5), AddGameDialog,
  CoverCache (Steam CDN 4 хоста / иконка из exe / Epic-фолбэк; кеш:
  coversDir → `<project>/covers` → `%LOCALAPPDATA%\VANGUARD\covers`),
  кнопка «+ Добавить игру», GameCard с обложкой и меню Edit/Delete (только Local),
  дедуп ручных записей по (launcher, appId). Живьём: `covers/steam_1127400.jpg`
  (460×215 скачан), `covers/local-<md5>.png` (32×32 из notepad.exe).
- **Фаза B — PresentMon + перцентили**: findPresentMon() по маске PresentMon*.exe
  (выбор макс. версии — чинит баг v0.2), SettingsDialog: Обзор/статус/«Скачать»/
  --restart_as_admin, setMonitorTarget() (авто --process_name под игру + перезапуск
  мониторинга; эвристика «единственный exe в каталоге»), перцентили 1%/0.1% low
  (окно 60 с, худшие кадры) + батч framesUpdated → FrameTimeGraph::addSamples.
  Важно: у PresentMon 2.6.0 `--output_stdout` и `--no_csv` взаимоисключающи
  (`--no_csv` убран из аргументов FPSMonitor).

### В работе — фаза C (FPS-оверлей)
- **C2 `FpsOverlay` — ГОТОВ (15.10.2026)**: отдельное top-level окно поверх всего:
  - `include/FpsOverlay.h` + `src/FpsOverlay.cpp` (добавлены в `CMakeLists.txt`):
    `Qt::FramelessWindowHint|WindowStaysOnTopHint|Tool`; native-стили в
    `showEvent` (`SetWindowLongPtrW`: WS_EX_LAYERED | WS_EX_TRANSPARENT
    (click-through) | WS_EX_TOOLWINDOW (нет в taskbar) | WS_EX_NOACTIVATE);
    `reposition()` — 9 якорей по `screenAt(cursor)` с 14px отступом от
    availableGeometry; paintEvent мгновенный: скругл. фон #0A0E14, cyan-бордер,
    FPS-заголовок + 1%/0.1% low, график frame time 10 с (сетка 16.7/33.4 мс,
    градиентная заливка), строки CPU/GPU/RAM/NET/DISK; размеры S/M/L
    (260×140 / 330×215 / 360×280), прозрачность, static anchorKeys/sizeKeys.
  - MainWindow: член `fpsOverlay`, создание в ctor (гейт `overlayEnabled`),
    связи: `fpsMonitor::fpsUpdated(int)`→setFps, `lowStatsUpdated`→setLowStats,
    `framesUpdated`→addFrameSamples, `metricsProvider::metricsUpdated`→
    setSystemMetrics; `applyProfileForGame(game.name)` при запуске игры;
    сброс FPS при остановке мониторинга; `hide()` в `closeEvent` (иначе
    `quitOnLastWindowClosed` не сработает — оверлей «последнее окно»);
    `delete` в dtor (окно с parent=nullptr!).
  - Профили оверлея — `SettingsManager` (base + applyProfileForGame по имени).
  - **Двойной монитор/DPR:** геометрия Qt уже в логических px (dpr 1.5 на левом
    2560×1600@150%), `move()` в логических — физически панель над таскбаром.
    «Обрезка» на скриншотах — артефакт `CopyFromScreen` (bitmap высотой 1440
    при физической 1600); проверено захватом области ниже — панель целая.
  - Живьём: `build/c2_final2.png` — панель над таскбаром, CPU/GPU/RAM тикают
    (GPU 40°C / 1.7GB NVML), cyan-бордер, скругления на месте.
  - **Не проверено:** click-through вживую (прокрутка под панелью) — флаги
    выставлены, визуального теста не делалось; хоткей — фаза C3.
- **C1 `MetricsProvider` — ГОТОВ (15.10.2026), собирается и запускается**:
  - `include/MetricsProvider.h` — без изменений (struct SystemMetrics: cpuLoad/
    cpuTemp/cpuClock, ram*, gpuLoad/gpuTemp/vram*/gpuClock/gpuPowerW,
    netDown/UpKbs, diskRead/WriteMbs; start/stop/poll, сигнал metricsUpdated).
  - `src/MetricsProvider.cpp` — дописан целиком, маркер `__APPEND2__` убран:
    `pollNetwork()` — GetIfTable2, дельты октетов по всем интерфейсам кроме
    loopback (type 24), первый тик — база, период берётся из `m_timer->interval()`;
    `pollDisk()` — PDH, static-хэндлы query/счётчиков, `PdhAddEnglishCounterW
    "\\PhysicalDisk(_Total)\\Disk {Read,Write} Bytes/sec"`, база по первому
    collect, при неудаче `initFailed` (повторов нет);
    `pollCpuTempAsync()` — async PowerShell раз в 3 с (`Get-CimInstance
    -Namespace root/wmi -ClassName MSAcpi_ThermalZoneTemperature`),
    °C = CurrentTemperature/10 − 273.15, при ошибке остаётся -1.
  - Исправлено: опечатка `m_prevNetPrimed` → `m_netPrimed`; удалён мёртвый
    `QRegularExpression` из `pollCpuClock()` (+ его include); добавлены
    `<cstdlib>` (std::malloc/free) и `<QDateTime>`.
  - **Ловушки MinGW ucrt64 (запомнить, больше не наступать)**:
    1. `PROCESSOR_POWER_INFORMATION` в MinGW отсутствует — объявлен локально
       как `PpiInfo` (6×ULONG, static_assert layout) в анонимном namespace;
    2. `CallNtPowerInformation` MinGW держит в `<powerbase.h>`, не в powrprof.h;
    3. `MIB_IF_ROW2` / `GetIfTable2` в netioapi.h спрятаны под `#ifdef _WS2IPDEF_`:
       включать `<ws2tcpip.h>` ДО `<windows.h>`; сам netioapi.h подтянет
       iphlpapi.h, но только при `NTDDI_VERSION >= Vista`, поэтому в начале .cpp
       (до всех include) `#define _WIN32_WINNT 0x0A00` и
       `#define NTDDI_VERSION 0x0A000000`;
    4. MinGW не линкует по pragma comment — в CMakeLists добавлено
       `target_link_libraries(VANGUARD PRIVATE iphlpapi pdh powrprof)`.
  - CMakeLists: `src/MetricsProvider.cpp` + `include/MetricsProvider.h` добавлены
    в `qt_add_executable`.
  - `MainWindow`: член `MetricsProvider metricsProvider`; `connect(metricsUpdated)`
    → форматирование (`-1` → «—», пары used/total «12.4 / 32.0 GB») →
    `MonitorPage::setSystemMetrics(...)`; `metricsProvider.start(500)` в конце
    ctor. Проверено: сборка чистая, окно «VANGUARD — Game Launcher» открывается,
    процесс жив ~9 с (включая тик PowerShell), закрытие штатное.
- **C2 — не начат**: `include/FpsOverlay.h` + `src/FpsOverlay.cpp` — QWidget:
  флаги `Qt::Tool|FramelessWindowHint|WindowStaysOnTopHint|
  WindowTransparentForInput|WindowDoesNotAcceptFocus`, атрибуты
  WA_TranslucentBackground/WA_ShowWithoutActivating/NoDropShadowWindowHint;
  после show() через winId() выставить `WS_EX_LAYERED|WS_EX_TRANSPARENT|
  WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE` + SetWindowPos(HWND_TOPMOST, SWP_NOACTIVATE);
  paintEvent: rounded rect, чёрный фон с alpha, 1px циановая рамка, метрики
  (Consolas), спарклайны; 9 якорей от availableGeometry, S/M/L, прозрачность;
  данные от FPSMonitor (fpsUpdated/percentilesUpdated/framesUpdated) и MetricsProvider.
- **C3 — готово (15.10.2026)**: глобальный хоткей toggle оверлея.
  `MainWindow::registerOverlayHotkey()`: `RegisterHotKey(hwnd главного окна,
  kOverlayHotkeyId=1, MOD_CONTROL|MOD_SHIFT, 'F')` (дефолт `overlayHotkey`
  из SettingsManager), при FALSE — fallback `Ctrl+Alt+F`; парсер
  `parseHotkey("Ctrl+Shift+F")` (Ctrl/Alt/Shift/Win + A–Z/0–9/F1–F12);
  `WM_HOTKEY` в `nativeEvent` (до WM_NCHITTEST) → `fpsOverlay->toggle()`;
  `UnregisterHotKey` в деструкторе (хранится `WId m_hotkeyWin`).
  **Питалы**: (1) Win+Shift+F в системе занят **Logi Options+**
  (RegisterHotKey → 1409) — дефолт Ctrl+Shift+F; (2) injected-ввод
  (SendInput/keybd_event) НЕ триггерит RegisterHotKey на этой Win11 —
  автоматический self-test хоткея невозможен, проверять только живым
  нажатием; (3) qDebug у GUI-процесса не виден без redirect stderr —
  для диагностики работал файловый лог, потом удалён.
- **C4 — готово (15.10.2026)**: вкладка «Оверлей» + Apply на лету.
  SettingsDialog → QTabWidget («Основные» = прежняя форма; «Оверлей»):
  `overlayEnabled`, монитор (`overlayMonitor`: primary/secondary/active/both —
  «Активный» = MonitorFromWindow(GetForegroundWindow()) → центр окна в физ
  rect экранов, «Оба» = зеркало-дублёр FpsOverlay на screens[1]), 9 якорей
  (`overlayAnchor`), S/M/L (`overlaySize`), слайдер прозрачности
  (`overlayOpacity`, дефолт 85), 8 чекбоксов метрик (`overlayShowFps/Lows/
  Graph/Gpu/Cpu/Ram/Net/Disk`), поле хоткея (`overlayHotkey`).
  Кнопки Ok|Apply|Cancel; Apply → `saveToSettings()` + signal
  `overlaySettingsApplied` → в MainWindow: `fpsOverlay->loadSettings()` +
  `registerOverlayHotkey()` (без перезапуска и закрытия диалога).
  FpsOverlay: авто-высота панели по контенту (sizeFor согласован с paintEvent:
  pad=12, зазор 6, rowH=16, header = fpsFont*1.25, graph S/M/L = 64/90/112 —
  проверено численно: M всё вкл → 330×246; L без Net/Disk → 420×243);
  зеркало создаётся в публичном ctor (`FpsOverlay(bool, QWidget*)` — приватный),
  конфиг/данные синхронизируются, удаляется в dtor.
  Проверено числовым замером GetWindowRect: anchor=topLeft → (14,14),
  monitor=secondary → L=-2546 (левый монитор), размеры по формуле совпали.
  **Питалы**: `read_files` отдаёт устаревший кэш после правок (`outdated…`) —
  верить `Get-Content`/`Select-String` при точечных заменах.
- **C5 — частично (15.10.2026), исправлено 2 бага**:
  (1) **Крэш 0xC0000005 при переключении вкладок** — в `switchPage`
  `finished`-колбэк делал `pages->setGraphicsEffect(nullptr)` (Qt сам
  удаляет эффект — владение) и тут же `effect->deleteLater()` → double-free;
  плюс при быстрых кликах старый `QPropertyAnimation` продолжал писать
  в удалённый effect. Фикс: член `m_pageFade` — явный stop/disconnect/
  deleteLater предыдущей анимации перед новой, effect освобождается только
  через `setGraphicsEffect(nullptr)`. Проверено живьём (быстрые клики ок).
  (2) **Наложение метрик на странице «Мониторинг»**: `addRow` складывал
  CPU/TEMP/CLOCK (и RAM/GPU/GPU TEMP) в ОДНУ ячейку QGridLayout
  (row,0)/(row,1) — визуальный каш. Фикс: `addCell(row, col)` — таблица
  3×3, пары caption/value в отдельных колонках. Проверено скриншотом.
  Подзаголовок страницы: «Win+Shift+F» → «по умолчанию Ctrl+Shift+F».
  Осталось живьём: click-through над другим окном, «Применить» на лету,
  хоткей при фокусе в другом окне, метрики при запущенном мониторинге.
  *(позднее подтверждено пользователем: click-through/хоткей/Apply/метрики ок; NVML в оверлее живой)*
- **D — готово (15.10.2026): упаковка v1.0.0**:
  - `project(VANGUARD VERSION 1.0.0)`, TitleBar «GAME LAUNCHER // v1.0»,
    Sidebar «v1.0.0».
  - **Манифест**: `resources/app.manifest` (requireAdministrator — PresentMon/ETW,
    supportedOS Win10/11, longPathAware, PerMonitorV2) + `resources/app.rc`
    (манифест `1 24`, `IDI_APPICON ICON app.ico`, VERSIONINFO 1.0.0.0),
    подключён в qt_add_executable, `enable_language(RC)` (windres).
    **Проверено**: в exe есть RT_MANIFEST (requireAdministrator)/RT_ICON/
    RT_GROUP_ICON/RT_VERSION. Qt-шный автo-`VANGUARD.exe.manifest` в линковку
    не попадает (без конфликта). ВНИМАНИЕ: теперь каждый запуск требует UAC.
  - **app.ico** сгенерирован из logo.svg точками (GDI+,7 размеров PNG-in-ICO:
    16–256, циан #00E5FF). PowerShell: DrawPolygon/DrawLines требуют
    `List[PointF].ToArray()` (иначе ошибка приведения).
  - **deploy**: windeployqt копирует Qt/плагины, НО НЕ транзитивные MSYS2-
    зависимости Qt (zlib1, libicu*, libharfbuzz, libpng, libjpeg, libbrotli*,
    libmd4c, libfreetype, libglib* и т.д. — ~25 DLL, аудит objdump по
    обходу build/** → «ALL RESOLVED, 48 modules»). Явный список
    `MINGW_RUNTIME_DLLS` добавлен в цель deploy (копия из каталога
    CMAKE_CXX_COMPILER). Иначе старт падал с диалогом «Системная ошибка:
    отсутствует zlib1/libbrotlidec/libzstd.dll» (особенно на elevated-запуске).
  - `installer.iss` (Inno Setup 6): Program Files, ярлыки Пуск/Рабочий стол,
    опц. автозапуск (HKCU Run), деинсталлятор, `SetupIconFile=app.ico`,
    covers/ НЕ копируется, сборка `ISCC.exe installer.iss → dist/`.
    **Inno Setup на машине НЕ установлен** — файл не проверен компиляцией.
  - README.md переписан под v1.0 (возможности/установка/использование/FAQ),
    docs/ARCHITECTURE.md обновлён; скриншоты `docs/screenshots/main.png`
    (v1.0.0 в титуле) и `overlay.png` (живые метрики: CPU/GPU-NVML/RAM/NET/DISK).
  - Скриншоты по новому регламенту: пользователь снимает сам (Win+Shift+S),
    агент НЕ читает картинки в контекст — только `dir` + текстовые ссылки
    `![...](docs/screenshots/...)` в README/ARCHITECTURE. Вставлены ссылки на
    `library.png` / `overlay.png` / `settings_overlay.png`; **library.png
    пользователь ещё не сохранил** (в папке только main/overlay/settings_overlay).
- **E — реализовано (15.10.2026), сборка зелёная, живой прогон за юзером**:
  - Сохранение между запусками (closeEvent → SettingsManager): `windowGeometry`
    (saveGeometry включает maximized), `lastPage` (восстановление в ctor,
    fade-анимация при старте допустима — окно ещё не показано),
    `overlayActiveProfile` (после loadSettings(), который его сбрасывает).
  - First-run: ключ `firstRunDone`, при пустой библиотеке — приветствие
    (QTimer 450 мс, хоткей из настроек); при ручном «Обновить библиотеку»
    с пустым результатом — диагноз `GameScanner::steamInstalled()` /
    `epicInstalled()` (реестр Steam / каталог Manifests Epic).
  - Битый `settings.json`: `SettingsManager::lastLoadFailed()` + копия
    `settings.json.bad` (save не затирает данные молча) + диалог в ctor.
  - Битый `local_games.json`: `hadParseError()` → одноразовый диалог
    (`m_localJsonWarned`).
  - NVML: подсказка в статус MonitorPage при `!nvmlAvailable()`
    (initNvml() отрабатывает в `MetricsProvider::start()` — проверено по коду).
  - Не делалось (осознанно): PresentMon-ошибки уже были в статусе
    MonitorPage (lastError с инструкцией); CRUD-ошибки LocalGamesStore уже
    показывались через QMessageBox.
  - **Светлая тема удалена полностью** (по решению пользователя): вкладка
    SettingsDialog без combo темы (`themeCombo` убран), ключ `theme` больше
    не читается/не пишется в SettingsManager, `applyTheme()` всегда ставит
    `:/styles/styles.qss`, `resources/light.qss` удалён и убран из
    `resources.qrc`; README/ARCHITECTURE/TODO очищены от упоминаний.
- **C5**: проверка: клик проходит сквозь оверлей, хоткей при фокусе в другом
  окне; exclusive fullscreen — ограничение (задокументировать в FAQ фазы D).

### Дальше (после C)
- Фаза D: VERSION 1.0.0 + TitleBar «v1.0»; dist-цель (windeployqt --release
  --compiler-runtime + tools/ + local_games.json); app.ico из logo.svg + app.rc
  (windres) + QApplication::setWindowIcon; installer.iss; README/ARCHITECTURE;
  TODO-зачистка. Развилка: Inno Setup ставить (winget) или писать «на веру».
- Фаза E: firstRun-баннер «Синхронизируйте библиотеку», сохранение
  geometry/maximized/lastPage/профиль, сообщения об ошибках (Steam не найден,
  битый JSON, нет PresentMon, нет NVML), визуальная проверка двух тем.
- Не сделан ручной UI-тест фазы A (клики Add/Edit/Delete через диалог).

---


### Что сделано

- **`FPSMonitor::findPresentMon()` переписан**: public static, маска
  `PresentMon*.exe`, порядок — настройки → каталог exe → `tools/` → `../tools` →
  cwd/tools → PATH → Program Files; версии из имени (`-2.6.0-`) парсятся,
  выбирается максимальная. Это чинит главный баг v0.2: файл
  `tools/PresentMon-2.6.0-x64.exe` уже лежал в проекте, но поиск требовал
  строго `PresentMon.exe`.
- **SettingsDialog**: «Обзор…» к пути PresentMon, метка статуса автопоиска
  «Найден: … / не найден», кнопка «Скачать PresentMon» (GitHub latest),
  чекбокс `--restart_as_admin` (ключ `presentMonRestartAsAdmin`).
- **Авто-подстановка цели**: `MainWindow::setMonitorTarget()` — при запуске
  игры в поле цели ставится имя exe; если мониторинг уже идёт — перезапуск
  PresentMon с новым `--process_name`. `resolveGameProcessName()` для каталога
  теперь ищет единственный exe (раньше возвращал имя папки — PresentMon не
  находил процесс).
- **Перцентили low**: история кадров за 60 с (`m_history`), методика — худшие
  1% / 0.1% frame times, FPS = 1000/среднее; сигналы `percentilesUpdated` и
  `framesUpdated` (батч в `FrameTimeGraph::addSamples` — один update вместо
  сотен). `MainWindow` подключён, `setLowStats(-1,-1)`-заглушка убрана.

### Что проверено (живьём)

- Сборка + запуск после каждой подзадачи — без stderr.
- Прогон PresentMon 2.6.0 нашими аргументами: вскрыты 2 факта —
  (1) `--output_stdout` и `--no_csv` взаимоисключающи (`--no_csv` убран,
  иначе PresentMon игнорирует его и сыплет CSV-файлы); (2) ETW-трассировка
  падает с «access denied» без elevation (на машине нет группы
  «Performance Log Users») — сообщение об ошибке в коде распознаёт этот случай.

### В TODO

- Живой тест FPS на реальной игре: только с запуском VANGUARD от администратора
  (внешнее окружение, недоступно из автоматизации).
- `--terminate_on_proc_exit` подтверждён в stderr 2.6.0 — но оставляем
  собственную остановку через terminate()/kill().

---


### Что сделано

- **`GameInfo` расширен**: `localId`, `workingDir`, `coverPath`, `addedAt`
  (обратно совместимо со старым `local_games.json`).
- **`LocalGamesStore`** (новый, singleton как `SettingsManager`): CRUD над
  `local_games.json`; путь резолвится из 4 кандидатов (рядом с exe, `../`, cwd,
  AppData), запись атомарно (`tmp` + rename) с бэкапом `.bak`; старые записи без
  `localId` получают стабильный синтез из md5(имя|путь) — `qHash` сидируется на
  процесс и сломал бы кеш обложек.
- **`AddGameDialog`** (новый): имя, exe + «Обзор…», рабочая папка, лаунчер
  (Local/Steam/Epic, AppID показывается только для магазинных), обложка
  (превью + «Авто»/«Выбрать файл…»), валидация (Local — существующий файл,
  Steam — числовой AppID).
- **`CoverCache`** (новый, kill-feature): кеш `covers/{key}.jpg|png`.
  Каталог: настройка `coversDir` → dev-режим `<project>/covers` →
  `%LOCALAPPDATA%\VANGUARD\covers` → авто. Steam — CDN по цепочке из 4 URL
  (cloudflare → shared.akamai → steamcdn-a.akamaihd → library_600x900),
  валидация ответа (200, >1 КБ); Local — иконка из exe через
  `ExtractIconExW` → 32bpp DIB → `QImage(ARGB32)` → PNG; Epic — фолбэк SVG.
  `importCover()` копирует пользовательскую обложку в кеш.
- **UI**: кнопка «+ Добавить игру» в шапке `LibraryPage`; `GameCard` показывает
  обложку (120×68, маленькая иконка — целиком) и контекстное меню
  (Запустить / Редактировать / Удалить, только для Local); сигналы
  add/edit/delete проброшены до `MainWindow`; `scanAll()` дедуплицирует ручные
  записи по (launcher, appId).
- **Стили**: `#addGameBtn`, `#coverLabel`, `#coverPreview`, тёмный `QMenu`
  (+ светлые версии в light.qss).
- `Qt6::Network` подключён в CMake (нужен CoverCache).

### Что проверено (живьём)

- Сборка `[100%] Built target VANGUARD`, запуск без stderr.
- `covers/steam_1127400.jpg` (41867 байт, 460×215) — Mindustry скачан с Steam CDN.
- `covers/local-<md5>.png` (32×32) — иконка notepad.exe извлечена WinAPI.
- dev-режим: кеш пошёл в `<project>/covers`.

### В TODO

- Ручная проверка UI-флоу (диалог Add/Edit/Delete) на реальных кликах.
- Epic-обложки: публичного CDN нет — отложено в v1.1.
- Иконка из exe — 32×32 (ExtractIconEx): для 256px-иконок качество
  посредственное, но картинка не масштабируется (показ целиком).

---
## Что сделано

### 3 задачи

1. **`Optimizer.restoreAfterGame()`** — замыкание цикла оптимизации.
   - Новая структура `ClosedProcess {imageName, exePath}` и хранилище `m_closedProcesses`
     (`include/Optimizer.h`).
   - Перед `taskkill` сохраняется путь к exe через PowerShell
     `Get-Process … -ExpandProperty Path` (`processExecutablePath()` в `src/Optimizer.cpp`);
     PowerShell вызывается только если процесс реально был запущен.
   - `restoreAfterGame()` перезапускает через `QProcess::startDetached` (фолбэк на имя
     через PATH) и очищает список; `hasClosedProcesses()` / `closedProcesses()` для UI.
   - UI: кнопка **Restore** в sidebar и **Restore apps** в toolbar,
     `restoreAfterGame()` в `closeEvent()`.

2. **`FPSMonitor` + панель Monitor** — из заглушки в рабочий мониторинг.
   - `FPSMonitor` переведён в `QObject` (`Q_OBJECT`, сигналы `fpsUpdated`,
     `availabilityChanged`, `monitoringStopped`, таймер 500 мс).
   - Источник FPS — внешний Intel PresentMon, запускаемый через `QProcess`.
   - CSV парсится потоково: ищется заголовок с колонкой `MsBetweenPresents`
     (фолбэк `FrameTime`), FPS = среднее от `1000/ms` за интервал опроса.
     Разбор CSV с поддержкой кавычек (`splitCsvLine`).
   - Поиск бинаря: путь из настроек → рядом с exe → `tools/` → PATH;
     `isAvailable()` теперь реальный.
   - `src/MainWindow.cpp` + `include/MainWindow.h`: страница Monitor
     (поле имени процесса, крупный FPS, статус, старт/стоп).

3. **Диалог настроек + переключение страниц.**
   - Новое: `include/SettingsDialog.h`, `src/SettingsDialog.cpp` — тема (тёмная/светлая),
     `autoOptimize`, `closeBrowsersOnOptimize`, путь к PresentMon.
   - `MainWindow`: `QStackedWidget` (страницы Library / Monitor), подключены **все**
     5 кнопок sidebar (раньше Monitor/Settings висели без обработчиков),
     `applyTheme()` с чтением `:/styles/styles.qss` + `:/styles/light.qss`.
   - Настройки стали реально применяться: `autoOptimize` запускает оптимизацию
     при старте игры (`applyAutoOptimize()`), `closeBrowsersOnOptimize` передаётся
     в `optimizeBeforeGame(bool closeBrowsers)`.

### 2 бага (найдены при прогоне self-check)

- **Дубль игры в списке.** Дедупликация библиотек Steam была case-sensitive,
  а путь из реестра (`c:/program files (x86)/steam`) не совпадал по регистру
  с `libraryfolders.vdf` (`C:/Program Files...`) → Mindustry ×2.
  Исправлено case-insensitive сравнением в `steamLibraryFolders()`
  (`src/GameScanner.cpp`): 3 игры → 2.
- **Утечка процесса.** После `kill()` в `FPSMonitor::stop()` процесс не успевал
  быть переaped → `QProcess: Destroyed while process is still running`.
  Добавлен `waitForFinished(1000)` после `kill()` + подстраховка в деструкторе.

### Деплой / сборка

- **Починены ресурсы стилей:** `CMakeLists.txt` класть стили в `:/resources/styles.qss`,
  а код читал `:/styles/styles.qss` — стили **не применялись**. Переведено на
  `resources.qrc` + `set(CMAKE_AUTORCC ON)`; теперь алиасы совпадают
  (`:/styles/styles.qss`, `:/styles/light.qss`).
- Добавлена CMake-цель **`deploy`** (`windeployqt --no-translations`), появляется
  только если `windeployqt` найден: `cmake --build build --target deploy`.
- Новая тема `resources/light.qss`, подключена в `resources.qrc`.
- Документация: обновлены `TODO.md`, `README.md`, `docs/ARCHITECTURE.md`.

## Принятые решения

- **PresentMon, а не NVML.** NVML даёт только метрики GPU (загрузка/температура),
  но не FPS. Для FPS нужен ETW-трекинг PresentMon. Флаги браны из официального
  README (`--output_stdout --no_csv --no_console_stats --session_name VANGUARD
  --stop_existing_session`, опционально `--process_name <exe>`). Флаг
  `--terminate_on_proc_exit` **сознательно не используется** — не подтвердился
  в актуальном README, неизвестный флаг сломал бы запуск; остановку делаю сам
  (`terminate()` → `kill()`).
- **`QStandardPaths` вместо хардкода.** Было `C:/Projects/VANGUARD/...` (exe не
  работал вне папки исходников). Стало: настройки → `AppConfigLocation`
  (+ `mkpath` при записи), `local_games.json` → 4 кандидата (рядом с exe,
  `../`, cwd, `AppData`).
- **`QStackedWidget` для страниц.** Проще всего для Library/Monitor без
  смены `centralWidget`; кнопки sidebar просто переключают индекс.
- **Восстановление процессов привязано к сессии лаунчера** (`closeEvent`),
  а не к завершению игры: отслеживания процесса игры пока нет.

## Что в TODO

Требует внешнего окружения / доработки:

- Положить `PresentMon.exe` и проверить FPS на реальной игре.
- Проверить `scanEpic()` на машине с Epic Games Launcher.
- Протестировать запуск реальной игры (двойной клик).
- Отслеживание завершения игры: `restoreAfterGame()` сейчас вызывается при выходе
  из VANGUARD, а не при завершении процесса игры.
- Обложки/иконки игр.
- Дополнительные метрики GPU (NVML): загрузка, температура, память.

Уже закрыто и отмечено в `TODO.md`: `restoreAfterGame()`, интеграция PresentMon,
кнопки sidebar, `local_games.json`, deploy через windeployqt, проверка `scanSteam()`
(Mindustry найден).

## Заметки по окружению

- Сборка требует `C:\msys64\ucrt64\bin` в PATH — иначе `moc.exe` падает
  с `0xc0000135`. Генератор: `MinGW Makefiles`.
- Корневой `settings.json` больше не используется (настройки теперь в
  `%LOCALAPPDATA%\VANGUARD\settings.json`) — файл можно удалить.
