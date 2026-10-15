# VANGUARD — Game Launcher v1.0

## Описание

VANGUARD — игровой лаунчер для Windows с оптимизацией под игры,
мониторингом FPS и системных метрик и FPS-оверлеем поверх игр.

## Возможности

- **Библиотека игр** — автообнаружение Steam, Epic Games и локальных игр;
  двойной клик запускает игру (Steam URI / `QProcess`); обложки из Steam CDN
  и иконки из exe; добавление/правка/удаление своих игр вручную.
- **Оптимизация** — закрытие браузеров и мессенджеров перед игрой (`taskkill`)
  с восстановлением закрытых процессов (кнопка **Restore** или выход из
  приложения).
- **Мониторинг FPS** — через [Intel PresentMon](https://github.com/GameTechDev/PresentMon):
  FPS, 1 % / 0.1 % low, график frame time за 60 с.
- **Системные метрики** — CPU (загрузка/температура/частота), GPU через NVML
  (загрузка/температура/VRAM), RAM, сеть и диск — на странице «Мониторинг».
- **FPS-оверлей** — topmost click-through панель поверх игр:
  - глобальный хоткей **Ctrl+Shift+F** (переключает панель, работает даже
    когда в фокусе игра; меняется в настройках, fallback Ctrl+Alt+F);
  - 9 позиций (якоря), размеры S/M/L, прозрачность 0–100 %;
  - выбор монитора: основной / вторичный / активный / оба;
  - гибкая настройка метрик: FPS, low, график, GPU/CPU/RAM/Network/Disk;
  - профили настроек под конкретные игры.
- **Настройки** — автооптимизация, закрытие
  браузеров, путь к PresentMon, вкладка «Оверлей» с кнопкой «Применить»
  (настройки применяются без перезапуска).

## Требования

- Windows 10/11 x64.
- Для сборки из исходников: CMake 3.16+, Qt6 (Widgets/Svg/Network),
  MinGW (MSYS2 ucrt64) или MSVC.
- Для сборки установщика: [Inno Setup 6+](https://jrsoftware.org/isinfo.php).
- Для мониторинга FPS: Intel PresentMon (скачивается кнопкой в настройках)
  и права администратора (ETW-трассировка кадров).

## Установка

### Готовый установщик (рекомендуется)

1. Запустите `VANGUARD-Setup-1.0.0.exe` (собирается из `installer.iss`,
   см. ниже) и подтвердите UAC.
2. Ярлыки: меню «Пуск» (+ опционально рабочий стол), опциональный автозапуск.
3. При каждом запуске VANGUARD спросит UAC — приложение работает от
   администратора, это нужно PresentMon для ETW-трассировки кадров.

## Сборка

```
cmake -B build
cmake --build build --config Release
cmake --build build --target deploy   # Qt DLL + tools/PresentMon рядом с exe
```

На MSYS2 в PATH должен быть `C:\msys64\ucrt64\bin` (иначе `moc` падает
с `0xc0000135`).

Установщик (нужен [Inno Setup 6+](https://jrsoftware.org/isinfo.php)):

```
ISCC.exe installer.iss     → dist/VANGUARD-Setup-1.0.0.exe
```

## Использование

1. **Библиотека** — нажмите «Обновить библиотеку»: VANGUARD найдёт игры
   Steam/Epic. Свои игры добавляются кнопкой «+ Добавить игру».
2. **Мониторинг** — укажите процесс игры (или запустите игру из лаунчера —
   цель подставится сама) и нажмите «Начать мониторинг».
3. **Оверлей** — панель FPS показывается поверх игр; переключение —
   **Ctrl+Shift+F**. Позиция/размер/метрики — Настройки → «Оверлей».
4. **Оптимизация** — «Оптимизировать» перед игрой закроет браузеры и
   мессенджеры; «Restore» вернёт их обратно.

## Скриншоты

![Библиотека](docs/screenshots/library.png)
![Оверлей](docs/screenshots/overlay.png)
![Настройки оверлея](docs/screenshots/settings_overlay.png)

## FAQ

**Почему при запуске спрашивает UAC?**
PresentMon собирает ETW-трассировку кадров — без прав администратора
мониторинг FPS не работает (ошибка «нет прав на ETW», код 6).

**Мониторинг пишет «нет прав на ETW-трассировку»?**
Запустите VANGUARD от администратора либо добавьте свой аккаунт в группу
«Performance Log Users» (учётная запись → Группы → Добавить).

**Где найти PresentMon?**
Кнопка «Скачать PresentMon» в настройках (GitHub releases) или положите
`PresentMon-*.exe` в `tools/` рядом с `VANGUARD.exe`. Автопоиск проверяет
настройки → каталог exe → `tools/` → `../tools` → PATH → Program Files.

**Хоткей Ctrl+Shift+F не срабатывает?**
Проверьте, не занят ли хоткей другим приложением (например, Logi Options+
держит Win+Shift+F — поэтому дефолтный хоткей Ctrl+Shift+F). Сменить:
Настройки → «Оверлей» → «Хоткей» → «Применить».

**Оверлей не виден в эксклюзивном fullscreen?**
Игры в exclusive-fullscreen перекрывают любые topmost-окна. Переключите
игру на borderless/windowed — оверлей будет поверх.

**Где хранятся настройки и кэш?**
Настройки: `%LOCALAPPDATA%\VANGUARD\settings.json`; обложки:
`%LOCALAPPDATA%\VANGUARD\covers`. Папка установки не изменяется —
деинсталлятор удаляет только файлы программы (кэш covers в Program Files
не копируется — это пользовательские данные).

**Игры из Epic не найдены?**
Нужен установленный Epic Games Launcher с играми в `%PROGRAMFILES%\Epic Games`.
Если игра добавлена вручную — используйте «+ Добавить игру».

## Структура проекта

- `include/`, `src/` — исходный код (ядро: MainWindow, GameScanner,
  Optimizer, FPSMonitor, MetricsProvider, FpsOverlay, SettingsManager)
- `src/ui/` — виджеты страниц (Library/Monitor/Optimize) и хром окна
- `resources/` — стили (`styles.qss`), иконки, `app.rc`
  (манифест requireAdministrator + иконка `app.ico` + VERSIONINFO)
- `tools/` — PresentMon (копируется целью deploy)
- `docs/` — документация (`ARCHITECTURE.md`, скриншоты)
- `installer.iss` — Inno Setup 6 → `dist/VANGUARD-Setup-1.0.0.exe`
- `local_games.json` — пример локальных игр (ищется рядом с exe, `../`,
  рабочей директории или в AppData)

## Лицензия

MIT License. Полный текст — в файле [LICENSE](LICENSE).

Copyright (c) 2026 Тимофей С.
