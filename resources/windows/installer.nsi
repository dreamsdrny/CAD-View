; NSIS 3 Unicode installer. Generated payload is prepared by package-native.ps1.
Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
Var TestMode
Var Parameters

!ifndef INPUT_DIR
    !error "INPUT_DIR must point to the generated portable staging directory."
!endif
!ifndef OUTPUT_FILE
    !error "OUTPUT_FILE must name the setup executable."
!endif

Name "CAD"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Qiao\CAD"
InstallDirRegKey HKCU "Software\Qiao\CAD" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetDatablockOptimize on
VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "CAD"
VIAddVersionKey /LANG=1033 "CompanyName" "Qiao"
VIAddVersionKey /LANG=1033 "FileDescription" "CAD - Qiao Setup"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Qiao CAD"
!define MUI_ICON "${ICON_FILE}"
!define MUI_UNICON "${ICON_FILE}"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\CAD.exe"
!define MUI_FINISHPAGE_RUN_TEXT "启动 CAD"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

Function .onInit
    StrCpy $TestMode 0
    ${GetParameters} $Parameters
    ClearErrors
    ${GetOptions} $Parameters "/TESTMODE" $0
    ${IfNot} ${Errors}
        StrCpy $TestMode 1
    ${EndIf}
FunctionEnd

Function un.onInit
    StrCpy $TestMode 0
    ${GetParameters} $Parameters
    ClearErrors
    ${GetOptions} $Parameters "/TESTMODE" $0
    ${IfNot} ${Errors}
        StrCpy $TestMode 1
    ${EndIf}
FunctionEnd

Section "CAD" SEC_MAIN
    SetShellVarContext current
    SetOutPath "$INSTDIR"
    File /r "${INPUT_DIR}\*.*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    ${If} $TestMode == 0
    CreateDirectory "$SMPROGRAMS\Qiao CAD"
    CreateShortcut "$SMPROGRAMS\Qiao CAD\CAD.lnk" "$INSTDIR\CAD.exe" "" "$INSTDIR\CAD.exe" 0
    CreateShortcut "$SMPROGRAMS\Qiao CAD\卸载 CAD.lnk" "$INSTDIR\Uninstall.exe"
    CreateShortcut "$DESKTOP\CAD.lnk" "$INSTDIR\CAD.exe" "" "$INSTDIR\CAD.exe" 0
    WriteRegStr HKCU "Software\Qiao\CAD" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD" "DisplayName" "CAD"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD" "DisplayIcon" "$INSTDIR\CAD.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD" "Publisher" "Qiao"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD" "NoRepair" 1
    ${EndIf}
SectionEnd

Section "Uninstall"
    SetShellVarContext current
    ${If} $TestMode == 0
    Delete "$DESKTOP\CAD.lnk"
    Delete "$SMPROGRAMS\Qiao CAD\CAD.lnk"
    Delete "$SMPROGRAMS\Qiao CAD\卸载 CAD.lnk"
    RMDir "$SMPROGRAMS\Qiao CAD"
    ${EndIf}
    ; Remove only installed program files; keep any user-created drawings in the folder.
    Delete "$INSTDIR\CAD.exe"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\qt.conf"
    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\styles"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\generic"
    RMDir /r "$INSTDIR\networkinformation"
    RMDir /r "$INSTDIR\tls"
    Delete "$INSTDIR\docs\README.md"
    Delete "$INSTDIR\docs\USER_GUIDE.md"
    RMDir "$INSTDIR\docs"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    ${If} $TestMode == 0
        DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QiaoCAD"
        DeleteRegKey HKCU "Software\Qiao\CAD"
    ${EndIf}
SectionEnd
