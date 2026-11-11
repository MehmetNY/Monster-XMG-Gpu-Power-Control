; MONSTER GPU POWER installer / kurulum paketi (NSIS 3, Unicode) - English + Turkish / Turkce + Ingilizce.
; Does NOT install any driver and does not touch the OEM Control Center. / Surucu KURMAZ; OEM Control Center'a dokunmaz.
;
; Language / Dil:
;   * Interactive setup shows a language dialog first (English / Türkçe). The choice drives every installer page,
;     the shortcut names and the uninstaller, and it is remembered in HKLM\Software\MonsterGpuPower\Language (1033 = English,
;     1055 = Turkish). The installed application reads that value as its initial interface language (the EN|TR switch in the
;     application window can change it later; a reinstall with a different language choice takes precedence again).
;   * Silent / scripted setup:  Setup.exe /S /LANG=EN   or   /S /LANG=TR      (without /LANG the system UI language is used,
;     English when it is neither English nor Turkish).  Other switches: /NODESKTOP  /NOSTARTMENU  /D=<install dir>
Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "Sections.nsh"

!define APPNAME "MONSTER GPU POWER"
!define APPEXE  "MonsterGpuPower.exe"
!define APPVER  "1.5.0"
!define APPKEY  "Software\MonsterGpuPower"
!define UNKEY   "Software\Microsoft\Windows\CurrentVersion\Uninstall\MonsterGpuPower"

Name "${APPNAME}"
OutFile "..\build\MONSTER_GPU_POWER_Setup.exe"
InstallDir "$PROGRAMFILES64\MonsterGpuPower"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "${APPNAME} ${APPVER}"

VIProductVersion "1.5.0.0"
VIAddVersionKey /LANG=1055 "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=1055 "FileDescription" "${APPNAME} kurulum programı"
VIAddVersionKey /LANG=1055 "FileVersion" "${APPVER}"
VIAddVersionKey /LANG=1055 "LegalCopyright" "GPL-3.0"
VIAddVersionKey /LANG=1033 "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=1033 "FileDescription" "${APPNAME} installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${APPVER}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "GPL-3.0"

!define MUI_ICON   "..\res\app.ico"
!define MUI_UNICON "..\res\app.ico"
!define MUI_ABORTWARNING

; Language dialog: both languages always listed (Unicode installer), text shown in both languages because none is chosen yet.
!define MUI_LANGDLL_ALLLANGUAGES
!define MUI_LANGDLL_WINDOWTITLE "Installer Language / Kurulum Dili"
!define MUI_LANGDLL_INFO "Please select the installer language.$\r$\nLütfen kurulum dilini seçin."

!define MUI_FINISHPAGE_RUN "$INSTDIR\${APPEXE}"
!define MUI_FINISHPAGE_RUN_TEXT "$(TXT_RUN_APP)"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
; Kaldırıcı onay sayfasının üst metni (iki dilli): OEM profil dosyasına dokunulmadığını KALDIRMADAN ÖNCE söyler.
!define MUI_UNCONFIRMPAGE_TEXT_TOP "$(TXT_UNINST_TOP)"
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; The first language is the fallback when the system language is neither English nor Turkish: English is the primary language.
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Turkish"

; ------------------------------------------------------------------------------------------------ texts / metinler
LangString SEC_MAIN       ${LANG_TURKISH} "Uygulama (zorunlu)"
LangString SEC_MAIN       ${LANG_ENGLISH} "Application (required)"
LangString SEC_STARTMENU  ${LANG_TURKISH} "Başlat menüsü kısayolu"
LangString SEC_STARTMENU  ${LANG_ENGLISH} "Start Menu shortcut"
LangString SEC_DESKTOP    ${LANG_TURKISH} "Masaüstü kısayolu"
LangString SEC_DESKTOP    ${LANG_ENGLISH} "Desktop shortcut"

