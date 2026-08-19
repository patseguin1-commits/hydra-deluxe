; Hydra Windows installer (Inno Setup 6).
;
; Compiled by installer\build_installer.ps1, which passes:
;   /DHYDRA_VERSION=<x.y.z>   version parsed from CMakeLists.txt
;   /DHYDRA_STAGE=<dir>       cmake --install staging dir (exes, resource/, docs)
;   /DHYDRA_REDIST=<dir>      dir holding VC_redist.x64.exe
;
; Design notes:
;   * Installs to {autopf}\Hydra with admin rights, then grants the Users
;     group modify rights on the folder ([Dirs]). Hydra writes its database,
;     settings, and HTML reports next to the exe, so the install folder must
;     stay writable for standard users.
;   * The uninstaller removes only the files it installed. Runtime-created
;     hydra*.db / *_settings.ini / *_ui.ini / hydra_*.html survive on purpose.
;   * AppId must never change across releases, or upgrades stop replacing
;     the existing install and Add/Remove gets duplicate entries.

#ifndef HYDRA_VERSION
  #error Pass /DHYDRA_VERSION (use installer\build_installer.ps1)
#endif

[Setup]
AppId={{638FCDD7-88E0-438D-9C52-388C6C9CF4E3}
AppName=Hydra
AppVersion={#HYDRA_VERSION}
AppVerName=Hydra {#HYDRA_VERSION}
DefaultDirName={autopf}\Hydra
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile={#SourcePath}..\LICENSE
SetupIconFile={#SourcePath}..\resource\icon_app.ico
UninstallDisplayIcon={app}\Hydra.exe
DisableProgramGroupPage=yes
OutputDir={#SourcePath}..\build-cpp\installer
OutputBaseFilename=Hydra-{#HYDRA_VERSION}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Dirs]
; Hydra creates its db/settings/reports next to the exe; standard users need
; write access here. The grant is scoped to this one folder.
Name: "{app}"; Permissions: users-modify

[Files]
Source: "{#HYDRA_STAGE}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs
Source: "{#HYDRA_REDIST}\VC_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
; AppUserModelIDs must match src/app/edition.h so taskbar pins group with the
; running processes. CLI tools get no shortcuts.
Name: "{autoprograms}\Hydra"; Filename: "{app}\Hydra.exe"; AppUserModelID: "Hydra.Hydra"
Name: "{autoprograms}\Hydra Uncapped"; Filename: "{app}\HydraUncapped.exe"; AppUserModelID: "Hydra.Uncapped"

[Run]
Filename: "{tmp}\VC_redist.x64.exe"; Parameters: "/install /quiet /norestart"; \
    StatusMsg: "Installing Microsoft Visual C++ Runtime..."; Check: VCRedistNeeded
Filename: "{app}\Hydra.exe"; Description: "Launch Hydra"; Flags: nowait postinstall skipifsilent

[Code]
// Skip the redistributable when the x64 VC++ 2015+ runtime is already there.
// The key lives in the 64-bit hive; Installed=1 means present. VC_redist
// self-skips downgrades, so no version compare is needed.
function VCRedistNeeded: Boolean;
var
  Installed: Cardinal;
begin
  Result := True;
  if RegQueryDWordValue(HKLM64,
      'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed',
      Installed) then
    Result := (Installed <> 1);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent then
    MsgBox('Your Hydra records and settings were kept in ' +
           ExpandConstant('{app}') + '.' + #13#10 +
           'Delete that folder manually if you no longer want them.',
           mbInformation, MB_OK);
end;
