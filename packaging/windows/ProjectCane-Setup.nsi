; ProjectCane - Windows installer (no game files included)
; The user points the installer at their own NTSC Sly Cooper ISO and the game
; data is extracted during installation with the File Extractor.
;
; Build steps (see README.md in this folder):
;   1. In Visual Studio build Sly1-Release and "File Extractor" as Release | x64.
;   2. makensis packaging/windows/ProjectCane-Setup.nsi
;   The installer is written next to this script.

Unicode true
SetCompressor /SOLID lzma
ManifestDPIAware true
RequestExecutionLevel user

!define APPNAME   "ProjectCane"
!define EXENAME   "Sly Cooper and the Thievius Raccoonus.exe"
!define EXTRACTOR "File Extractor.exe"
!define ROOT      "../.."
!ifndef SRC
  !define SRC     "${ROOT}/x64/Release"   ; folder holding the built game exe and runtime DLLs
!endif
!ifndef VERSION
  !define VERSION "0.4.0-beta.1"
!endif
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"

Name "${APPNAME} - Sly Cooper and the Thievius Raccoonus"
VIProductVersion "0.4.0.1"
VIAddVersionKey "ProductName" "ProjectCane"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "ProjectCane installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "ProjectCane contributors"
!ifndef OUTDIR
  !define OUTDIR  "."                     ; next to this script
!endif
OutFile "${OUTDIR}/ProjectCane-QOL-v${VERSION}-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\${APPNAME}"
InstallDirRegKey HKCU "Software\${APPNAME}" "InstallDir"
BrandingText "ProjectCane PC port"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "nsDialogs.nsh"
!include "FileFunc.nsh"

Var IsoPath
Var SkipExtract
Var hIsoText
Var hSkipBox

!define MUI_ICON   "${ROOT}/Sly1-Release/Assets/Sly1.ico"
!define MUI_UNICON "${ROOT}/Sly1-Release/Assets/Sly1.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Install ProjectCane"
!define MUI_WELCOMEPAGE_TEXT "This installs the ProjectCane PC port of Sly Cooper and the Thievius Raccoonus, with DLSS / FSR, mods, mouse support and the reworked menus.$\r$\n$\r$\nNo game data is included. You need your own NTSC (US) copy of Sly Cooper and the Thievius Raccoonus as a disc image (.iso); the installer extracts the game files from it.$\r$\n$\r$\nNo administrator rights are needed."
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXENAME}"
!define MUI_FINISHPAGE_RUN_TEXT "Play Sly Cooper now"
!define MUI_COMPONENTSPAGE_SMALLDESC

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
Page custom IsoPageCreate IsoPageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; -----------------------------------------------------------------------------
; ISO selection page
; -----------------------------------------------------------------------------
Function IsoPageCreate
  !insertmacro MUI_HEADER_TEXT "Your copy of Sly Cooper" "Select your NTSC Sly Cooper and the Thievius Raccoonus disc image."
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 0 100% 36u "Choose the .iso file of your own NTSC (US, SCUS-97198) copy of the game. The installer reads the game data from it and puts it in the install folder. The ISO itself is not modified or copied."
  Pop $0

  ${NSD_CreateText} 0 42u 78% 13u "$IsoPath"
  Pop $hIsoText
  ${NSD_CreateBrowseButton} 80% 41u 20% 15u "Browse..."
  Pop $0
  ${NSD_OnClick} $0 IsoBrowse

  ${NSD_CreateCheckbox} 0 68u 100% 12u "Skip - the game files are already in the install folder (reinstall / update)"
  Pop $hSkipBox
  ${If} $SkipExtract == ${BST_CHECKED}
    ${NSD_Check} $hSkipBox
  ${EndIf}

  ${NSD_CreateLabel} 0 90u 100% 40u "Don't have an ISO? Create one from your own disc with a tool such as ImgBurn, or with the PCSX2 / dumping guides. Extraction needs about 2 GB of free space and takes a few minutes."
  Pop $0

  nsDialogs::Show
FunctionEnd

Function IsoBrowse
  ${NSD_GetText} $hIsoText $0
  nsDialogs::SelectFileDialog open "$0" "Disc images (*.iso)|*.iso|All files (*.*)|*.*"
  Pop $0
  ${If} $0 != ""
  ${AndIf} $0 != "error"
    ${NSD_SetText} $hIsoText "$0"
  ${EndIf}
FunctionEnd

Function IsoPageLeave
  ${NSD_GetText} $hIsoText $IsoPath
  ${NSD_GetState} $hSkipBox $SkipExtract
  ${If} $SkipExtract == ${BST_CHECKED}
    Return
  ${EndIf}
  ${If} $IsoPath == ""
    MessageBox MB_ICONEXCLAMATION "Please choose your Sly Cooper .iso file (or tick Skip if the game files are already installed)."
    Abort
  ${EndIf}
  ${IfNot} ${FileExists} "$IsoPath"
    MessageBox MB_ICONEXCLAMATION "That file doesn't exist:$\r$\n$IsoPath"
    Abort
  ${EndIf}
  ; GetSize measures files inside a folder, so pass the ISO's folder plus its name as the mask
  ${GetParent} "$IsoPath" $R0
  ${GetFileName} "$IsoPath" $R1
  ClearErrors
  ${GetSize} "$R0" "/M=$R1 /S=0M /G=0" $0 $1 $2
  ${IfNot} ${Errors}
  ${AndIf} $0 != ""
  ${AndIf} $0 < 500
    MessageBox MB_ICONEXCLAMATION "That file is only $0 MB - it doesn't look like a full Sly Cooper disc image."
    Abort
  ${EndIf}
