// Profil seçimi = kalıcı uygulama (ana pencere tarafı).
//
//  * Her profil tıklaması KALICIDIR: OEM ön kontrol -> EC + NVML doğrulaması -> OEM profil dosyasına (Mode3_Profile1.json,
//    GPU.ConfigurableTGPTarget) güvenli yazım + geri okuma. Geçici (yalnız anlık) uygulama yoktur.
//  * OEM profil dosyası kullanılamıyorsa HİÇBİR ŞEY uygulanmaz (ne EC ne OEM dosyası).
//  * OEM dosyasına yazma YALNIZ kullanıcının profil tıklamasından gelir; çıkışta OEM 140 W'a dönüş yalnız anlık değeri değiştirir.
//  * İki katman (a: EC + NVIDIA, b: OEM dosyası) kullanıcıya AYRI raporlanır; biri doğrulanamadan "tam başarı" denmez.
//  * Bu özellik firmware/BIOS/EC'ye kalıcı yazım DEĞİLDİR; OEM GCUService'in kendi profil dosyasına dayanır ve o servis gereklidir.
#include <memory>

#include "main_window.h"
#include "main_window_ids.h"
#include "settings_store.h"

namespace mgp {
namespace win {

using namespace ids;
using txt::Level;
using txt::Notice;

struct MainWindow::ApplyDone {
  ApplyResult r;                // anlık katman (kalıcı akışta txn.live ile aynı)
  bool exitRestore = false;     // çıkışta OEM 140 W'a dönüş (yalnız anlık)
  bool haveState = false;       // r.after arayüze canlı durum olarak alınabilir
  bool oemLockBusy = false;     // OEM dosyasının çapraz-süreç kilidi alınamadı: hiçbir şey uygulanmadı
  PersistTxnResult txn;         // iki katmanlı işlem sonucu (kalıcı akışta)
  bool haveOemAfter = false;
  oem::Inspection oemAfter;     // işlem SONRASI gerçek OEM durumu
};

// ======================================================================= onaylar
// Profil tıklamasının onay iletişim kutuları. false = kullanıcı vazgeçti.
bool MainWindow::EnsureConsentsFor(int watts) {
  const ProfileDef* p = FindProfile(watts);
  if (!p) return false;
  if (!cur_.identity.boardConfirmed && !settings_.consentModel) {
    if (MessageBoxW(hwnd_, txt::kConsentModelText(), txt::kAppTitle, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return false;
    settings_.consentModel = true;
    SaveSettings(settings_);
  }
  if (p->watts > kOemMaxWatts && !settings_.consentOverOem) {
    if (MessageBoxW(hwnd_, txt::kConsentOverOemText(), txt::kAppTitle, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return false;
    settings_.consentOverOem = true;
    SaveSettings(settings_);
  }
  if (p->evidence == Evidence::ExpectedUnverified && !settings_.consentUnverified) {
    if (MessageBoxW(hwnd_, txt::kConsentUnverifiedText(), txt::kAppTitle, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return false;
    settings_.consentUnverified = true;
    SaveSettings(settings_);
  }
  return true;
}

void MainWindow::OnResetConsents() {
  settings_.consentOverOem = settings_.consentUnverified = settings_.consentModel = false;
  SaveSettings(settings_);
  SetNotice([]() { return txt::ConsentsResetNotice(); });
  log_.Write(LogLevel::Info, "UI: consents reset");
  Refresh();
}

// ======================================================================= profil seçimi
void MainWindow::OnProfileClick(int idx) {
  const ProfileDef& p = kProfiles[idx];
  if (applyInFlight_ || exiting_) return;
  if (!haveState_) {
    SetNotice([]() { return txt::StateReadingNotice(); });
    Refresh();
    return;
  }
  if (!cur_.CanWrite()) {
    {
      const LiveState snap = cur_;
      SetNotice([snap]() { return txt::FirstBlockerNotice(snap); });
    }
    Refresh();
    return;
  }
  if (haveOem_ && !oemInsp_.ok) {
    // OEM profil dosyası kullanılamıyor: profil yalnız kalıcı uygulandığı için hiçbir şey yapılmaz ve gereksiz onay sorulmaz.
    {
      const oem::Lock lk = oemInsp_.lock;
      const std::string ver = oemInsp_.svc.versionRead ? oemInsp_.svc.version : std::string();
      SetNotice([lk, ver]() { return txt::PersistUnavailableNotice(lk, ver); });
    }
    log_.Write(LogLevel::Warn, std::string("UI: ") + std::to_string(p.watts) + " W not applied: OEM profile module locked (" +
                                   oem::LockName(oemInsp_.lock) + ")");
    Refresh();
    return;
  }
  // Seçilen değer OEM dosyasında kalıcı tutulacağı için (zaten etkin olsa bile) >140 W / canlı doğrulanmamış profil onayı her zaman istenir.
  if (!EnsureConsentsFor(p.watts)) return;
  // Onay iletişim kutusu açıkken durum değişmiş olabilir.
  if (applyInFlight_ || exiting_ || !haveState_ || !cur_.CanWrite()) return;
  log_.Write(LogLevel::Info, "UI: user requested " + std::to_string(p.watts) + " W (persistent)");
  StartApply(p.watts, false);
}

void MainWindow::StartApply(int watts, bool exitRestore, const std::string& approvedSha, bool foreignApproved) {
  if (applyInFlight_) return;
  applyInFlight_ = true;
  applyTarget_ = watts;
  applyIsExit_ = exitRestore;
  ClearNotice();
  ApplyOptions opts;
  opts.consentOverOem = settings_.consentOverOem;
  opts.consentUnverified = settings_.consentUnverified;
  opts.consentModel = settings_.consentModel;
  HWND hw = hwnd_;
  Backends* be = &be_;
  ILog* logp = &log_;
  const OemOwnership own = settings_.oem;  // işlem anındaki sahiplik kanıtı: yabancı değer tespiti buna dayanır
  BlockShutdown(true);
  worker_.Post([hw, be, logp, watts, opts, exitRestore, own, approvedSha, foreignApproved]() {
    std::unique_ptr<ApplyDone> d(new ApplyDone());
    d->exitRestore = exitRestore;
    d->r.target = watts;
    try {
      NamedLock lock(kApplyLockName, 20000);  // başka bir MONSTER GPU POWER örneğiyle aynı anda yazmayı engeller
      if (lock.acquired() && lock.abandoned()) logp->Write(LogLevel::Warn, "apply lock was abandoned by an earlier process that ended while holding it");
      if (!lock.acquired()) {
        d->r.outcome = Outcome::BlockedNotReady;
        d->r.detail = "apply lock busy";
        d->txn.live = d->r;
      } else if (exitRestore) {
        // Çıkışta OEM 140 W'a dönüş: YALNIZ anlık değer; OEM profil dosyasına dokunulmaz.
        d->r = be->engine().Apply(watts, opts);
        d->txn.live = d->r;
        d->haveState = d->r.outcome != Outcome::BlockedUnknownProfile;
      } else {
        // Kilit sırası HER ZAMAN Apply -> OEM. Tüm işlem (ön denetim, EC+NVML, OEM yazımı, geri okuma) iki kilit altında yürür.
        NamedLock oemLock(kOemLockName, 5000);
        if (oemLock.acquired() && oemLock.abandoned()) logp->Write(LogLevel::Warn, "OEM profile lock was abandoned by an earlier process that ended while holding it");
        if (!oemLock.acquired()) {
          d->oemLockBusy = true;
          d->r.outcome = Outcome::BlockedNotReady;
          d->r.detail = "OEM profile lock busy";
          d->txn.live = d->r;
        } else {
          PersistTxnInput in;
          in.watts = watts;
          in.opts = opts;
          in.own = own;
          in.approvedSha = approvedSha;
          in.foreignApproved = foreignApproved;
          d->txn = RunPersistTransaction(be->engine(), be->oemStore(), *logp, in);
          d->r = d->txn.live;
          d->haveState = !d->txn.aborted && !d->txn.needsApproval && !d->txn.oemUnavailable && d->r.outcome != Outcome::BlockedUnknownProfile;
          // İşlem SONRASI gerçek OEM durumu: sahiplik kanıtı ve arayüz bundan türetilir. Bu okumadaki bir istisna, işlemin KENDİ
          // (tamamlanmış) sonucunu bozmamalı: ayrı try.
          try {
            d->oemAfter = be->oemStore().Inspect();
            d->haveOemAfter = true;
          } catch (...) {
            d->haveOemAfter = false;
            logp->Write(LogLevel::Warn, "post-transaction OEM inspection failed with an exception; the transaction result itself is unaffected");
          }
        }
      }
    } catch (...) {
      // Motor kendi içinde yakalar; buraya düşen yalnız kilit/ayırma hatalarıdır. Tamamlanma iletisi her durumda gönderilir.
      d->r.target = watts;
      d->r.outcome = Outcome::BlockedNotReady;
      d->r.detail = "internal exception before apply";
      d->txn = PersistTxnResult();
      d->txn.live = d->r;
      d->haveState = false;
    }
    ApplyDone* raw = d.release();
    if (!PostMessageW(hw, WM_APPLYDONE, 0, reinterpret_cast<LPARAM>(raw))) delete raw;
  });
  Refresh();
}

void MainWindow::OnApplyDone(ApplyDone* dp) {
  std::unique_ptr<ApplyDone> d(dp);
  applyInFlight_ = false;
  BlockShutdown(false);
  if (!d) {  // gönderici tamamlanma iletisini oluşturamadı: arayüz "uygulanıyor"da takılı kalmasın
    log_.Write(LogLevel::Error, "UI: apply completion payload missing");
    SetNotice([]() { return txt::ApplyResultLostNotice(); });
    exitPending_ = false;
    RequestPoll(true, false);
    Refresh();
    return;
  }
  const ApplyResult& r = d->r;
  log_.Write(LogLevel::Info, std::string("UI: apply finished outcome=") + OutcomeName(r.outcome) + " target=" + std::to_string(r.target));

  if (d->exitRestore) {
    KillTimer(hwnd_, IDT_EXITGUARD);
    if (d->haveState) {
      cur_ = r.after;
      haveState_ = true;
    }
    if (r.Succeeded()) {
      DoExit();
      return;
    }
    // Çıkışta OEM 140 W'a dönüş başarısız: kullanıcı bunu bilmeden uygulama sessizce kapanmaz.
    SetNotice([r]() { return txt::ApplyNotice(r); });
    if (!IsShown()) RestoreFromTray();
    Refresh();
    const std::wstring msg = std::wstring(txt::kRestoreOnExitFailed()) + L"\n\n" + resultNotice_.text + L"\n\n" + txt::kExitAnywayQuestion();
    exitPending_ = false;
    const int ans = MessageBoxW(hwnd_, msg.c_str(), txt::kAppTitle, MB_YESNO | MB_ICONERROR | MB_DEFBUTTON2);
    if (ans == IDYES) {
      log_.Write(LogLevel::Warn, "exit: user chose to exit although OEM 140 W restore failed");
      DoExit();
      return;
    }
    UpdateTrayTip();
    RearmPoll();
    Refresh();
    return;
  }

  if (d->oemLockBusy) {
    SetNotice([]() { return txt::PersistLockBusyNotice(); });
    log_.Write(LogLevel::Warn, "UI: OEM profile lock busy; nothing applied");
    RequestPoll(true);
    Refresh();
    if (exitPending_) {
      exitPending_ = false;
      RequestExit();
    }
    return;
  }

  const TxnClass cls = ClassifyTxn(d->txn);
  log_.Write(LogLevel::Info, std::string("UI: persist txn class=") + std::to_string(static_cast<int>(cls)) +
                                 (d->txn.persistAttempted ? std::string(" oem_commit=") + std::to_string(static_cast<int>(d->txn.oem.kind)) : std::string()));
  if (cls == TxnClass::NeedsApproval) {
    OnForeignApproval(*d);  // hiçbir şey uygulanmadı; kullanıcıya sorulur
    return;
  }
  if (d->haveState) {
    // Geri alma denendiyse GERÇEK son durum geri almanın sonucudur; okunamadıysa tam okuma istenir (aşağıda).
    cur_ = (d->txn.liveRestoreAttempted && d->txn.liveRestore.after.ec.ok) ? d->txn.liveRestore.after : r.after;
    haveState_ = true;
  }

  const bool restored = d->txn.liveRestoreAttempted;
  const bool critical = r.outcome == Outcome::FailedRollbackUnverified || cls == TxnClass::OemFailedRestoreUnverified;
  bool appendMetaFailed = false, appendNoOwnership = false;

  if (cls == TxnClass::BothOk) {
    // Sahiplik kanıtı: işlem SONRASI gerçek denetimden (SHA-256). YALNIZ bu işlemin yazıp doğruladığı dosya için: işlemden sonra başka bir
    // program dosyayı değiştirdiyse o sürüm "bizim" sayılmaz ve eski kanıt (artık dosyayla uyuşmaz) tutulmaz.
    const OemOwnership own = d->haveOemAfter ? OwnershipFromTxn(d->oemAfter, d->txn) : OemOwnership();
    if (d->haveOemAfter && !own.Present())
      log_.Write(LogLevel::Warn, "UI: the OEM profile differs from the version this transaction verified; ownership is NOT recorded");
    if (own.Present()) settings_.oem = own;
    else settings_.oem.Clear();
    if (!SaveSettings(settings_)) appendMetaFailed = true;
    if (!own.Present()) appendNoOwnership = true;
  }
  // Bildirim üretici işlev olarak saklanır: kullanıcı dili değiştirirse sonuç yeni dilde yeniden üretilir.
  {
    const PersistTxnResult txn = d->txn;
    SetNotice([cls, txn, appendMetaFailed, appendNoOwnership]() {
      Notice n = txt::PersistTxnNotice(cls, txn);
      if (appendMetaFailed) {
        n.text += txt::kPersistMetaSaveFailed();
        if (n.level == Level::Info) n.level = Level::Warn;
      }
      if (appendNoOwnership) {
        n.text += txt::kPersistNoOwnership();
        if (n.level == Level::Info) n.level = Level::Warn;
      }
      return n;
    });
  }
  // BothOkAlready: dosyaya yazılmadı; sahiplik kanıtı olduğu gibi kalır (yabancı bir değeri sahiplenmeyiz).

  if (d->haveOemAfter) StoreOemInspection(d->oemAfter);
  else RequestOemInspect();
  // Geri alma denendiyse veya durum belirsizse GERÇEK durum tam okumayla yeniden alınır.
  if (restored || critical) RequestPoll(true);
  if (critical) {
    // Kritik: motor yazma kilidini de tutar; kullanıcı bunu bilmeden geçmesin.
    exitPending_ = false;
    if (!IsShown()) RestoreFromTray();
    MessageBoxW(hwnd_, resultNotice_.text.c_str(), txt::kAppTitle, MB_OK | MB_ICONERROR);
  }
  UpdateTrayTip();
  RearmPoll();
  Refresh();
  if (exitPending_) {
    exitPending_ = false;
    RequestExit();
  }
}

// OEM dosyasında bu uygulamanın yazmadığı (ya da yazdığından sonra değişmiş) 140 W dışı bir değer var ve seçilen profil onu değiştirecek.
// İşlem çekirdeği hiçbir şey uygulamadı (anlık katman dahil). Kullanıcıya sorulur; varsayılan HAYIR. Evet ise işlem, soruda gösterilen
// TAM dosya sürümüne (SHA-256) sabitlenerek yeniden başlatılır: dosya bu arada değişirse çekirdek işlemi iptal eder.
void MainWindow::OnForeignApproval(ApplyDone& d) {
  const int fileW = d.txn.pre.target, wantW = d.txn.live.target;
  const std::string sha = d.txn.pre.sha;
  log_.Write(LogLevel::Info, "UI: the OEM profile holds " + std::to_string(fileW) + " W that this application did not write (" +
                                 (d.txn.foreign == ForeignKind::UnownedValue ? "no ownership record" : "changed after our last write") +
                                 "); asking before overwriting it with " + std::to_string(wantW) + " W; nothing applied yet");
  if (d.haveOemAfter) StoreOemInspection(d.oemAfter);
  bool approved = false;
  if (!exitPending_ && !exiting_) {
    if (!IsShown()) RestoreFromTray();
    const std::wstring q = txt::PersistForeignConfirm(d.txn.foreign, fileW, wantW);
    approved = MessageBoxW(hwnd_, q.c_str(), txt::kAppTitle, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
  }
  if (approved) {
    // Onay penceresi açıkken durum değişmiş olabilir: yalnız hâlâ uygunsa devam edilir (ve dosya sürümü çekirdekte yeniden denetlenir).
    if (!applyInFlight_ && !exiting_ && !exitPending_ && haveState_ && cur_.CanWrite()) {
      log_.Write(LogLevel::Info, "UI: user approved overwriting the foreign OEM value; the approved file version is pinned by SHA-256");
      StartApply(wantW, false, sha, true);
      return;
    }
    SetNotice([]() { return Notice{Level::Warn, txt::kPersistForeignStale()}; });
    RequestPoll(true);
    RequestOemInspect();
  } else {
    SetNotice([]() { return Notice{Level::Info, txt::kPersistForeignDeclined()}; });
    log_.Write(LogLevel::Info, "UI: overwriting the foreign OEM value was not approved; nothing applied");
  }
  UpdateTrayTip();
  RearmPoll();
  Refresh();
  if (exitPending_) {
    exitPending_ = false;
    RequestExit();
  }
}

void MainWindow::BlockShutdown(bool on) {
  // Yazma sürerken Windows'a "kapatmayı bekle" nedeni bildirilir (Vista+; dinamik yüklenir).
  typedef BOOL(WINAPI * Create)(HWND, LPCWSTR);
  typedef BOOL(WINAPI * Destroy)(HWND);
  static HMODULE u = GetModuleHandleW(L"user32.dll");
  if (!u || !hwnd_) return;
  if (on) {
    if (Create f = reinterpret_cast<Create>(reinterpret_cast<void*>(GetProcAddress(u, "ShutdownBlockReasonCreate"))))
      f(hwnd_, txt::kShutdownReason());
  } else {
    if (Destroy f = reinterpret_cast<Destroy>(reinterpret_cast<void*>(GetProcAddress(u, "ShutdownBlockReasonDestroy")))) f(hwnd_);
  }
}

}  // namespace win
}  // namespace mgp
