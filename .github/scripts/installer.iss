#define MyAppName "OBS Test Card"
#define MyAppPublisher "Marulo"
#define MyAppURL "https://github.com/marulo/OBS_Test_Card"

[Setup]
; NOTE: The value of AppId uniquely identifies this application.
; Do not use the same AppId value in installers for other applications.
AppId={{91C80B0F-9C1D-47C0-8D3A-25A12D2BCF64}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
; The default installation directory for OBS Studio plugins
ArchitecturesInstallIn64BitMode=x64
DefaultDirName={autopf}\obs-studio
DirExistsWarning=no
DisableProgramGroupPage=yes
; Output to the release directory (two levels up from scripts)
OutputDir=..\..\release
OutputBaseFilename={#OutputBase}-{#MyAppVersion}-windows-x64
Compression=lzma
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Files]
; The source files are in the specified release folder
Source: "..\..\release\{#Configuration}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; NOTE: Don't use "Flags: ignoreversion" on any shared system files