FunctionEnd

; -----------------------------------------------------------------------------
Section "ProjectCane (required)" SecGame
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${SRC}/${EXENAME}"
  ; FFmpeg, Streamline / DLSS and AMD FSR runtimes (no community mods)
  File /x version.dll "${SRC}/*.dll"
  File "${ROOT}/x64/Extractor/${EXTRACTOR}"
  SetOutPath "$INSTDIR\Fonts"
  File /r "${SRC}/Fonts/*.*"

  ; Documentation and third-party licenses
  SetOutPath "$INSTDIR\Docs"
  File "${ROOT}/README.md"
  File "${ROOT}/DLSS_README.md"
  File "${ROOT}/MODS_README.md"
  SetOutPath "$INSTDIR\Docs\Licenses"
  File "/oname=NVIDIA-Streamline-license.txt" "${ROOT}/Libraries/streamline/Streamline-license.txt"
  File "/oname=NVIDIA-DLSS-license.txt" "${ROOT}/Libraries/streamline/nvngx_dlss.license.txt"
  File "/oname=AMD-FidelityFX-license.md" "${ROOT}/Libraries/fidelityfx/license.md"
  File "/oname=FFmpeg-LICENSE.txt" "${ROOT}/Libraries/ffmpeg/ffmpeg-master-latest-win64-lgpl-shared/LICENSE.txt"
  File "${ROOT}/Libraries/fonts/LICENSE-fonts-*.txt"
  SetOutPath "$INSTDIR"

  ; Shortcuts
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Sly Cooper and the Thievius Raccoonus.lnk" "$INSTDIR\${EXENAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Re-extract game files (drop ISO here).lnk" "$INSTDIR\${EXTRACTOR}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Uninstall ProjectCane.lnk" "$INSTDIR\Uninstall.exe"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\${APPNAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "ProjectCane - Sly Cooper and the Thievius Raccoonus"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\${EXENAME}"
  WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "ProjectCane"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1

  ; Extract the game data from the user's ISO
  ${If} $SkipExtract == ${BST_CHECKED}
    DetailPrint "Skipping game file extraction."
  ${Else}
    extract:
    DetailPrint "Extracting game files from $IsoPath ..."
    DetailPrint "(this takes a few minutes - the log below shows progress)"
    nsExec::ExecToLog '"$INSTDIR\${EXTRACTOR}" "$IsoPath" --no-pause'
    Pop $0
    ${If} $0 != 0
    ${OrIfNot} ${FileExists} "$INSTDIR\Worlds\*.*"
      MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "Extracting the game files failed (code $0).$\r$\n$\r$\nMake sure the file is an NTSC (US) Sly Cooper and the Thievius Raccoonus disc image.$\r$\n$\r$\nRetry with another file, or Cancel to finish without game files (you can extract later from the Start menu)." IDCANCEL extract_done
      nsDialogs::SelectFileDialog open "$IsoPath" "Disc images (*.iso)|*.iso|All files (*.*)|*.*"
      Pop $1
      ${If} $1 != ""
      ${AndIf} $1 != "error"
        StrCpy $IsoPath $1
      ${EndIf}
      Goto extract
    ${EndIf}
    DetailPrint "Game files extracted."
    extract_done:
  ${EndIf}
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\Sly Cooper and the Thievius Raccoonus.lnk" "$INSTDIR\${EXENAME}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecGame}    "The game program, DLSS / FSR / FFmpeg runtimes, fonts and the File Extractor. Game data is extracted from your ISO."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Adds a desktop shortcut."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; -----------------------------------------------------------------------------
Section "Uninstall"
  Delete "$DESKTOP\Sly Cooper and the Thievius Raccoonus.lnk"
  RMDir /r "$SMPROGRAMS\${APPNAME}"

  Delete "$INSTDIR\${EXENAME}"
  Delete "$INSTDIR\${EXTRACTOR}"
  Delete "$INSTDIR\*.dll"
  RMDir /r "$INSTDIR\Worlds"
  RMDir /r "$INSTDIR\Cutscenes"
  RMDir /r "$INSTDIR\Fonts"
  RMDir /r "$INSTDIR\Docs"
  Delete "$INSTDIR\*.log"
  Delete "$INSTDIR\imgui.ini"

  MessageBox MB_YESNO|MB_ICONQUESTION "Also delete your saves, save states, screenshots and settings?" /SD IDNO IDNO keepdata
    RMDir /r "$INSTDIR\Saves"
    RMDir /r "$INSTDIR\SaveStates"
    RMDir /r "$INSTDIR\Screenshots"
    RMDir /r "$INSTDIR\Settings Backups"
    Delete "$INSTDIR\*.ini"
  keepdata:

  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINSTKEY}"
  DeleteRegKey HKCU "Software\${APPNAME}"
SectionEnd