LangString DESC_MAIN      ${LANG_TURKISH} "Uygulama dosyaları, lisans ve kaldırıcı. Sürücü KURULMAZ; OEM Control Center'a dokunulmaz. OEM profil dosyası (UserPofiles) yalnız uygulamada bir profile tıkladığınızda güncellenir."
LangString DESC_MAIN      ${LANG_ENGLISH} "Application files, license and uninstaller. No driver is installed; the OEM Control Center is not touched. The OEM profile file (UserPofiles) is updated only when you click a profile in the app."
LangString DESC_STARTMENU ${LANG_TURKISH} "Başlat menüsüne ${APPNAME} ve Kaldır kısayollarını ekler."
LangString DESC_STARTMENU ${LANG_ENGLISH} "Adds ${APPNAME} and Uninstall shortcuts to the Start Menu."
LangString DESC_DESKTOP   ${LANG_TURKISH} "Masaüstüne ${APPNAME} kısayolu ekler."
LangString DESC_DESKTOP   ${LANG_ENGLISH} "Adds a ${APPNAME} shortcut to the desktop."

LangString TXT_RUN_APP     ${LANG_TURKISH} "${APPNAME} uygulamasını şimdi başlat"
LangString TXT_RUN_APP     ${LANG_ENGLISH} "Start ${APPNAME} now"

LangString TXT_PUBLISHER   ${LANG_TURKISH} "Topluluk aracı"
LangString TXT_PUBLISHER   ${LANG_ENGLISH} "Community tool"
LangString LNK_UNINSTALL   ${LANG_TURKISH} "Kaldır"
LangString LNK_UNINSTALL   ${LANG_ENGLISH} "Uninstall"

; v1.4.0: her profil tıklaması seçimi OEM profil dosyasına yazar. Kaldırıcı o dosyaya ve yedeklerine DOKUNMAZ; bunu açıkça söyler.
; v1.4.0: every profile click writes the selection into the OEM profile file. The uninstaller does NOT touch that file or its backups, and says so.
; NOT: Onay sayfasındaki metin alanı yaklaşık 3 satırdır; daha uzun metin KIRPILIR (Wine görüntüsünde doğrulandı) -> kısa tutuldu.
; Geri alma yönergesi (kaldırmadan ÖNCE 140 W profilini seçmek ya da .bak yedeğini elle geri koymak) README.md'de ve kaldırma ayrıntı günlüğünde.
LangString TXT_UNINST_TOP  ${LANG_TURKISH} "${APPNAME} kaldırılacak; önce uygulamayı kapatın. NOT: OEM profil dosyasına (UserPofiles) ve yedeklerine dokunulmaz; kaydedilmiş GPU güç değeri OEM hizmetinde geçerli kalır."
LangString TXT_UNINST_TOP  ${LANG_ENGLISH} "${APPNAME} will be uninstalled; close the application first. NOTE: the OEM profile file (UserPofiles) and its backups are not touched; a saved GPU power value stays in effect via the OEM service."
LangString TXT_UNINST_OEM  ${LANG_TURKISH} "OEM profil dosyasına (UserPofiles) ve yedeklerine dokunulmadı. Seçtiğiniz GPU güç profili OEM profil dosyasında durduğu için OEM hizmeti tarafından geçerli kalır. OEM varsayılanına dönmek için yedek (.bak) dosyasını elle geri koyabilir ya da kaldırmadan önce uygulamada 140 W profilini seçebilirsiniz (README.tr.md: 'Elle geri dönüş')."
LangString TXT_UNINST_OEM  ${LANG_ENGLISH} "The OEM profile file (UserPofiles) and its backups were not touched. The GPU power profile you selected stays in the OEM profile file, so the OEM service keeps applying it. To return to the OEM default, put a backup (.bak) file back by hand, or select the 140 W profile in the app before uninstalling (README.md: 'Manual rollback')."
; v1.3.0: kaldırıcı iki Görev Zamanlayıcı görevini siler ve silindiklerini doğrular; doğrulanamazsa bunu açıkça yazar (sessizce geçmez).
; v1.3.0: the uninstaller removes both Task Scheduler tasks and verifies it; when that cannot be verified it says so instead of staying silent.
LangString TXT_INST_LEGACY ${LANG_TURKISH} "UYARI: Eski sürümlerden kalan 'MonsterGpuPower_ApplyAtLogon' görevi silinemedi ya da silindiği doğrulanamadı; Görev Zamanlayıcı'dan elle silebilirsiniz."
LangString TXT_INST_LEGACY ${LANG_ENGLISH} "WARNING: the 'MonsterGpuPower_ApplyAtLogon' task left by earlier versions could not be removed, or its removal could not be verified; you can delete it by hand in Task Scheduler."
LangString TXT_UNINST_TASKS ${LANG_TURKISH} "UYARI: Görev Zamanlayıcı'daki 'MonsterGpuPower_Autostart' ve/veya 'MonsterGpuPower_ApplyAtLogon' görevi silinemedi ya da silindiği doğrulanamadı. Görev Zamanlayıcı'yı açıp bu görevleri elle silebilirsiniz."
LangString TXT_UNINST_TASKS ${LANG_ENGLISH} "WARNING: the Task Scheduler task 'MonsterGpuPower_Autostart' and/or 'MonsterGpuPower_ApplyAtLogon' could not be removed, or its removal could not be verified. You can delete these tasks by hand in Task Scheduler."

