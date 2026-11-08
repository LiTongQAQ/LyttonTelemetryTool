; Lytton Telemetry Tool installer (Inno Setup 6). Built by scripts/build-installer.sh, which passes the version:
;   ISCC /DAppVersion=0.2.3 installer\lytton.iss
; Per-user install, no administrator rights. Installing over an existing installation just updates it; uninstalling
; removes only the files installed here, so lytton.ini, lytton.log and the recordings folder stay.
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define AppName "Lytton Telemetry Tool"
#define AppExe "LyttonTelemetryTool.exe"

[Setup]
AppId={{6F2B8E61-4C1D-4E7A-9B53-1A7C2D9E4F80}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} V{#AppVersion}
AppPublisher=LiTongQAQ
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=LyttonTelemetryTool-Setup-{#AppVersion}
SetupIconFile=..\assets\brand\logo.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
LicenseFile=packages\com.lytton.f1race\meta\license.txt
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
; the Qt program with its Qt DLLs and QML modules (build-qt\app, windeployqt output); development leftovers excluded
Source: "..\build-qt\app\*"; DestDir: "{app}"; Excludes: "*.ini,*.log,.lytton-write-test,recordings\*"; Flags: ignoreversion recursesubdirs createallsubdirs
; the old ImGui window, kept while the Qt pages are being reworked
Source: "..\build\f1-race.exe"; DestDir: "{app}"; DestName: "LyttonClassic.exe"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "packages\com.lytton.f1race\meta\license.txt"; DestDir: "{app}"; DestName: "许可与第三方组件.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{group}\{#AppName}（虚拟比赛演示）"; Filename: "{app}\{#AppExe}"; Parameters: "--demo"; WorkingDir: "{app}"
Name: "{group}\{#AppName}（旧版界面）"; Filename: "{app}\LyttonClassic.exe"; WorkingDir: "{app}"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Start {#AppName} now"; Flags: nowait postinstall skipifsilent
