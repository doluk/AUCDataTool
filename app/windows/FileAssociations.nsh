; SPDX-FileCopyrightText: 2026 Lukas Dobler
; SPDX-License-Identifier: LGPL-3.0-or-later
;
; Registers AUCDataTool as the handler for AUC raw data file extensions
; (.auc, .mw, .mwrs, .ip1-.ip8, .ra1-.ra8, .ri1-.ri8; Windows registry extensions
; are case-insensitive, so .IP1-.IP8, .RA1-.RA8, .RI1-.RI8 are covered as well).

!ifndef AUC_FILE_ASSOCIATIONS_NSH
!define AUC_FILE_ASSOCIATIONS_NSH

!macro AUC_REGISTER_PROGID PROGID DESCRIPTION
    WriteRegStr SHCTX "Software\Classes\${PROGID}" "" "${DESCRIPTION}"
    WriteRegStr SHCTX "Software\Classes\${PROGID}\DefaultIcon" "" "$INSTDIR\bin\AUCDataTool.exe,0"
    WriteRegStr SHCTX "Software\Classes\${PROGID}\shell\open\command" "" '"$INSTDIR\bin\AUCDataTool.exe" "%1"'
!macroend

!macro AUC_UNREGISTER_PROGID PROGID
    DeleteRegKey SHCTX "Software\Classes\${PROGID}"
!macroend

; Always offered in "Open with"; the default handler only where no other program has
; claimed the extension (.mw is also Maple's worksheet format, .ip*/.ra*/.ri* Beckman's).
!macro AUC_REGISTER_EXT EXT PROGID
    ReadRegStr $0 SHCTX "Software\Classes\${EXT}" ""
    StrCmp $0 "" 0 +2
        WriteRegStr SHCTX "Software\Classes\${EXT}" "" "${PROGID}"
    WriteRegStr SHCTX "Software\Classes\${EXT}\OpenWithProgids" "${PROGID}" ""
    WriteRegStr SHCTX "Software\Classes\Applications\AUCDataTool.exe\SupportedTypes" "${EXT}" ""
    WriteRegStr SHCTX "Software\AUCDataTool\Capabilities\FileAssociations" "${EXT}" "${PROGID}"
!macroend

!macro AUC_UNREGISTER_EXT EXT PROGID
    ReadRegStr $0 SHCTX "Software\Classes\${EXT}" ""
    StrCmp $0 "${PROGID}" 0 +2
        DeleteRegValue SHCTX "Software\Classes\${EXT}" ""
    DeleteRegValue SHCTX "Software\Classes\${EXT}\OpenWithProgids" "${PROGID}"
    DeleteRegKey /ifempty SHCTX "Software\Classes\${EXT}\OpenWithProgids"
    DeleteRegKey /ifempty SHCTX "Software\Classes\${EXT}"
!macroend

