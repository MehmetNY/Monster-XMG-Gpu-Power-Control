// MainWindow'un kaynak dosyaları arasında paylaşılan ileti, zamanlayıcı ve denetim numaraları.
#pragma once
#include "win_util.h"

namespace mgp {
namespace win {
namespace ids {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_STATE = WM_APP + 2;      // canlı durum okuması (worker -> arayüz)
constexpr UINT WM_APPLYDONE = WM_APP + 3;  // profil uygulama sonucu (worker -> arayüz)
constexpr UINT WM_OEMSTATE = WM_APP + 4;   // OEM profil denetimi sonucu (worker -> arayüz)
constexpr UINT WM_TASKDONE = WM_APP + 5;   // Görev Zamanlayıcı işlemi sonucu (worker -> arayüz)
enum : UINT_PTR { IDT_POLL = 1, IDT_EXITGUARD = 2, IDT_RESUME = 3, IDT_TRAYRETRY = 4 };

enum : int {
  IDC_PROF0 = 1001,  // 6 profil düğmesi: 1001..1006
  IDC_CHK_AUTOSTART = 1101,
  IDC_CHK_RESTORE,
  IDC_BTN_REFRESH = 1201,
  IDC_BTN_CONSENT,
  IDC_BTN_ABOUT
};

}  // namespace ids
}  // namespace win
}  // namespace mgp