; ------------------------------------------------------------------------------------------------ helpers / yardımcılar
; Uygulama çalışıyorsa (profil yazımı sürüyor olabilir) zorla kapatmak yerine kullanıcıdan kapatmasını iste.
; If the app is running (a profile write may be in progress) ask the user to exit it instead of killing it.
; NOTE: inside .onInit / un.onInit NSIS still resolves $(LangStrings) with the language table that was active at startup
; (it is re-selected from $LANGUAGE only after the callback returns), so this message picks its text from $LANGUAGE itself.
!define MSG_RUNNING_TR "${APPNAME} çalışıyor. Lütfen önce uygulamadan çıkış yapın (pencere ya da tepsi simgesi > Çıkış), sonra bu işlemi yeniden başlatın."
!define MSG_RUNNING_EN "${APPNAME} is running. Please exit it first (window or tray icon > Exit), then start this operation again."
!macro CheckRunning
  System::Call 'kernel32::OpenMutexW(i 0x100000, i 0, w "Local\MonsterGpuPower.SingleInstance") p .R0'
  ${If} $R0 != 0
    System::Call 'kernel32::CloseHandle(p $R0)'
    ${If} $LANGUAGE == ${LANG_TURKISH}
      MessageBox MB_OK|MB_ICONEXCLAMATION "${MSG_RUNNING_TR}" /SD IDOK
    ${Else}
      MessageBox MB_OK|MB_ICONEXCLAMATION "${MSG_RUNNING_EN}" /SD IDOK
    ${EndIf}
    Abort
  ${EndIf}
!macroend

Function un.onInit
  SetRegView 64
  ReadRegStr $R0 HKLM "${APPKEY}" "Language"   ; language chosen at install time
  ${If} $R0 == "1033"
  ${OrIf} $R0 == "1055"
    StrCpy $LANGUAGE $R0
  ${EndIf}
  !insertmacro CheckRunning
FunctionEnd

