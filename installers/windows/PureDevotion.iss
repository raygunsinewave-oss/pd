; Pure Devotion — Inno Setup script (x64). Built by build_installer.ps1.
#define AppName "Pure Devotion"
#define AppVersion "1.0.0"
#ifndef ArtefactDir
  #define ArtefactDir "..\..\build\PureDevotion_artefacts\Release"
#endif
#ifndef ContentDir
  #define ContentDir "..\..\content"
#endif

[Setup]
AppId={{6F2A9C1E-8D4B-4E3A-9B7F-5D0C2E1A7B44}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Raygun
DefaultDirName={autopf}\Raygun\Pure Devotion
DefaultGroupName=Raygun
DisableProgramGroupPage=yes
OutputBaseFilename=PureDevotion_Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayName={#AppName}
LicenseFile=..\..\LICENSE.txt

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom
Name: "app"; Description: "Standalone application"; Types: full custom
Name: "content"; Description: "Factory presets and content (2,850 presets)"; Types: full custom

[Files]
Source: "{#ArtefactDir}\VST3\Pure Devotion.vst3\*"; DestDir: "{commoncf64}\VST3\Pure Devotion.vst3"; Components: vst3; Flags: recursesubdirs ignoreversion
Source: "{#ArtefactDir}\Standalone\Pure Devotion.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#ContentDir}\Factory Presets\*"; DestDir: "{commonappdata}\Raygun\Pure Devotion\Factory Presets"; Components: content; Flags: recursesubdirs ignoreversion
Source: "{#ContentDir}\Wavetables\*"; DestDir: "{commonappdata}\Raygun\Pure Devotion\Wavetables"; Components: content; Flags: recursesubdirs ignoreversion skipifsourcedoesntexist
Source: "{#ContentDir}\Samples\*"; DestDir: "{commonappdata}\Raygun\Pure Devotion\Samples"; Components: content; Flags: recursesubdirs ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{autoprograms}\Pure Devotion"; Filename: "{app}\Pure Devotion.exe"; Components: app
