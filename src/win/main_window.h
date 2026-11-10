// Ana pencere + tepsi + uygulama akışı. Arayüz iş parçacığı hiçbir zaman EC/NVML/OEM dosyasına dokunmaz;
// tüm donanım ve dosya işleri Worker üzerinde çalışır, sonuçlar PostMessage ile döner.
//
// Dosyalar: main_window.cpp (pencere, yerleşim, çizim, tepsi, çıkış), main_window_apply.cpp (profil seçimi -> kalıcı uygulama akışı),
// main_window_persist.cpp (OEM profil dosyasının salt okunur denetimi), main_window_tasks.cpp (Görev Zamanlayıcı: açılışta başlat).
#pragma once
#include <string>

#include "../core/oem_profile.h"
#include "../core/persist_view.h"
#include "../core/settings.h"
#include "backends.h"
#include "strings_tr.h"
#include "ui_controls.h"
#include "worker.h"

namespace mgp {
namespace win {

class MainWindow {
 public:
  MainWindow(HINSTANCE hi, Backends& be, ILog& log, const Settings& settings);
  ~MainWindow();
  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;

  bool Create(bool startHidden);
  // Mesaj döngüsünden: Tab / ok tuşları / F5. true dönerse mesaj işlenmiştir.
  bool PreTranslate(MSG* m);
  HWND hwnd() const { return hwnd_; }

  static const wchar_t* ClassName();
  static UINT ShowMeMessage();  // ikinci örnek, çalışan pencereyi öne getirmek için gönderir

 private:
  struct ApplyDone;  // WM_APPLYDONE yükü (main_window_apply.cpp)
  struct OemMsg;     // WM_OEMSTATE yükü (main_window_persist.cpp)
  struct TaskMsg;    // WM_TASKDONE yükü (main_window_tasks.cpp)
  enum class TaskOp { Probe, EnableAutostart, DisableAutostart };
  static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
  LRESULT WndProc(UINT, WPARAM, LPARAM);

  // --- oluşturma / yerleşim
  void CreateChildren();
  void Layout();
  void ResizeForDpi(bool center);
  void ApplyLayoutDpi(int dpi);  // yerleşim ölçeğini (yazı tipleri dahil) değiştirir ve denetimleri yeniden biçimlendirir
  void ApplyDwm();
  void LoadIcons();

  // --- çizim
  void OnPaint();
  void PaintAll(ui::Canvas& cv);
  tr::Notice CurrentNotice() const;
  void PaintCard(ui::Canvas& cv);
  void PaintNotice(ui::Canvas& cv);
  void PaintPersist(ui::Canvas& cv);