; ------------------------------------------------------------------------------------------------ sections / bölümler
Section "$(SEC_MAIN)" SecMain
  SectionIn RO                 ; mandatory: cannot be unchecked / zorunlu bileşen
  SetRegView 64                ; 64-bit application: 64-bit registry view
  SetShellVarContext all
  SetOutPath "$INSTDIR"
  File "..\build\${APPEXE}"
  File "..\LICENSE"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  ; v1.0-v1.3 kurulumlarının Beni oku dosyaları artık yok (v1.4.0'da README GitHub'dadır); eski özellikleri anlatan bayat metin kalmasın.
  Delete "$INSTDIR\README_TR.md"
  Delete "$INSTDIR\README_EN.md"
  ; v1.0-v1.3'ten kalan "Windows açılışında uygula" görevi v1.4.0'da yoktur: kurulum onu siler (yoksa sorun değil; uygulamanın otomatik başlatma görevine dokunulmaz).
  ; The "apply at Windows startup" task left by v1.0-v1.3 does not exist in v1.4.0: setup removes it (fine if absent; the app's autostart task is NOT touched).
  ExecWait '"$INSTDIR\${APPEXE}" /apply-startup' $0
  ${If} $0 != 0
    DetailPrint "$(TXT_INST_LEGACY)"
  ${EndIf}

  WriteRegStr HKLM "${APPKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${APPKEY}" "Language" "$LANGUAGE"
  WriteRegStr HKLM "${UNKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKLM "${UNKEY}" "DisplayVersion" "${APPVER}"
  WriteRegStr HKLM "${UNKEY}" "DisplayIcon" "$INSTDIR\${APPEXE}"
  WriteRegStr HKLM "${UNKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNKEY}" "Publisher" "$(TXT_PUBLISHER)"
  WriteRegDWORD HKLM "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNKEY}" "NoRepair" 1
SectionEnd

; Optional shortcuts (default: both checked; the default also applies to silent installs).
Section "$(SEC_STARTMENU)" SecStartMenu
  SetShellVarContext all       ; Start Menu: all users
  SetOutPath "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  ; an earlier install in the other language may have left the other uninstall shortcut name behind
  Delete "$SMPROGRAMS\${APPNAME}\Kaldır.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Uninstall.lnk"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${APPEXE}" "" "$INSTDIR\${APPEXE}" 0
  CreateShortcut "$SMPROGRAMS\${APPNAME}\$(LNK_UNINSTALL).lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "$(SEC_DESKTOP)" SecDesktop
  SetShellVarContext all       ; Desktop: all users
  SetOutPath "$INSTDIR"
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${APPEXE}" "" "$INSTDIR\${APPEXE}" 0
SectionEnd

; (placed after the sections: it refers to ${SecDesktop} / ${SecStartMenu})
; Setup.exe /S /LANG=EN /NODESKTOP /NOSTARTMENU  (default: both shortcuts are created)
Function .onInit
  ; 1) explicit /LANG=EN|TR  2) interactive: language dialog  3) silent without /LANG: system language
  StrCpy $R2 0                       ; 1 = language fixed by the command line
  ${GetParameters} $R1
  ClearErrors
  ${GetOptions} $R1 "/LANG=" $R0
  ${IfNot} ${Errors}
    ${If} $R0 == "EN"
    ${OrIf} $R0 == "ENGLISH"
    ${OrIf} $R0 == "1033"
      StrCpy $LANGUAGE ${LANG_ENGLISH}
      StrCpy $R2 1
    ${ElseIf} $R0 == "TR"
    ${OrIf} $R0 == "TURKISH"
    ${OrIf} $R0 == "1055"
      StrCpy $LANGUAGE ${LANG_TURKISH}
      StrCpy $R2 1
    ${EndIf}
  ${EndIf}
  ${If} $R2 == 0
    !insertmacro MUI_LANGDLL_DISPLAY  ; does nothing in silent mode
  ${EndIf}

  !insertmacro CheckRunning
  ${GetParameters} $R1
  ClearErrors
  ${GetOptions} $R1 "/NODESKTOP" $R0
  ${IfNot} ${Errors}
    !insertmacro UnselectSection ${SecDesktop}
  ${EndIf}
  ClearErrors
  ${GetOptions} $R1 "/NOSTARTMENU" $R0
  ${IfNot} ${Errors}
    !insertmacro UnselectSection ${SecStartMenu}
  ${EndIf}
FunctionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "$(DESC_MAIN)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "$(DESC_STARTMENU)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "$(DESC_DESKTOP)"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  SetRegView 64
  SetShellVarContext all
  ; Remove BOTH Task Scheduler tasks first (the "start the app at logon" task and the old v1.0-v1.2 apply task; fine if they do not exist). The shared OEM driver and the Control Center are NOT touched.
  ExecWait '"$INSTDIR\${APPEXE}" /remove-startup' $0
  ${If} $0 != 0
    ; The warning must be SEEN: the details list is collapsed by default and the last DetailPrint replaces the status line. Silent mode (/S) skips the box (/SD IDOK).
    DetailPrint "$(TXT_UNINST_TASKS)"
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(TXT_UNINST_TASKS)" /SD IDOK
  ${EndIf}
  Delete "$INSTDIR\${APPEXE}"
  ; (v1.0-v1.3 kurulumlarından kalan Beni oku dosyaları)
  Delete "$INSTDIR\README_TR.md"
  Delete "$INSTDIR\README_EN.md"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\Uninstall.exe"
  Delete "$INSTDIR\settings.ini"
  Delete "$INSTDIR\settings.ini.tmp"
  Delete "$INSTDIR\startup_task.tmp.xml"
  RMDir "$INSTDIR"
  ; Deleting shortcuts is safe even if they were not selected at install time (missing files are ignored). Both language variants.
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Kaldır.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Uninstall.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"
  Delete "$DESKTOP\${APPNAME}.lnk"
  DeleteRegKey HKLM "${UNKEY}"
  DeleteRegKey HKLM "${APPKEY}"
  ; Settings and log folder (data of this application only)
  SetShellVarContext current
  RMDir /r "$LOCALAPPDATA\MonsterGpuPower"
  ; The OEM profile file / backups under Program Files\OEM are deliberately NEVER touched here.
  DetailPrint "$(TXT_UNINST_OEM)"
SectionEnd
