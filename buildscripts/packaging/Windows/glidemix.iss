; GlideMix -- starter Inno Setup 6 script for the Windows installer.
;
; Packs what `cmake --install` put in the install prefix (bin\GlideMix.exe,
; Qt, QML, resources), plus the optional extras GlideMix looks for next to
; the exe. Compile on the build machine:
;
;   iscc /DBuildDir=C:\path\to\install-prefix /DAppVersion=1.0.0 ^
;        buildscripts\packaging\Windows\glidemix.iss
;
; Optional, each skipped when the folder is not given or empty:
;   /DFFmpegDir=...       FFmpeg shared DLLs (avformat-*.dll ...) -> bin\ffmpeg
;                         (BundledFFmpegDir() in FFmpegFunctions.cpp)
;   /DKeyAnalyzerDir=...  OpenKeyScan's PyInstaller build -> bin\openkeyscan-analyzer
;                         (findKeyAnalyzer() in openkeyscanclient.cpp)
;   /DOnnxRuntimeDir=...  mixhouse_deps\onnxruntime-win-x64-1.18.0 (Beat This!)
;                         -> onnxruntime.dll beside GlideMix.exe
;
; Unsigned builds meet SmartScreen ("Windows protected your PC" -> More info
; -> Run anyway). The installer never starts GlideMix on its own: the
; "Launch" box at the end is unchecked, and skipped in silent installs.

#define AppName "GlideMix"
#define AppExe "GlideMix.exe"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #error Pass the CMake install prefix: iscc /DBuildDir=<prefix> glidemix.iss
#endif
#define RepoRoot AddBackslash(SourcePath) + "..\..\.."

[Setup]
; Fixed for every GlideMix release: Windows uses it to find the installed copy
; for upgrades and uninstall (glidemix_clean_uninstall.ps1 looks for it too).
AppId={{332C5FF2-1D0E-427E-B5B2-1D433E93F6D8}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} Beta v{#AppVersion}
AppPublisher={#AppName}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Per-user by default (no admin prompt); the dialog offers all users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#RepoRoot}\build.release
OutputBaseFilename={#AppName}-Beta-v{#AppVersion}-x64-setup
SetupIconFile={#RepoRoot}\share\icons\AppIcon\GlideMix.ico
UninstallDisplayIcon={app}\bin\{#AppExe}
LicenseFile={#RepoRoot}\buildscripts\packaging\Windows\GlideMix-LICENSE.txt
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#BuildDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef FFmpegDir
Source: "{#FFmpegDir}\*.dll"; DestDir: "{app}\bin\ffmpeg"; Flags: ignoreversion skipifsourcedoesntexist
#endif
#ifdef KeyAnalyzerDir
Source: "{#KeyAnalyzerDir}\*"; DestDir: "{app}\bin\openkeyscan-analyzer"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
#endif
#ifdef OnnxRuntimeDir
Source: "{#OnnxRuntimeDir}\lib\onnxruntime*.dll"; DestDir: "{app}\bin"; Flags: ignoreversion skipifsourcedoesntexist
#endif

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\bin\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\bin\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
; An update from inside GlideMix runs this installer with /SILENT: reopen the app after it.
Filename: "{app}\bin\{#AppExe}"; Flags: nowait; Check: WizardSilent

; User data (%APPDATA%\Glidemix, %LOCALAPPDATA%\Glidemix) is kept on uninstall,
; as on the Mac; glidemix_clean_uninstall.ps1 removes it for clean tests.
