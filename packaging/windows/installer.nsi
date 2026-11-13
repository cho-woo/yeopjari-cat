; Compile through tools/build_installer.py. Per-user install; preserve pet data.
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!ifndef APP_VERSION
  !error "APP_VERSION is required"
!endif
!ifndef PAYLOAD_DIR
  !error "PAYLOAD_DIR is required"
!endif
!ifdef UNSIGNED_REVIEW
  !define APP_NAME "옆자리 고양이 (설치 검토용)"
  !define INSTALL_KEY "YeopjariCat-InstallerReview"
  Caption "서명되지 않은 로컬 설치 검토용 — 공개 배포 금지"
!else
  !define APP_NAME "옆자리 고양이"
  !define INSTALL_KEY "YeopjariCat"
  !ifndef SIGN_COMMAND
    !error "Release builds require SIGN_COMMAND for installer and uninstaller signing"
  !endif
  !uninstfinalize '${SIGN_COMMAND} "%1"' = 0
  !finalize '${SIGN_COMMAND} "%1"' = 0
!endif

Name "${APP_NAME}"
OutFile "${OUTPUT_EXE}"
RequestExecutionLevel user
ManifestSupportedOS Win10
InstallDir "$LOCALAPPDATA\Programs\${INSTALL_KEY}"
InstallDirRegKey HKCU "Software\${INSTALL_KEY}" "InstallDir"
SetCompressor /SOLID lzma
BrandingText "가치탐험단 · Value Wander"
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "FileDescription" "Yeopjari Cat Setup"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "CompanyName" "가치탐험단"
VIAddVersionKey "LegalCopyright" "가치탐험단"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "화면 한쪽에서 함께 지내는 작은 고양이를 설치합니다.$\r$\n$\r$\n이 사용자 계정에만 설치하며, 기존 고양이의 생일과 돌봄 기록은 그대로 사용합니다."
!define MUI_FINISHPAGE_RUN "$INSTDIR\YeopjariCat.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!define MUI_FINISHPAGE_RUN_TEXT "옆자리 고양이 실행"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Korean"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "Windows 10/11 64비트에서 설치할 수 있습니다." /SD IDOK
    SetErrorLevel 3
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_OK|MB_ICONSTOP "Windows 10 이상이 필요합니다." /SD IDOK
    SetErrorLevel 3
    Abort
  ${EndIf}
  FindWindow $0 "YeopjariCat.Native.v1"
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONINFORMATION "실행 중인 옆자리 고양이를 우클릭해 종료한 뒤 설치해주세요. 돌봄 기록은 유지됩니다." /SD IDOK
    SetErrorLevel 5
    Abort
  ${EndIf}
FunctionEnd

Section "옆자리 고양이" Main
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File "${PAYLOAD_DIR}\YeopjariCat.exe"
  File "${PAYLOAD_DIR}\update-feed.txt"
  File "${PAYLOAD_DIR}\README_KO.txt"
  File "${PAYLOAD_DIR}\CHANGELOG_KO.txt"
  File "${PAYLOAD_DIR}\THIRD_PARTY_NOTICES.txt"
  File "${PAYLOAD_DIR}\DISTRIBUTION_KO.txt"
  File "${__FILEDIR__}\NSIS-NOTICES.txt"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortCut "$SMPROGRAMS\${APP_NAME}\옆자리 고양이.lnk" "$INSTDIR\YeopjariCat.exe"
  WriteRegStr HKCU "Software\${INSTALL_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALL_KEY}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALL_KEY}" "Publisher" "가치탐험단"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALL_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALL_KEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALL_KEY}" "DisplayIcon" "$INSTDIR\YeopjariCat.exe"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALL_KEY}" "NoRepair" 1
SectionEnd

Function un.onInit
  FindWindow $0 "YeopjariCat.Native.v1"
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONINFORMATION "옆자리 고양이를 종료한 뒤 제거해주세요. 돌봄 기록은 삭제되지 않습니다." /SD IDOK
    SetErrorLevel 5
    Abort
  ${EndIf}
FunctionEnd

Section "Uninstall"
  SetShellVarContext current
  Delete "$INSTDIR\YeopjariCat.exe"
  Delete "$INSTDIR\update-feed.txt"
  Delete "$INSTDIR\README_KO.txt"
  Delete "$INSTDIR\CHANGELOG_KO.txt"
  Delete "$INSTDIR\THIRD_PARTY_NOTICES.txt"
  Delete "$INSTDIR\DISTRIBUTION_KO.txt"
  Delete "$INSTDIR\NSIS-NOTICES.txt"
  Delete "$INSTDIR\Uninstall.exe"
  Delete "$SMPROGRAMS\${APP_NAME}\옆자리 고양이.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"
  ; Never recursively remove installation or pet-data folders.
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${INSTALL_KEY}"
  DeleteRegKey HKCU "Software\${INSTALL_KEY}"
SectionEnd
