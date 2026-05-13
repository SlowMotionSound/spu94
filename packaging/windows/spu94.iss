; SPU-94 Phase 26 -- Windows Inno Setup 6 installer script
; Installs VST3 and CLAP plugins to standard Windows scan paths.
; Compile with: iscc /DMyAppVersion=<version> spu94.iss
;
; No code signing (deferred per project decision -- beta testers must
; bypass SmartScreen on first run).

[Setup]
AppName=SPU-94
AppVersion={#MyAppVersion}
AppPublisher=SPU-94 Project
AppSupportURL=https://github.com/spu94project/spu94
DefaultDirName={autopf}\SPU-94
DisableDirPage=yes
OutputBaseFilename=SPU-94-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=SPU-94
UninstallDisplayIcon={app}\unins000.exe

[Files]
; VST3 bundle (directory with recurse)
Source: "artifacts\VST3\SPU-94.vst3\*"; DestDir: "{commoncf}\VST3\SPU-94.vst3"; Flags: recursesubdirs
; CLAP single file
Source: "artifacts\CLAP\SPU-94.clap"; DestDir: "{commoncf}\CLAP"

[InstallDelete]
; Clean previous install before copying new files
Type: filesandordirs; Name: "{commoncf}\VST3\SPU-94.vst3"
Type: files; Name: "{commoncf}\CLAP\SPU-94.clap"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf}\VST3\SPU-94.vst3"
Type: files; Name: "{commoncf}\CLAP\SPU-94.clap"