  // --- durum
  void UpdateControls();
  void Refresh() {
    UpdateControls();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
  void RequestPoll(bool forceFull, bool userRefresh = false);
  void RearmPoll();
  void OnState(LiveState* s);

  // --- komutlar
  void OnCommand(int id);
  void OnAbout();
  void ShowMe();

  // --- profil seçimi = kalıcı uygulama (win/main_window_apply.cpp)
  void OnProfileClick(int idx);
  bool EnsureConsentsFor(int watts);  // onay iletişim kutuları; false = vazgeçildi
  // exitRestore=true: çıkışta YALNIZ anlık (EC + NVIDIA) değer OEM 140 W'a döner; OEM dosyasına DOKUNULMAZ. Aksi hâlde işlem her zaman
  // kalıcıdır: OEM ön kontrol -> EC+NVML -> OEM JSON güvenli commit. approvedSha/foreignApproved: kullanıcı, OEM dosyasındaki bu uygulamanın
  // yazmadığı değerin TAM bu dosya sürümü için üzerine yazılmasını onayladı (işlem çekirdeği başka sürümde işlemi iptal eder).
  void StartApply(int watts, bool exitRestore, const std::string& approvedSha = std::string(), bool foreignApproved = false);
  void OnApplyDone(ApplyDone* d);
  void OnForeignApproval(ApplyDone& d);  // OEM dosyasında yabancı değer: hiçbir şey uygulanmadı; kullanıcıya sorulur (varsayılan: Hayır)
  void OnResetConsents();
  void BlockShutdown(bool on);

  // --- OEM profil dosyası durumu (win/main_window_persist.cpp)
  void RequestOemInspect();
  void OnOemState(OemMsg* m);
  PersistView CurrentPersistView() const;
  void StoreOemInspection(oem::Inspection insp);
  tr::Notice PersistStatusNotice() const;

  // --- Görev Zamanlayıcı: "Uygulamayı açılışta başlat" (win/main_window_tasks.cpp)
  // COM çağrıları arayüzü bloklamasın diye Worker'da çalışır; anahtarın durumu GERÇEK görevden türetilir (ayar dosyasından DEĞİL).
  void RequestTaskProbe();
  void PostTaskOp(TaskOp op);
  void OnTaskDone(TaskMsg* m);
  void OnAutostartToggle(bool want);

  // --- tepsi / çıkış
  void HideToTray();
  void RestoreFromTray();
  bool AddTray();
  void RemoveTray();
  void AllowShellMessages();  // yükseltilmiş süreçte "TaskbarCreated" iletisine UIPI izni
  void ArmTrayRetry();
  void OnTrayRetry();
  void UpdateTrayTip();
  void ShowTrayMenu();
  void RequestExit();
  void DoExit(DWORD workerWaitMs = 20000);
  bool IsShown() const;
  int IndexOfProfile(HWND h) const;
  int S(int v) const { return ui::S(v, dpi_); }

  HINSTANCE hi_;
  Backends& be_;
  ILog& log_;
  Settings settings_;

  HWND hwnd_ = nullptr;
  HWND prof_[6] = {};
  HWND chkAutostart_ = nullptr, chkRestore_ = nullptr;
  HWND btnRefresh_ = nullptr, btnConsent_ = nullptr, btnAbout_ = nullptr;
  ui::Fonts fonts_;
  int dpi_ = 96;     // YERLEŞİM ölçeği (S() ve yazı tipleri bununla); ekrana sığmıyorsa sysDpi_'den küçüktür
  int sysDpi_ = 96;  // pencerenin bulunduğu monitörün gerçek DPI'ı (çerçeve/başlık ölçüleri için)
  HICON iconBig_ = nullptr, iconSmall_ = nullptr;

  RECT rcCard_ = {}, rcNotice_ = {}, rcDesc_[2] = {}, rcFooter_ = {};
  RECT rcPersist_ = {}, rcPersistTitle_ = {}, rcPersistDesc_ = {}, rcPersistStatus_ = {};
  int persistSepY_ = 0;
  int clientH_ = 600;
  std::wstring lastLoggedNotice_;   // son günlüğe yazılan sonuç bildirimi (değişimde bir kez günlüğe yazılır)
  std::wstring lastPersistStatus_;  // kartın son durum satırı (değişimi günlüğe yazılır: destek)

  Worker worker_;
  LiveState cur_;
  bool haveState_ = false;
  bool pollInFlight_ = false;
  bool applyInFlight_ = false;
  int applyTarget_ = 0;
  bool applyIsExit_ = false;  // sürmekte olan uygulama çıkıştaki OEM 140 W dönüşü (yalnız anlık)
  bool exitPending_ = false;
  bool exiting_ = false;
  int pollCount_ = 0;
  bool trayShown_ = false;
  int trayRetries_ = 0;  // tepsi simgesi eklenemedikçe (görev çubuğu henüz hazır değil) sınırlı yeniden deneme
  UINT taskbarCreated_ = 0;
  tr::Notice resultNotice_;

  // --- OEM profil dosyası durumu (yalnız arayüz iş parçacığı; depo nesnesi YALNIZ Worker'dan kullanılır)
  oem::Inspection oemInsp_;  // en son GERÇEK denetim
  bool haveOem_ = false;
  bool oemInspInFlight_ = false;

  // --- Görev Zamanlayıcı durumu (yalnız arayüz iş parçacığı; COM çağrıları Worker'da)
  bool autostartOn_ = false;     // GERÇEK görevden türetilmiş: kayıtlı + etkin + bu kopyanın yolu + /minimized
  bool taskOpInFlight_ = false;  // sorgu / oluşturma / silme sürüyor
  int autostartPending_ = -1;    // kullanıcı anahtarı çevirdi, işlem sürüyor: -1 yok, 0/1 istenen durum (anahtar görsel olarak ona gider)
};

}  // namespace win
}  // namespace mgp
