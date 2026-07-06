#define AppName "BJ LED Ambilight"
#ifndef AppVersion
#define AppVersion "0.1.0"
#endif
#define AppExeName "BJLEDAmbilight.exe"

[Setup]
AppId={{64A18941-31C4-4560-AF13-419E0D3E8E6A}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=BJ LED Ambilight
DefaultDirName={autopf}\BJ LED Ambilight
DefaultGroupName=BJ LED Ambilight
DisableProgramGroupPage=yes
OutputDir=..\..\dist
OutputBaseFilename=BJLEDAmbilight-{#AppVersion}-windows-x64-setup
SetupIconFile=..\..\src\resources\AppIcon.ico
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest

[Files]
Source: "..\..\build\BJLEDAmbilight.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\src\resources\icons\app-icon-256.png"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\BJ LED Ambilight"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\BJ LED Ambilight"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch BJ LED Ambilight"; Flags: nowait postinstall skipifsilent
