; Inno Setup script for FastSMRW.
; Invoked by build.bat / CI with the version and the dist folder on the command
; line, e.g.:
;   ISCC.exe /DMyAppVersion=0.1.0 /DSourceDir=dist /DOutputDir=. installer.iss
; Produces FastSMRWInstaller.exe. The installer writes an "installed.txt" marker
; next to the exe; the in-app updater keys off that marker to update an installed
; copy via this setup (silently) instead of the portable zip.

#define MyAppName "FastSMRW"
#define MyAppPublisher "Mew"
#define MyAppURL "https://masonasons.me"
#define MyAppExeName "FastSMRW.exe"

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "dist"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{B7E4F2A1-9C3D-4E6F-A1B2-3C4D5E6F7A8B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputBaseFilename=FastSMRWInstaller
OutputDir={#OutputDir}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Admin by default (Program Files), but the user may choose a per-user install.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Install the whole run folder (exe, speech DLLs, soundpacks, keymaps, docs).
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\installed.txt"

[Code]
// Drop a marker so the running app knows it was installed (vs. portable). A
// portable zip never contains this file.
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    SaveStringToFile(ExpandConstant('{app}\installed.txt'), 'Installed via setup', False);
end;
