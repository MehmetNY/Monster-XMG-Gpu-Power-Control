// Kullanıcıya görünen TÜM Türkçe metinler tek yerde. Motorun teknik (İngilizce) ayrıntıları yalnız günlüğe gider.
#pragma once
#include <cstdio>
#include <string>

#include "../core/persist_txn.h"
#include "../core/persist_view.h"
#include "../core/power_profile_engine.h"
#include "win_util.h"

namespace mgp {
namespace win {
namespace tr {

enum class Level { Info, Warn, Error };

struct Notice {
  Level level = Level::Info;
  std::wstring text;
  bool empty() const { return text.empty(); }
};

inline std::wstring Num1(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.1f", v);
  return Widen(b);
}
inline std::wstring Watts(int w) { return std::to_wstring(w) + L" W"; }

inline const wchar_t* kAppTitle = L"MONSTER GPU POWER";
inline const wchar_t* kVersion = L"1.4.0";

inline std::wstring BlockerText(Blocker b) {
  switch (b) {
    case Blocker::IdentityUnreadable:
      return L"Cihaz kimliği (SMBIOS) okunamadı. Güvenlik için yazma kapalı.";
    case Blocker::IdentityMismatch:
      return L"Bu bilgisayar Monster Tulpar T5 V21.7 (BIOS N.1.07Mon08) olarak doğrulanamadı. "
             L"Güvenlik için bu cihazda EC'ye hiç dokunulmaz.";
    case Blocker::DriverAbsent:
      return L"Uniwill \"ACPIDriver\" sürücüsü bulunamadı (aygıt: ACPI\\INOU0000). Sürücü kurulu değil ya da çalışmıyor; "
             L"EC'ye erişilemiyor. Ayrıntı için README.md \"Sürücü bağımlılığı\".";
    case Blocker::DriverAccessDenied:
      return L"ACPIDriver aygıtına erişim reddedildi. Uygulamayı yönetici olarak çalıştırın.";
    case Blocker::DriverError:
      return L"ACPIDriver aygıtı açılırken beklenmeyen bir hata oluştu (günlüğe bakın).";
    case Blocker::EcReadInvalid:
      return L"EC güç değerleri okunamadı veya geçersiz döndü. Güvenlik için yazma kapalı.";
    case Blocker::EcStateUnexpected:
      return L"EC güç durumu tanınmıyor (başka bir yazılım değiştirmiş olabilir). Güvenlik için yazma kapalı.";
    case Blocker::NvmlUnavailable:
      return L"NVIDIA NVML kullanılamıyor; sonuç doğrulanamayacağı için yazma kapalı.";
    case Blocker::GpuMismatch:
      return L"Beklenen NVIDIA GeForce RTX 3070 Laptop GPU bulunamadı. Yazma kapalı.";
    case Blocker::MaxLimitMismatch:
      return L"NVIDIA azami güç limiti beklenen 150 W ile uyuşmuyor. Yazma kapalı.";
    case Blocker::FaultLatched:
      return L"KRİTİK: Önceki bir işlemin geri alınması doğrulanamadı. Güvenlik için yazma kapalı. Bilgisayarı yeniden başlatın "
             L"ya da EC ve NVIDIA değerleri tutarlı hâle geldikten sonra \"Yenile\"ye basın.";
  }
  return L"Bilinmeyen engel.";
}

inline Notice FirstBlockerNotice(const LiveState& s) {
  Notice n;
  if (s.blockers.empty()) return n;
  n.level = (s.blockers[0] == Blocker::DriverAbsent || s.blockers[0] == Blocker::IdentityMismatch ||
             s.blockers[0] == Blocker::DriverAccessDenied || s.blockers[0] == Blocker::FaultLatched)
                ? Level::Error
                : Level::Warn;
  n.text = BlockerText(s.blockers[0]);
  if (s.blockers.size() > 1) n.text += L"  (+" + std::to_wstring(s.blockers.size() - 1) + L" neden daha)";
  return n;
}

inline const wchar_t* DriverText(DriverState d) {
  switch (d) {
    case DriverState::Connected: return L"Bağlı";
    case DriverState::Absent: return L"Yok";
    case DriverState::AccessDenied: return L"Erişim reddedildi";
    case DriverState::Error: return L"Hata";
  }
  return L"?";
}

inline const wchar_t* DriverTextFor(const LiveState& s) {
  return s.driverProbed ? DriverText(s.driver) : L"denenmedi";
}

inline const wchar_t* IdentityText(IdentityVerdict v) {
  switch (v) {
    case IdentityVerdict::Exact: return L"doğrulandı";
    case IdentityVerdict::Unreadable: return L"okunamadı";
    case IdentityVerdict::ManufacturerMismatch: return L"üretici uyuşmuyor";
    case IdentityVerdict::ModelMismatch: return L"model uyuşmuyor";
    case IdentityVerdict::BiosMismatch: return L"BIOS uyuşmuyor";
  }
  return L"?";
}

inline Notice ApplyNotice(const ApplyResult& r) {
  Notice n;
  const std::wstring w = Watts(r.target);
  switch (r.outcome) {
    case Outcome::Ok:
      n.level = Level::Info;
      n.text = w + L" uygulandı. EC değeri geri okundu ve NVIDIA sürücüsü de " +
               (r.after.nv.ok ? Num1(r.after.nv.limitW) + L" W" : std::wstring(L"yeni limiti")) + L" olarak doğruladı.";
      break;
    case Outcome::AlreadyActive:
      n.text = w + L" zaten etkin: EC ve NVIDIA aynı değeri gösteriyor, bir şey yazılmadı.";
      break;
    case Outcome::BlockedUnknownProfile:
      n.level = Level::Error;
      n.text = L"Bilinmeyen profil istendi; hiçbir şey yazılmadı.";
      break;
    case Outcome::BlockedNotReady: {
      n.level = Level::Warn;
      n.text = L"Şu an uygulanamaz: ";
      n.text += r.before.blockers.empty() ? std::wstring(L"başka bir güç işlemi sürüyor veya durum okunamadı.")
                                          : BlockerText(r.before.blockers[0]);
      break;
    }
    case Outcome::BlockedConsent:
      n.level = Level::Warn;
      n.text = w + L" için gerekli onay verilmediği için uygulanmadı.";
      break;
    case Outcome::BlockedTemp:
      n.level = Level::Warn;
      n.text = L"GPU çekirdeği " + std::to_wstring(r.before.nv.tempC) + L" °C. OEM profilinin (140 W) üzerine çıkmak için sıcaklık " +
               std::to_wstring(kMaxTempToRaiseC) + L" °C'nin altında olmalı; hiçbir şey yazılmadı. Soğuyunca tekrar deneyin.";
      break;
    case Outcome::BlockedAboveVbiosMax:
      n.level = Level::Error;
      n.text = w + L", NVIDIA'nın bildirdiği azami limitin üzerinde; uygulanmadı.";
      break;
    case Outcome::FailedNoChange:
      n.level = Level::Error;
      n.text = w + L" uygulanamadı. EC'nin değişmediği doğrulandı. (Ayrıntı: günlük)";
      break;
    case Outcome::FailedRolledBack:
      n.level = Level::Error;
      n.text = w + L" doğrulanamadı; önceki değere geri dönüldü ve geri dönüş doğrulandı. (Ayrıntı: günlük)";
      break;
    case Outcome::FailedRollbackUnverified:
      n.level = Level::Error;
      n.text = L"KRİTİK: " + w + L" uygulanamadı ve önceki değere dönüş DOĞRULANAMADI. EC durumu belirsiz. "
               L"Bilgisayarı yeniden başlatın, sonra \"Yenile\"ye basıp EC ve NVIDIA değerlerinin tutarlı olduğunu kontrol edin.";
      break;
  }
  return n;
}

inline const wchar_t* kConsentModelText =
    L"Bu bilgisayarın SMBIOS kaydında \"GM5TG7Y\" kart etiketi görünmüyor. Üretici, model (TULPAR T5 V21.7) ve BIOS sürümü "
    L"(N.1.07Mon08) ise birebir eşleşiyor.\n\n"
    L"Bu bilgisayarın Monster Tulpar T5 V21.7 (GM5TG7Y) olduğunu onaylıyor musunuz?\n\n"
    L"Onay kaydedilir; \"Onayları sıfırla\" ile geri alınabilir.";

inline const wchar_t* kConsentOverOemText =
    L"145 W ve 150 W, üreticinin bu platform için belirttiği en yüksek profilin (140 W) ÜZERİNDEDİR.\n\n"
    L"• Daha yüksek güç; daha fazla ısı, fan gürültüsü ve VRM yükü demektir.\n"
    L"• Uygulama sıcaklık sınırını (80 °C) denetler ve sonucu NVIDIA sürücüsüyle doğrular; ancak donanımın uzun süreli "
    L"davranışı bu yazılım tarafından güvence altına alınamaz.\n"
    L"• Bu değişiklik yalnızca kendi sorumluluğunuzdadır; garanti kapsamını etkileyebilir.\n\n"
    L"Riski anlıyor ve devam etmek istiyor musunuz?\n(Onay kaydedilir; \"Onayları sıfırla\" ile geri alınabilir.)";

inline const wchar_t* kConsentUnverifiedText =
    L"130 W ve 135 W profilleri bu cihazda canlı olarak denenmemiştir; EC eşlemesi 125 W ile 140 W arasındaki doğrulanmış "
    L"noktalardan beklenen değerdir.\n\n"
    L"Uygulama yazdıktan sonra EC'yi geri okur, NVIDIA limitini doğrular ve tutmazsa önceki değere döner.\n\n"
    L"Devam etmek istiyor musunuz?\n(Onay kaydedilir; \"Onayları sıfırla\" ile geri alınabilir.)";


// ======================================================================================================================
// Kalıcı profil uygulama (OEM-profil tabanlı kalıcılık). DOĞRU TEKNİK TANIM: bu bir donanım/firmware kalıcı yazımı DEĞİLDİR; OEM
// GCUService'in kendi profil dosyasına kaydedilen ve OEM hizmetiyle yeniden uygulanan OEM-profil tabanlı kalıcılıktır. OEM GCUService
// yine gereklidir. Bu nedenle aşağıdaki metinlerde firmware'e/BIOS'a kalıcı yazım ya da OEM altyapısından bağımsızlık anlamına gelen
// hiçbir ifade GEÇMEZ.
// ======================================================================================================================
inline const wchar_t* kPersistTitle = L"KALICI UYGULAMA";
inline const wchar_t* kPersistDesc =
    L"Seçili GPU güç profili, mevcut OEM profil dosyasına kaydedilir. MONSTER GPU POWER uygulamasının Windows açılışında "
    L"çalışması gerekmez. OEM GCUService kurulu ve çalışır olmalıdır.";

// Kalıcılık modülünün kilit nedeni (kullanıcıya). Teknik ayrıntı günlüktedir.
inline std::wstring LockReason(oem::Lock l, const std::string& version) {
  switch (l) {
    case oem::Lock::None: return L"";
    case oem::Lock::NotAdmin: return L"uygulama yönetici hakkıyla çalışmıyor";
    case oem::Lock::ServiceNotRunning: return L"OEM GCUService çalışmıyor";
    case oem::Lock::ServicePath: return L"çalışan GCUService beklenen OEM klasöründe değil";
    case oem::Lock::ServiceVersion:
      return L"GCUService sürümü " + (version.empty() ? std::wstring(L"okunamadı") : Widen(version)) + L" (yalnız 1.0.2.70 desteklenir)";
    case oem::Lock::DirUnsafe: return L"OEM profil klasörü (UserPofiles) güvenle doğrulanamadı";
    case oem::Lock::MainOptionIo: return L"MainOption.json okunamadı";
    case oem::Lock::MainOptionFormat: return L"MainOption.json beklenen biçimde değil";
    case oem::Lock::ProfileNotActive: return L"etkin OEM profili Turbo profil 1 (Mode3_Profile1) değil";
    case oem::Lock::ProfileIo: return L"Mode3_Profile1.json okunamadı";
    case oem::Lock::ProfileEncoding: return L"Mode3_Profile1.json kodlaması tanınmıyor";
    case oem::Lock::ProfileFormat: return L"Mode3_Profile1.json biçimi beklenenden farklı";
    case oem::Lock::ProfileNotActivated: return L"OEM profili etkin değil (Activated)";
    case oem::Lock::SwitchOff: return L"OEM profilinde ConfigurableTGPSwitch kapalı";
    case oem::Lock::DynamicBoostOn: return L"OEM profilinde DynamicBoostSwitch açık";
    case oem::Lock::TargetUnsupported: return L"OEM dosyasındaki hedef desteklenen değerlerden biri değil";
    case oem::Lock::Changed: return L"OEM profil dosyası işlem sırasında değişti (başka bir program yazmış olabilir)";
  }
  return L"bilinmeyen neden";
}

// Kartın durum satırı: GERÇEK OEM dosyasından + GERÇEK EC/NVIDIA ölçümünden türetilir.
inline Notice PersistStatus(const PersistView& v) {
  Notice n;
  switch (v.kind) {
    case PersistKind::Checking:
      n.text = L"OEM profil durumu denetleniyor…";
      return n;
    case PersistKind::Locked:
      n.level = Level::Warn;
      n.text = L"Kalıcı uygulama şu an yapılamıyor: " + LockReason(v.lock, v.serviceVersion) +
               L". Profiller yalnız kalıcı uygulandığı için bu durumda profil seçimi uygulanmaz.";
      if (v.oemWatts) n.text += L" OEM dosyasında " + Watts(v.oemWatts) + L" kayıtlı görünüyor.";
      return n;
    case PersistKind::Matches:
      n.text = L"OEM profilinde " + Watts(v.oemWatts) + L" kayıtlı  ·  Anlık profil: " + Watts(v.liveWatts);
      if (v.oemWatts == kOemMaxWatts) n.text += L"  (OEM varsayılanı)";
      else if (v.owner == OwnerKind::Mine) n.text += L"  ·  bu uygulama yazdı";
      break;
    case PersistKind::Differs:
      n.level = Level::Warn;
      n.text = L"Anlık " + Watts(v.liveWatts) + L" / OEM'de kayıtlı " + Watts(v.oemWatts) +
               L" (OEM hizmeti dosyayı yeniden okuyunca " + std::to_wstring(v.oemWatts) + L" W uygulanabilir)";
      break;
    case PersistKind::LiveUnknown:
      n.level = Level::Warn;
      n.text = L"OEM profilinde " + (v.oemWatts ? Watts(v.oemWatts) : std::wstring(L"?")) +
               L" kayıtlı  ·  Anlık profil okunamadı ya da tanınmıyor (EC ve NVIDIA tutarlı değil)";
      break;
  }
  if (v.owner == OwnerKind::Changed) {
    n.level = Level::Warn;
    n.text += L"  ·  Dosya bu uygulamanın son kaydından farklı (başka bir program değiştirmiş olabilir).";
  }
  return n;
}

// ---- onay iletişim kutuları -------------------------------------------------------------------------------------------
// OEM dosyasında bu uygulamanın yazmadığı (ya da yazdığından sonra değişmiş) 140 W dışı bir değer var: üzerine yazmadan ÖNCE sorulur.
inline std::wstring PersistForeignConfirm(ForeignKind k, int fileWatts, int wantWatts) {
  std::wstring t;
  if (k == ForeignKind::ChangedAfterOurs)
    t = L"OEM profil dosyası, bu uygulamanın en son yazdığı sürümden FARKLI: şu an " + Watts(fileWatts) +
        L" kayıtlı. Başka bir program (ör. Control Center, bir OEM güncellemesi) ya da elle yapılmış bir ayar dosyayı değiştirmiş olabilir.";
  else
    t = L"OEM profil dosyasında şu an " + Watts(fileWatts) +
        L" kayıtlı ve bu değeri bu uygulama yazmadı (Control Center, başka bir program ya da elle yapılmış bir ayar olabilir).";
  t += L"\n\nSeçtiğiniz profil: " + Watts(wantWatts) + L".\n\n"
       L"Devam ederseniz:\n"
       L"• Önce anlık EC + NVIDIA değeri " + Watts(wantWatts) + L" olarak uygulanıp doğrulanır,\n"
       L"• sonra OEM dosyasındaki hedef " + std::to_wstring(fileWatts) + L" W'tan " + std::to_wstring(wantWatts) +
       L" W'a DEĞİŞTİRİLİR (önce dosyanın tam yedeği alınır; mevcut yedekler korunur).\n\n"
       L"Devam edilsin mi?\n\n"
       L"(Hayır: hiçbir şey uygulanmaz; ne anlık profil ne de OEM dosyası değişir.)";
  return t;
}
inline const wchar_t* kPersistForeignDeclined =
    L"İptal edildi: OEM profil dosyasındaki değer bu uygulamanın yazdığı değil; onay verilmediği için HİÇBİR ŞEY uygulanmadı "
    L"(ne anlık profil ne de OEM dosyası değişti).";
inline const wchar_t* kPersistForeignStale =
    L"İşlem başlatılmadı: onay iletişim kutusu açıkken anlık durum değişti. Durum yeniden okunuyor; profili tekrar seçebilirsiniz. "
    L"Hiçbir şey uygulanmadı.";

inline const wchar_t* kRestoreOnExitFailed = L"Çıkışta OEM 140 W profiline dönüş DOĞRULANAMADI.";

// ---- işlem sonuçları ---------------------------------------------------------------------------------------------------
inline std::wstring OemCause(const PersistTxnResult& t) {
  using oem::CommitKind;
  switch (t.oem.kind) {
    case CommitKind::Refused: return LockReason(t.oem.lock, std::string());
    case CommitKind::FailedNoChange: return L"dosya yazılamadı; bu uygulama OEM dosyasında hiçbir değişiklik yapmadı";
    case CommitKind::FailedIndeterminate:
      return L"OEM dosyasının son durumu DOĞRULANAMADI (yedekler korundu, günlüğe bakın)";
    case CommitKind::Committed:
    case CommitKind::AlreadyCurrent: break;
  }
  return L"bilinmeyen neden";
}

// Profiller yalnız kalıcı uygulanır: OEM profil modülü kullanılamıyorsa (servis yok, sürüm farklı ...) HİÇBİR ŞEY uygulanmaz.
inline Notice PersistUnavailableNotice(oem::Lock lock, const std::string& version) {
  Notice n;
  n.level = Level::Warn;
  n.text = L"HİÇBİR ŞEY uygulanmadı: profiller yalnız kalıcı uygulanır ve OEM profil dosyası şu an kullanılamıyor (" +
           LockReason(lock, version) + L").";
  return n;
}

inline Notice PersistTxnNotice(TxnClass c, const PersistTxnResult& t) {
  Notice n;
  const std::wstring w = Watts(t.live.target);
  const int prev = t.live.before.activeProfile ? t.live.before.activeProfile : t.live.before.ecProfile;
  const std::wstring nvTxt = t.live.after.nv.ok ? Num1(t.live.after.nv.limitW) + L" W" : std::wstring(L"yeni limiti");
  switch (c) {
    case TxnClass::NeedsApproval:
      n.level = Level::Warn;
      n.text = L"OEM profil dosyasında bu uygulamanın yazmadığı bir değer var; onay verilmeden HİÇBİR ŞEY uygulanmadı.";
      return n;
    case TxnClass::Aborted:
      n.level = Level::Warn;
      n.text = L"OEM profil dosyası onayladığınız sürümden farklı olduğu için HİÇBİR ŞEY uygulanmadı. Durumu yenileyip tekrar deneyin.";
      return n;
    case TxnClass::OemUnavailable:
      return PersistUnavailableNotice(t.pre.lock, t.pre.svc.versionRead ? t.pre.svc.version : std::string());
    case TxnClass::LiveFailed:
      n = ApplyNotice(t.live);
      n.text += L" OEM profil dosyasına dokunulmadı.";
      return n;
    case TxnClass::BothOk:
      n.level = Level::Info;
      n.text = w + L" uygulandı ve kalıcı kaydedildi. (a) Anlık: EC değeri geri okundu, NVIDIA " + nvTxt + L" olarak doğruladı. "
               L"(b) OEM profil dosyası: GPU.ConfigurableTGPTarget = " + std::to_wstring(t.live.target) +
               L" yazıldı ve geri okundu (önce özgün dosyanın doğrulanmış yedeği alındı). Yeniden başlatma sonrası etkisini bu uygulama "
               L"doğrulayamaz; cihazda ayrıca gözlemlenir.";
      return n;
    case TxnClass::BothOkAlready:
      n.level = Level::Info;
      n.text = w + L": (a) Anlık EC + NVIDIA değeri doğrulandı. (b) OEM profili zaten " + w +
               L" idi ve doğrulandı; dosyaya yazılmadı, yeni yedek alınmadı.";
      return n;
    case TxnClass::OemFailedLiveRestored:
      n.level = Level::Error;
      n.text = w + L" anlık uygulanmıştı ancak OEM profil dosyasına kaydedilemedi (" + OemCause(t) +
               L"). Yarım sonuç kalmasın diye anlık değişiklik geri alındı: GPU ayarı tekrar " +
               (prev ? Watts(prev) : std::wstring(L"önceki değer")) + L" ve geri alma doğrulandı.";
      return n;
    case TxnClass::OemFailedRestoreUnverified:
      n.level = Level::Error;
      n.text = L"KRİTİK: " + w + L" anlık uygulandı, OEM profil dosyasına kaydedilemedi (" + OemCause(t) +
               L") ve önceki değere dönüş DOĞRULANAMADI. EC durumu belirsiz olabilir; durum yeniden okunuyor.";
      if (t.liveRestore.outcome == Outcome::FailedRollbackUnverified)
        n.text += L" Güvenlik için yazma kapalı: bilgisayarı yeniden başlatın, sonra \"Yenile\"ye basıp EC ve NVIDIA değerlerini kontrol edin.";
      else
        n.text += L" Anlık profili kontrol edin; gerekirse istediğiniz profili yeniden seçin.";
      return n;
    case TxnClass::OemFailedLiveUnchanged:
      n.level = Level::Error;
      n.text = L"OEM profil dosyasına kaydedilemedi (" + OemCause(t) + L"). Anlık GPU ayarı zaten " + w + L" idi ve değişmedi.";
      return n;
  }
  return n;
}

// OEM dosyasının çapraz-süreç kilidi alınamadı: HİÇBİR ŞEY uygulanmadı (geçici uygulama yoktur).
inline Notice PersistLockBusyNotice() {
  Notice n;
  n.level = Level::Warn;
  n.text = L"HİÇBİR ŞEY uygulanmadı: OEM profil dosyası başka bir işlem tarafından kullanılıyor (kilit alınamadı). "
           L"Biraz sonra profili tekrar seçin.";
  return n;
}

inline const wchar_t* kPersistNoOwnership =
    L" Not: dosyanın bu uygulamanın yazdığı sürüm olduğunu gösteren kanıt oluşturulamadı; sonraki seçimde dosyadaki değer yabancı sayılıp "
    L"yeniden onay sorulabilir.";
inline const wchar_t* kPersistMetaSaveFailed =
    L" UYARI: uygulama ayarları (sahiplik kaydı) diskte saklanamadı; sonraki seçimde dosyadaki değer yabancı sayılıp yeniden onay "
    L"sorulabilir.";

// "Hakkında" düğmesi: yalnız kullanıcı tıklayınca varsayılan tarayıcıda proje sayfasını açar (uygulamanın kendisi ağ bağlantısı kurmaz).
inline const wchar_t* kAboutLabel = L"Hakkında";
inline const wchar_t* kRepoUrl = L"https://github.com/MehmetNY/Monster-XMG-Gpu-Power-Control";
inline Notice AboutFailedNotice() {
  Notice n;
  n.level = Level::Warn;
  n.text = std::wstring(L"Tarayıcı açılamadı. Proje sayfası: ") + kRepoUrl;
  return n;
}

// ======================================================================================================================
// Açılışta başlatma (Görev Zamanlayıcı görevi: MonsterGpuPower_Autostart) ve çıkışta OEM 140 W'a dönüş.
// Açılış görevi uygulamayı tepside başlatır; hiçbir GPU profili UYGULAMAZ (seçilen profil zaten OEM profil dosyasında kalıcıdır).
// ======================================================================================================================
inline const wchar_t* kAutostartLabel = L"Uygulamayı açılışta başlat";
inline const wchar_t* kAutostartDesc = L"Windows oturum açılışında uygulamayı tepside başlatır; kendi başına profil uygulamaz.";
inline const wchar_t* kRestoreLabel = L"Çıkışta OEM 140 W'a dön";
inline const wchar_t* kRestoreDesc = L"Çıkarken anlık güç OEM 140 W'a döner; OEM dosyasındaki kalıcı seçiminiz değişmez.";

inline Notice AutostartOnNotice() {
  Notice n;
  n.text = L"Uygulama artık her Windows oturum açılışında (yaklaşık 10 sn sonra) tepside başlayacak. Bu görev profil uygulamaz; seçtiğiniz "
           L"profil zaten OEM profil dosyasında kalıcıdır.";
  return n;
}
inline Notice AutostartOffNotice() {
  Notice n;
  n.text = L"\"Uygulamayı açılışta başlat\" kapatıldı: açılış görevi silindi ve silindiği doğrulandı.";
  return n;
}
inline Notice AutostartEnableFailedNotice(const std::wstring& err) {
  Notice n;
  n.level = Level::Error;
  n.text = L"\"Uygulamayı açılışta başlat\" açılamadı: " + err + L". Hiçbir ayar değiştirilmedi.";
  return n;
}
inline Notice AutostartNotVerifiedNotice() {
  Notice n;
  n.level = Level::Warn;
  n.text = L"Açılış görevi oluşturuldu ama Görev Zamanlayıcı'dan beklenen biçimde okunamadı (etkin, bu uygulamanın yolu, /minimized). "
           L"Anahtar kapalı gösteriliyor; bir sonraki açılışta uygulamanın başlayacağı varsayılmamalıdır.";
  return n;
}
inline Notice AutostartDisableFailedNotice(const std::wstring& err) {
  Notice n;
  n.level = Level::Warn;
  n.text = L"Açılış görevi silinemedi ya da silindiği doğrulanamadı (" + err + L"). Görev duruyor olabilir: uygulama açılışta başlamaya devam edebilir.";
  return n;
}
inline Notice AutostartUntrustedPathNotice() {
  Notice n;
  n.level = Level::Warn;
  n.text = L"Bu kopya kurulum sihirbazıyla Program Files altına kurulmadığı için açılışta başlatma etkinleştirilemez: yükseltilmiş bir açılış "
           L"görevi, kullanıcının yazabildiği bir klasördeki programı çalıştırmamalıdır.";
  return n;
}
inline Notice AutostartBusyNotice() {
  Notice n;
  n.text = L"Açılış görevi işlemi sürüyor; bitince anahtarı tekrar deneyin.";
  return n;
}


}  // namespace tr
}  // namespace win
}  // namespace mgp
