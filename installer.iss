; Inno Setup 6 — установщик VANGUARD v1.0.0
; Сборка: ISCC.exe installer.iss  (Inno Setup 6+, https://jrsoftware.org/isinfo.php)
; Результат: dist/VANGUARD-Setup-1.0.0.exe
;
; Примечания:
;  - covers/ НЕ копируется в Program Files — это пользовательский кэш
;    (лежит в %LOCALAPPDATA%\VANGUARD\covers и заполняется при работе).
;  - VANGUARD.exe содержит манифест requireAdministrator (PresentMon/ETW),
;    поэтому установка и запуск требуют подтверждения UAC.

#define MyAppName "VANGUARD"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "VANGUARD"
#define MyAppExeName "VANGUARD.exe"
#define MyAppDir "C:\Projects\VANGUARD"

[Setup]
AppId={{9E1B2C44-6F3A-4D58-9A21-3C7E50D8B104}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputDir={#MyAppDir}\dist
OutputBaseFilename=VANGUARD-Setup-{#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
SetupIconFile={#MyAppDir}\resources\app.ico
DisableProgramGroupPage=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "startup"; Description: "Запускать VANGUARD вместе с Windows"; \
    GroupDescription: "Автозапуск:"; Flags: unchecked

[Files]
; Полная раскладка релиза: exe + Qt DLL (цель deploy) + tools/PresentMon.
; local_games.json — пример локальных игр (кэш covers берётся из AppData).
Source: "{#MyAppDir}\build\VANGUARD.exe"; DestDir: "{app}"; \
    Flags: ignoreversion
Source: "{#MyAppDir}\build\Qt6Core.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyAppDir}\build\Qt6Gui.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyAppDir}\build\Qt6Network.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyAppDir}\build\Qt6Svg.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyAppDir}\build\Qt6Widgets.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyAppDir}\build\libgcc_s_seh-1.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyAppDir}\build\libstdc++-6.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#MyAppDir}\build\libwinpthread-1.dll"; DestDir: "{app}"; Flags: ignoreversion
; Плагины Qt (windeployqt): platforms/imageformats/styles/... — всё из build\.
Source: "{#MyAppDir}\build\*"; DestDir: "{app}"; \
    Flags: recursesubdirs ignoreversion; \
    Excludes: "CMakeFiles\*,VANGUARD_autogen\*,CMakeCache.txt,Makefile,cmake_install.cmake,*.obj,*.o,*.d,*.a,*.log,*.png,*.txt,*.manifest,covers\*,dist\*"
Source: "{#MyAppDir}\tools\*"; DestDir: "{app}\tools"; Flags: recursesubdirs ignoreversion
Source: "{#MyAppDir}\local_games.json"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Удалить {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; \
    Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "{#MyAppName}"; ValueData: """{app}\{#MyAppExeName}"""; \
    Flags: uninsdeletevalue; Tasks: startup

[Run]
Filename: "{app}\{#MyAppExeName}"; \
    Description: "Запустить {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Скрытые кэши в Program Files не создаются, но подчищаем настройки-артефакты.
Type: files; Name: "{app}\*.log"
