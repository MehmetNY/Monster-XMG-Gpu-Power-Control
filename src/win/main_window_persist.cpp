// OEM profil dosyasının SALT OKUNUR denetimi (ana pencere tarafı).
//
//  * Kartın durumu GERÇEK OEM denetiminden + GERÇEK EC/NVIDIA ölçümünden türetilir (ayardan DEĞİL).
//  * Dosya G/Ç yalnız Worker iş parçacığında yapılır; arayüz iş parçacığı dosyaya dokunmaz. Bu denetim hiçbir şey yazmaz.
//  * OEM dosyasına yazma YALNIZ profil seçiminde olur (bkz. main_window_apply.cpp).
#include <memory>

#include "main_window.h"
#include "main_window_ids.h"

namespace mgp {
namespace win {

using namespace ids;
using tr::Level;
using tr::Notice;

struct MainWindow::OemMsg {
  oem::Inspection insp;
};

namespace {
// Worker iş parçacığı için güvenli "denetim yapılamadı" sonucu (istisna durumunda arayüz takılı kalmasın).
oem::Inspection FailedInspection(const char* why) {
  oem::Inspection i;
  i.ok = false;
  i.lock = oem::Lock::ProfileIo;
  i.detail = why;
  return i;
}
}  // namespace

void MainWindow::StoreOemInspection(oem::Inspection insp) {
  oemInsp_ = std::move(insp);
  haveOem_ = true;
}

PersistView MainWindow::CurrentPersistView() const {
  PersistViewInput in;
  in.haveInsp = haveOem_;
  in.insp = oemInsp_;
  in.liveWatts = (haveState_ && cur_.consistent) ? cur_.activeProfile : 0;
  in.own = settings_.oem;
  PersistView v = DerivePersistView(in);
  // Anlık durumun ilk okuması henüz gelmediyse "okunamadı" demek yanıltıcıdır; okuma bitene kadar "denetleniyor" gösterilir.
  if (!haveState_ && v.kind == PersistKind::LiveUnknown) v.kind = PersistKind::Checking;
  return v;
}

Notice MainWindow::PersistStatusNotice() const {
  if (applyInFlight_) {
    return {Level::Info, applyIsExit_ ? L"OEM 140 W'a dönülüyor…"
                                      : L"Profil uygulanıyor; EC + NVIDIA doğrulanınca OEM profil dosyasına da kaydedilecek…"};
  }
  return tr::PersistStatus(CurrentPersistView());
}

void MainWindow::RequestOemInspect() {
  if (!hwnd_ || exiting_ || oemInspInFlight_) return;
  oemInspInFlight_ = true;
  HWND hw = hwnd_;
  Backends* be = &be_;
  worker_.Post([hw, be]() {
    std::unique_ptr<OemMsg> m(new OemMsg());
    try {
      m->insp = be->oemStore().Inspect();  // yalnız okur; hiçbir şey yazmaz
    } catch (...) {
      m->insp = FailedInspection("internal exception during OEM inspection");
    }
    OemMsg* raw = m.release();
    if (!PostMessageW(hw, WM_OEMSTATE, 0, reinterpret_cast<LPARAM>(raw))) delete raw;
  });
}

void MainWindow::OnOemState(OemMsg* mp) {
  std::unique_ptr<OemMsg> m(mp);
  oemInspInFlight_ = false;
  if (exiting_ || !m) return;
  // Dış değişiklik (Control Center / OEM güncellemesi) yalnız günlüğe yazılır; kartın durum satırı gerçek dosyayı zaten gösterir.
  // HİÇBİR ŞEY otomatik yazılmaz.
  if (haveOem_ && oemInsp_.haveFile && m->insp.haveFile && oemInsp_.sha != m->insp.sha) {
    log_.Write(LogLevel::Info, "OEM profile file changed: target " + std::to_string(oemInsp_.target) + " -> " + std::to_string(m->insp.target));
  }
  if (haveOem_ && oemInsp_.ok != m->insp.ok) {
    log_.Write(LogLevel::Info, std::string("OEM profile module ") + (m->insp.ok ? "became available" : std::string("became locked: ") + oem::LockName(m->insp.lock)));
  }
  StoreOemInspection(std::move(m->insp));
  Refresh();
}

}  // namespace win
}  // namespace mgp