!macro AUC_REGISTER_FILE_ASSOCIATIONS
    !insertmacro AUC_REGISTER_PROGID "AUCDataTool.auc"   "openAUC Data File"
    !insertmacro AUC_REGISTER_PROGID "AUCDataTool.mw"    "Multi-Wavelength AUC Scan (.mw)"
    !insertmacro AUC_REGISTER_PROGID "AUCDataTool.mwrs"  "Multi-Wavelength AUC Scan (.mwrs)"
    !insertmacro AUC_REGISTER_PROGID "AUCDataTool.xl.ip" "Beckman XL Interference Scan"
    !insertmacro AUC_REGISTER_PROGID "AUCDataTool.xl.ra" "Beckman XL Absorbance Scan"
    !insertmacro AUC_REGISTER_PROGID "AUCDataTool.xl.ri" "Beckman XL Intensity Scan"

    WriteRegStr SHCTX "Software\Classes\Applications\AUCDataTool.exe" "FriendlyAppName" "AUCDataTool"
    WriteRegStr SHCTX "Software\Classes\Applications\AUCDataTool.exe\DefaultIcon" "" "$INSTDIR\bin\AUCDataTool.exe,0"
    WriteRegStr SHCTX "Software\Classes\Applications\AUCDataTool.exe\shell\open\command" "" '"$INSTDIR\bin\AUCDataTool.exe" "%1"'

    WriteRegStr SHCTX "Software\AUCDataTool\Capabilities" "ApplicationName" "AUCDataTool"
    WriteRegStr SHCTX "Software\AUCDataTool\Capabilities" "ApplicationDescription" "Viewer for analytical ultracentrifugation raw data"
    WriteRegStr SHCTX "Software\RegisteredApplications" "AUCDataTool" "Software\AUCDataTool\Capabilities"

    !insertmacro AUC_REGISTER_EXT ".auc"  "AUCDataTool.auc"
    !insertmacro AUC_REGISTER_EXT ".mw"   "AUCDataTool.mw"
    !insertmacro AUC_REGISTER_EXT ".mwrs" "AUCDataTool.mwrs"

    !insertmacro AUC_REGISTER_EXT ".ip1"  "AUCDataTool.xl.ip"
    !insertmacro AUC_REGISTER_EXT ".ip2"  "AUCDataTool.xl.ip"
    !insertmacro AUC_REGISTER_EXT ".ip3"  "AUCDataTool.xl.ip"
    !insertmacro AUC_REGISTER_EXT ".ip4"  "AUCDataTool.xl.ip"
    !insertmacro AUC_REGISTER_EXT ".ip5"  "AUCDataTool.xl.ip"
    !insertmacro AUC_REGISTER_EXT ".ip6"  "AUCDataTool.xl.ip"
    !insertmacro AUC_REGISTER_EXT ".ip7"  "AUCDataTool.xl.ip"
    !insertmacro AUC_REGISTER_EXT ".ip8"  "AUCDataTool.xl.ip"

    !insertmacro AUC_REGISTER_EXT ".ra1"  "AUCDataTool.xl.ra"
    !insertmacro AUC_REGISTER_EXT ".ra2"  "AUCDataTool.xl.ra"
    !insertmacro AUC_REGISTER_EXT ".ra3"  "AUCDataTool.xl.ra"
    !insertmacro AUC_REGISTER_EXT ".ra4"  "AUCDataTool.xl.ra"
    !insertmacro AUC_REGISTER_EXT ".ra5"  "AUCDataTool.xl.ra"
    !insertmacro AUC_REGISTER_EXT ".ra6"  "AUCDataTool.xl.ra"
    !insertmacro AUC_REGISTER_EXT ".ra7"  "AUCDataTool.xl.ra"
    !insertmacro AUC_REGISTER_EXT ".ra8"  "AUCDataTool.xl.ra"

    !insertmacro AUC_REGISTER_EXT ".ri1"  "AUCDataTool.xl.ri"
    !insertmacro AUC_REGISTER_EXT ".ri2"  "AUCDataTool.xl.ri"
    !insertmacro AUC_REGISTER_EXT ".ri3"  "AUCDataTool.xl.ri"
    !insertmacro AUC_REGISTER_EXT ".ri4"  "AUCDataTool.xl.ri"
    !insertmacro AUC_REGISTER_EXT ".ri5"  "AUCDataTool.xl.ri"
    !insertmacro AUC_REGISTER_EXT ".ri6"  "AUCDataTool.xl.ri"
    !insertmacro AUC_REGISTER_EXT ".ri7"  "AUCDataTool.xl.ri"
    !insertmacro AUC_REGISTER_EXT ".ri8"  "AUCDataTool.xl.ri"

    System::Call 'shell32.dll::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
!macroend

