; The Windows installer (Inno Setup 6), made by the Release workflow (.github/workflows/release.yml) from the folder of
; the zip. It installs for everyone (administrator) or, chosen in its first page, for the user alone; it adds Mothion to
; the Start menu (and, if asked, to the desktop), opens .moth projects with it, and can be uninstalled from Windows'
; settings. Built with:
;   ISCC /DAppVersion=0.2.0 /DNumericVersion=0.2.0 /DSourceDir=<the program's folder> /DOutputDir=<where>
;        /DOutputName=<file name without .exe> installer\mothion.iss

#ifndef AppVersion
  #error AppVersion is not set (/DAppVersion=...)
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir is not set (/DSourceDir=...)
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
#ifndef OutputName
  #define OutputName "Mothion-" + AppVersion + "-windows-x64-setup"
#endif

[Setup]
; The AppId ties the versions together: a new one installs over the old, and one uninstaller. Never change it.
AppId={{6D3F2B8A-4C1E-4E7B-9A55-2F0C8D1B7E43}
AppName=Mothion
AppVersion={#AppVersion}
AppVerName=Mothion {#AppVersion}
AppPublisher=Mothion
AppPublisherURL=https://github.com/HeineCantor/Mothion
AppSupportURL=https://github.com/HeineCantor/Mothion/issues
AppUpdatesURL=https://github.com/HeineCantor/Mothion/releases
VersionInfoVersion={#NumericVersion}
DefaultDirName={autopf}\Mothion
DefaultGroupName=Mothion
DisableProgramGroupPage=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
LicenseFile={#SourceDir}\LICENSE.txt
SetupIconFile=..\resources\icon\mothion.ico
UninstallDisplayIcon={app}\mothion.exe
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Mothion"; Filename: "{app}\mothion.exe"
Name: "{autodesktop}\Mothion"; Filename: "{app}\mothion.exe"; Tasks: desktopicon

[Registry]
; .moth projects open with Mothion (for the user or for everyone, as it is installed).
Root: HKA; Subkey: "Software\Classes\.moth"; ValueType: string; ValueName: ""; ValueData: "Mothion.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.moth\OpenWithProgids"; ValueType: string; ValueName: "Mothion.Project"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Mothion.Project"; ValueType: string; ValueName: ""; ValueData: "Mothion project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Mothion.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\mothion.exe,0"
Root: HKA; Subkey: "Software\Classes\Mothion.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\mothion.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\mothion.exe\SupportedTypes"; ValueType: string; ValueName: ".moth"; ValueData: ""; Flags: uninsdeletekey

[Run]
Filename: "{app}\mothion.exe"; Description: "{cm:LaunchProgram,Mothion}"; Flags: nowait postinstall skipifsilent
