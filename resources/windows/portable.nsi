; Single-file portable launcher. No installation, shortcuts or uninstall entry.
; Payload is prepared by package-native.ps1 using cmake --install and windeployqt.
Unicode true
!include "FileFunc.nsh"
!ifndef INPUT_DIR
    !error "INPUT_DIR must point to the generated portable staging directory."
!endif
!ifndef OUTPUT_FILE
    !error "OUTPUT_FILE must name the single portable executable."
!endif
!ifndef VERSION
    !error "VERSION must be passed by the packaging script."
!endif
!ifndef ICON_FILE
    !error "ICON_FILE must point to the generated application icon."
!endif

Name "CAD ${VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Qiao\CAD-Portable\${VERSION}"
RequestExecutionLevel user
SilentInstall silent
AutoCloseWindow true
ShowInstDetails nevershow
Icon "${ICON_FILE}"
SetCompressor /SOLID lzma
SetDatablockOptimize on
VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "CAD"
VIAddVersionKey /LANG=1033 "CompanyName" "Qiao"
VIAddVersionKey /LANG=1033 "FileDescription" "CAD - Qiao Portable"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Qiao CAD"

Var Parameters

Function .onInit
    ${GetParameters} $Parameters
FunctionEnd

Section "CAD Portable"
    SetShellVarContext current
    SetOutPath "$INSTDIR"
    ; Reuse an already extracted copy; this folder only contains this version.
    IfFileExists "$INSTDIR\CAD.exe" launch
    File /r "${INPUT_DIR}\*.*"
launch:
    ; Forward drag-and-drop or command-line file paths to the real application.
    Exec '"$INSTDIR\CAD.exe" $Parameters'
SectionEnd