!macro AUC_UNREGISTER_FILE_ASSOCIATIONS
    !insertmacro AUC_UNREGISTER_EXT ".auc"  "AUCDataTool.auc"
    !insertmacro AUC_UNREGISTER_EXT ".mw"   "AUCDataTool.mw"
    !insertmacro AUC_UNREGISTER_EXT ".mwrs" "AUCDataTool.mwrs"

    !insertmacro AUC_UNREGISTER_EXT ".ip1"  "AUCDataTool.xl.ip"
    !insertmacro AUC_UNREGISTER_EXT ".ip2"  "AUCDataTool.xl.ip"
    !insertmacro AUC_UNREGISTER_EXT ".ip3"  "AUCDataTool.xl.ip"
    !insertmacro AUC_UNREGISTER_EXT ".ip4"  "AUCDataTool.xl.ip"
    !insertmacro AUC_UNREGISTER_EXT ".ip5"  "AUCDataTool.xl.ip"
    !insertmacro AUC_UNREGISTER_EXT ".ip6"  "AUCDataTool.xl.ip"
    !insertmacro AUC_UNREGISTER_EXT ".ip7"  "AUCDataTool.xl.ip"
    !insertmacro AUC_UNREGISTER_EXT ".ip8"  "AUCDataTool.xl.ip"

    !insertmacro AUC_UNREGISTER_EXT ".ra1"  "AUCDataTool.xl.ra"
    !insertmacro AUC_UNREGISTER_EXT ".ra2"  "AUCDataTool.xl.ra"
    !insertmacro AUC_UNREGISTER_EXT ".ra3"  "AUCDataTool.xl.ra"
    !insertmacro AUC_UNREGISTER_EXT ".ra4"  "AUCDataTool.xl.ra"
    !insertmacro AUC_UNREGISTER_EXT ".ra5"  "AUCDataTool.xl.ra"
    !insertmacro AUC_UNREGISTER_EXT ".ra6"  "AUCDataTool.xl.ra"
    !insertmacro AUC_UNREGISTER_EXT ".ra7"  "AUCDataTool.xl.ra"
    !insertmacro AUC_UNREGISTER_EXT ".ra8"  "AUCDataTool.xl.ra"

    !insertmacro AUC_UNREGISTER_EXT ".ri1"  "AUCDataTool.xl.ri"
    !insertmacro AUC_UNREGISTER_EXT ".ri2"  "AUCDataTool.xl.ri"
    !insertmacro AUC_UNREGISTER_EXT ".ri3"  "AUCDataTool.xl.ri"
    !insertmacro AUC_UNREGISTER_EXT ".ri4"  "AUCDataTool.xl.ri"
    !insertmacro AUC_UNREGISTER_EXT ".ri5"  "AUCDataTool.xl.ri"
    !insertmacro AUC_UNREGISTER_EXT ".ri6"  "AUCDataTool.xl.ri"
    !insertmacro AUC_UNREGISTER_EXT ".ri7"  "AUCDataTool.xl.ri"
    !insertmacro AUC_UNREGISTER_EXT ".ri8"  "AUCDataTool.xl.ri"

    !insertmacro AUC_UNREGISTER_PROGID "AUCDataTool.auc"
    !insertmacro AUC_UNREGISTER_PROGID "AUCDataTool.mw"
    !insertmacro AUC_UNREGISTER_PROGID "AUCDataTool.mwrs"
    !insertmacro AUC_UNREGISTER_PROGID "AUCDataTool.xl.ip"
    !insertmacro AUC_UNREGISTER_PROGID "AUCDataTool.xl.ra"
    !insertmacro AUC_UNREGISTER_PROGID "AUCDataTool.xl.ri"

    DeleteRegKey SHCTX "Software\Classes\Applications\AUCDataTool.exe"
    DeleteRegValue SHCTX "Software\RegisteredApplications" "AUCDataTool"
    DeleteRegKey SHCTX "Software\AUCDataTool\Capabilities"
    DeleteRegKey /ifempty SHCTX "Software\AUCDataTool"

    System::Call 'shell32.dll::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
!macroend

!endif
