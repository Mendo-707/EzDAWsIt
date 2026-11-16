; EzDAWsIt installer (Inno Setup 6). build.bat runs this after the app builds and passes its tests:
;     ISCC.exe /DAppVersion=1.0.0 installer\EzDAWsIt.iss
; Output: dist\EzDAWsIt-Setup-<version>.exe
;
; NEVER change AppId: Windows uses it to recognise an update of an installed EzDAWsIt.
; Songs (Documents\EzDAWsIt) and settings are the user's: installing, updating and
; uninstalling never touch them.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

#define AppName      "EzDAWsIt"
#define AppPublisher "EzDAW"
#define AppExe       "EzDAWsIt.exe"
#define SongExt      ".tracktionedit"
#define SongProgId   "EzDAW.Song"

[Setup]
AppId={{27D34074-2687-4F43-8FDE-50EC44011A81}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppCopyright=Copyright (C) 2026 {#AppPublisher}. Free software under the GNU GPL v3.
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} installer
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Everyone on this PC by default; "Only for me" needs no administrator
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\LICENSE.txt
SetupIconFile=..\Resources\EzDAWsIt.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir=..\dist
OutputBaseFilename={#AppName}-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; An open EzDAWsIt is asked to close (it saves its song) before files are replaced
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "associate";   Description: "Open songs (*{#SongExt}) with {#AppName} when double-clicked"; GroupDescription: "Songs:"

[Files]
Source: "..\app\{#AppExe}";              DestDir: "{app}"; Flags: ignoreversion
; Lets crash logs name the functions involved (only read when something goes wrong)
Source: "..\app\EzDAWsIt.pdb";           DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\LICENSE.txt";                DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt";       DestDir: "{app}"; Flags: ignoreversion
Source: "README.txt";                    DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{autoprograms}\{#AppName}";                Filename: "{app}\{#AppExe}"
Name: "{autoprograms}\{#AppName} licence";        Filename: "{app}\THIRD-PARTY-NOTICES.txt"
Name: "{autodesktop}\{#AppName}";                 Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; The song type: name, icon, and how to open it. Always listed under "Open with".
Root: HKA; Subkey: "Software\Classes\{#SongProgId}"; ValueType: string; ValueName: ""; ValueData: "{#AppName} song"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#SongProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\{#SongProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\{#SongExt}\OpenWithProgids"; ValueType: string; ValueName: "{#SongProgId}"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: "{#SongExt}"; ValueData: ""; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
; Double-click opens EzDAWsIt (only if chosen: another DAW may use the same file type)
Root: HKA; Subkey: "Software\Classes\{#SongExt}"; ValueType: string; ValueName: ""; ValueData: "{#SongProgId}"; Flags: uninsdeletevalue; Tasks: associate

[Run]
Filename: "{app}\{#AppExe}"; Description: "Start {#AppName} now"; Flags: nowait postinstall skipifsilent

[Messages]
FinishedLabel=Setup has installed [name] on your computer.%n%nYour songs are kept in Documents\EzDAWsIt. Uninstalling never deletes them.
