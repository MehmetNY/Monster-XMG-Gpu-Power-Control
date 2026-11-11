// Kullanıcıya görünen TÜM metinler tek yerde, iki dilli (Türkçe + İngilizce yan yana): T(tr, en) geçerli dilin metnini seçer.
// All user-visible texts in one place, in two languages side by side: T(tr, en) picks the current language.
// Motorun teknik (İngilizce) ayrıntıları yalnız günlüğe gider / Technical engine details go to the log only.
#pragma once
#include <cstdio>
#include <string>

#include "../core/persist_txn.h"
#include "../core/persist_view.h"
#include "../core/power_profile_engine.h"
#include "lang.h"
#include "win_util.h"

namespace mgp {
namespace win {
namespace txt {

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

// Marka, sürüm ve adres dilden bağımsızdır.
inline const wchar_t* kAppTitle = L"MONSTER GPU POWER";
inline const wchar_t* kVersion = L"1.5.0";
inline const wchar_t* kRepoUrl = L"https://github.com/MehmetNY/Monster-XMG-Gpu-Power-Control";

// ======================================================================================================================
// Cihaz / sürücü durumu
// ======================================================================================================================
inline std::wstring BlockerText(Blocker b) {
  switch (b) {
    case Blocker::IdentityUnreadable:
      return Tw(L"Cihaz kimliği (SMBIOS) okunamadı. Güvenlik için yazma kapalı.",
                L"The device identity (SMBIOS) could not be read. Writing is disabled for safety.");
    case Blocker::IdentityMismatch:
      return Tw(L"Bu bilgisayar Monster Tulpar T5 V21.7 (BIOS N.1.07Mon08) olarak doğrulanamadı. "
                L"Güvenlik için bu cihazda EC'ye hiç dokunulmaz.",
                L"This computer could not be verified as a Monster Tulpar T5 V21.7 (BIOS N.1.07Mon08). "
                L"For safety, the EC is never touched on this device.");
    case Blocker::DriverAbsent:
      return Tw(L"Uniwill \"ACPIDriver\" sürücüsü bulunamadı (aygıt: ACPI\\INOU0000). Sürücü kurulu değil ya da çalışmıyor; "
                L"EC'ye erişilemiyor. Ayrıntı için README.tr.md \"Sürücü bağımlılığı\".",
                L"The Uniwill \"ACPIDriver\" driver was not found (device: ACPI\\INOU0000). It is not installed or not running, "
                L"so the EC cannot be reached. See README.md \"Driver dependency\".");
    case Blocker::DriverAccessDenied:
      return Tw(L"ACPIDriver aygıtına erişim reddedildi. Uygulamayı yönetici olarak çalıştırın.",
                L"Access to the ACPIDriver device was denied. Run the application as administrator.");
    case Blocker::DriverError:
      return Tw(L"ACPIDriver aygıtı açılırken beklenmeyen bir hata oluştu (günlüğe bakın).",
                L"An unexpected error occurred while opening the ACPIDriver device (see the log).");
    case Blocker::EcReadInvalid:
      return Tw(L"EC güç değerleri okunamadı veya geçersiz döndü. Güvenlik için yazma kapalı.",
                L"The EC power values could not be read or came back invalid. Writing is disabled for safety.");
    case Blocker::EcStateUnexpected:
      return Tw(L"EC güç durumu tanınmıyor (başka bir yazılım değiştirmiş olabilir). Güvenlik için yazma kapalı.",
                L"The EC power state is not recognized (another program may have changed it). Writing is disabled for safety.");
    case Blocker::NvmlUnavailable:
      return Tw(L"NVIDIA NVML kullanılamıyor; sonuç doğrulanamayacağı için yazma kapalı.",
                L"NVIDIA NVML is unavailable; the result could not be verified, so writing is disabled.");
    case Blocker::GpuMismatch:
      return Tw(L"Beklenen NVIDIA GeForce RTX 3070 Laptop GPU bulunamadı. Yazma kapalı.",
                L"The expected NVIDIA GeForce RTX 3070 Laptop GPU was not found. Writing is disabled.");
    case Blocker::MaxLimitMismatch:
      return Tw(L"NVIDIA azami güç limiti beklenen 150 W ile uyuşmuyor. Yazma kapalı.",
                L"The NVIDIA maximum power limit does not match the expected 150 W. Writing is disabled.");
    case Blocker::FaultLatched:
      return Tw(L"KRİTİK: Önceki bir işlemin geri alınması doğrulanamadı. Güvenlik için yazma kapalı. Bilgisayarı yeniden başlatın "
                L"ya da EC ve NVIDIA değerleri tutarlı hâle geldikten sonra \"Yenile\"ye basın.",
                L"CRITICAL: the rollback of a previous operation could not be verified. Writing is disabled for safety. Restart the "
                L"computer, or press \"Refresh\" once the EC and NVIDIA values are consistent again.");
  }
  return Tw(L"Bilinmeyen engel.", L"Unknown blocker.");
}

inline Notice FirstBlockerNotice(const LiveState& s) {
  Notice n;
  if (s.blockers.empty()) return n;
  n.level = (s.blockers[0] == Blocker::DriverAbsent || s.blockers[0] == Blocker::IdentityMismatch ||
             s.blockers[0] == Blocker::DriverAccessDenied || s.blockers[0] == Blocker::FaultLatched)
                ? Level::Error
                : Level::Warn;
  n.text = BlockerText(s.blockers[0]);
  if (s.blockers.size() > 1) {
    const std::wstring k = std::to_wstring(s.blockers.size() - 1);
    n.text += Tw(L"  (+", L"  (+") + k + Tw(L" neden daha)", L" more)");
  }
  return n;
}

inline const wchar_t* DriverText(DriverState d) {
  switch (d) {
    case DriverState::Connected: return T(L"Bağlı", L"Connected");
    case DriverState::Absent: return T(L"Yok", L"Missing");
    case DriverState::AccessDenied: return T(L"Erişim reddedildi", L"Access denied");
    case DriverState::Error: return T(L"Hata", L"Error");
  }
  return L"?";
}

inline const wchar_t* DriverTextFor(const LiveState& s) {
  return s.driverProbed ? DriverText(s.driver) : T(L"denenmedi", L"not probed");
}

inline const wchar_t* IdentityText(IdentityVerdict v) {
  switch (v) {
    case IdentityVerdict::Exact: return T(L"doğrulandı", L"verified");
    case IdentityVerdict::Unreadable: return T(L"okunamadı", L"unreadable");
    case IdentityVerdict::ManufacturerMismatch: return T(L"üretici uyuşmuyor", L"manufacturer mismatch");
    case IdentityVerdict::ModelMismatch: return T(L"model uyuşmuyor", L"model mismatch");
    case IdentityVerdict::BiosMismatch: return T(L"BIOS uyuşmuyor", L"BIOS mismatch");
  }
  return L"?";
}

inline Notice ApplyNotice(const ApplyResult& r) {
  Notice n;
  const std::wstring w = Watts(r.target);
  switch (r.outcome) {
    case Outcome::Ok:
      n.level = Level::Info;
      n.text = w + Tw(L" uygulandı. EC değeri geri okundu ve NVIDIA sürücüsü de ", L" applied. The EC value was read back and the NVIDIA driver also confirmed ") +
               (r.after.nv.ok ? Num1(r.after.nv.limitW) + L" W" : Tw(L"yeni limiti", L"the new limit")) + Tw(L" olarak doğruladı.", L".");
      break;
    case Outcome::AlreadyActive:
      n.text = w + Tw(L" zaten etkin: EC ve NVIDIA aynı değeri gösteriyor, bir şey yazılmadı.",
                      L" is already active: the EC and NVIDIA report the same value; nothing was written.");
      break;
    case Outcome::BlockedUnknownProfile:
      n.level = Level::Error;
      n.text = Tw(L"Bilinmeyen profil istendi; hiçbir şey yazılmadı.", L"An unknown profile was requested; nothing was written.");
      break;
    case Outcome::BlockedNotReady: {
      n.level = Level::Warn;
      n.text = Tw(L"Şu an uygulanamaz: ", L"It cannot be applied right now: ");
      n.text += r.before.blockers.empty()
                    ? Tw(L"başka bir güç işlemi sürüyor veya durum okunamadı.", L"another power operation is running or the state could not be read.")
                    : BlockerText(r.before.blockers[0]);
      break;
    }
    case Outcome::BlockedConsent:
      n.level = Level::Warn;
      n.text = w + Tw(L" için gerekli onay verilmediği için uygulanmadı.", L" was not applied because the required confirmation was not given.");
      break;
    case Outcome::BlockedTemp:
      n.level = Level::Warn;
      n.text = Tw(L"GPU çekirdeği ", L"The GPU core is at ") + std::to_wstring(r.before.nv.tempC) +
               Tw(L" °C. OEM profilinin (140 W) üzerine çıkmak için sıcaklık ", L" °C. To go above the OEM profile (140 W) the temperature must be below ") +
               std::to_wstring(kMaxTempToRaiseC) +
               Tw(L" °C'nin altında olmalı; hiçbir şey yazılmadı. Soğuyunca tekrar deneyin.", L" °C; nothing was written. Try again once it has cooled down.");
      break;
    case Outcome::BlockedAboveVbiosMax:
      n.level = Level::Error;
      n.text = w + Tw(L", NVIDIA'nın bildirdiği azami limitin üzerinde; uygulanmadı.", L" is above the maximum limit reported by NVIDIA; not applied.");
      break;
    case Outcome::FailedNoChange:
      n.level = Level::Error;
      n.text = w + Tw(L" uygulanamadı. EC'nin değişmediği doğrulandı. (Ayrıntı: günlük)",
                      L" could not be applied. The EC was verified to be unchanged. (Details: log)");
      break;
    case Outcome::FailedRolledBack:
      n.level = Level::Error;
      n.text = w + Tw(L" doğrulanamadı; önceki değere geri dönüldü ve geri dönüş doğrulandı. (Ayrıntı: günlük)",
                      L" could not be verified; the previous value was restored and the restore was verified. (Details: log)");
      break;
    case Outcome::FailedRollbackUnverified:
      n.level = Level::Error;
      n.text = Tw(L"KRİTİK: ", L"CRITICAL: ") + w +
               Tw(L" uygulanamadı ve önceki değere dönüş DOĞRULANAMADI. EC durumu belirsiz. "
                  L"Bilgisayarı yeniden başlatın, sonra \"Yenile\"ye basıp EC ve NVIDIA değerlerinin tutarlı olduğunu kontrol edin.",
                  L" could not be applied and the return to the previous value could NOT be verified. The EC state is uncertain. "
                  L"Restart the computer, then press \"Refresh\" and check that the EC and NVIDIA values are consistent.");
      break;
  }
  return n;
}

// ======================================================================================================================
// Onay iletişim kutuları
// ======================================================================================================================
inline const wchar_t* kConsentModelText() {
  return T(L"Bu bilgisayarın SMBIOS kaydında \"GM5TG7Y\" kart etiketi görünmüyor. Üretici, model (TULPAR T5 V21.7) ve BIOS sürümü "
           L"(N.1.07Mon08) ise birebir eşleşiyor.\n\n"
           L"Bu bilgisayarın Monster Tulpar T5 V21.7 (GM5TG7Y) olduğunu onaylıyor musunuz?\n\n"
           L"Onay kaydedilir; \"Onayları sıfırla\" ile geri alınabilir.",
           L"This computer's SMBIOS record does not show the \"GM5TG7Y\" board tag. The manufacturer, model (TULPAR T5 V21.7) and "
           L"BIOS version (N.1.07Mon08) do match exactly.\n\n"
           L"Do you confirm that this computer is a Monster Tulpar T5 V21.7 (GM5TG7Y)?\n\n"
           L"Your confirmation is saved; it can be undone with \"Reset confirmations\".");
}

inline const wchar_t* kConsentOverOemText() {
  return T(L"145 W ve 150 W, üreticinin bu platform için belirttiği en yüksek profilin (140 W) ÜZERİNDEDİR.\n\n"
           L"• Daha yüksek güç; daha fazla ısı, fan gürültüsü ve VRM yükü demektir.\n"
           L"• Uygulama sıcaklık sınırını (80 °C) denetler ve sonucu NVIDIA sürücüsüyle doğrular; ancak donanımın uzun süreli "
           L"davranışı bu yazılım tarafından güvence altına alınamaz.\n"
           L"• Bu değişiklik yalnızca kendi sorumluluğunuzdadır; garanti kapsamını etkileyebilir.\n\n"
           L"Riski anlıyor ve devam etmek istiyor musunuz?\n(Onay kaydedilir; \"Onayları sıfırla\" ile geri alınabilir.)",
           L"145 W and 150 W are ABOVE the highest profile the manufacturer specifies for this platform (140 W).\n\n"
           L"• Higher power means more heat, more fan noise and more VRM load.\n"
           L"• The application checks the temperature limit (80 °C) and verifies the result with the NVIDIA driver; however, the "
           L"long-term behavior of the hardware cannot be guaranteed by this software.\n"
           L"• This change is entirely at your own risk and may affect your warranty.\n\n"
           L"Do you understand the risk and want to continue?\n(Your confirmation is saved; it can be undone with \"Reset confirmations\".)");
}

inline const wchar_t* kConsentUnverifiedText() {
  return T(L"130 W ve 135 W profilleri bu cihazda canlı olarak denenmemiştir; EC eşlemesi 125 W ile 140 W arasındaki doğrulanmış "
           L"noktalardan beklenen değerdir.\n\n"
           L"Uygulama yazdıktan sonra EC'yi geri okur, NVIDIA limitini doğrular ve tutmazsa önceki değere döner.\n\n"
           L"Devam etmek istiyor musunuz?\n(Onay kaydedilir; \"Onayları sıfırla\" ile geri alınabilir.)",
           L"The 130 W and 135 W profiles have not been tried live on this device; their EC mapping is the value expected from the "
           L"verified points between 125 W and 140 W.\n\n"
           L"After writing, the application reads the EC back, verifies the NVIDIA limit and returns to the previous value if it does "
           L"not hold.\n\n"
           L"Do you want to continue?\n(Your confirmation is saved; it can be undone with \"Reset confirmations\".)");
}

// ======================================================================================================================
// Kalıcı profil uygulama (OEM-profil tabanlı kalıcılık). DOĞRU TEKNİK TANIM: bu bir donanım/firmware kalıcı yazımı DEĞİLDİR; OEM
// GCUService'in kendi profil dosyasına kaydedilen ve OEM hizmetiyle yeniden uygulanan OEM-profil tabanlı kalıcılıktır. OEM GCUService
// yine gereklidir. Bu nedenle aşağıdaki metinlerde (HER İKİ DİLDE) firmware'e/BIOS'a kalıcı yazım ya da OEM altyapısından bağımsızlık
// anlamına gelen hiçbir ifade GEÇMEZ (tests/test_persist_view.h bunu her iki dil için denetler).
// ======================================================================================================================
inline const wchar_t* kPersistTitle() { return T(L"KALICI UYGULAMA", L"PERSISTENT APPLY"); }
inline const wchar_t* kPersistDesc() {
  return T(L"Seçili GPU güç profili, mevcut OEM profil dosyasına kaydedilir. MONSTER GPU POWER uygulamasının Windows açılışında "
           L"çalışması gerekmez. OEM GCUService kurulu ve çalışır olmalıdır.",
           L"The selected GPU power profile is saved to the existing OEM profile file. MONSTER GPU POWER does not need to run at "
           L"Windows startup. The OEM GCUService must be installed and running.");
}

// Kalıcılık modülünün kilit nedeni (kullanıcıya). Teknik ayrıntı günlüktedir.
inline std::wstring LockReason(oem::Lock l, const std::string& version) {
  switch (l) {
    case oem::Lock::None: return L"";
    case oem::Lock::NotAdmin: return Tw(L"uygulama yönetici hakkıyla çalışmıyor", L"the application is not running with administrator rights");
    case oem::Lock::ServiceNotRunning: return Tw(L"OEM GCUService çalışmıyor", L"the OEM GCUService is not running");
    case oem::Lock::ServicePath: return Tw(L"çalışan GCUService beklenen OEM klasöründe değil", L"the running GCUService is not in the expected OEM folder");
    case oem::Lock::ServiceVersion:
      return Tw(L"GCUService sürümü ", L"the GCUService version is ") +
             (version.empty() ? Tw(L"okunamadı", L"unreadable") : Widen(version)) +
             Tw(L" (yalnız 1.0.2.70 desteklenir)", L" (only 1.0.2.70 is supported)");
    case oem::Lock::DirUnsafe:
      return Tw(L"OEM profil klasörü (UserPofiles) güvenle doğrulanamadı", L"the OEM profile folder (UserPofiles) could not be verified as safe");
    case oem::Lock::MainOptionIo: return Tw(L"MainOption.json okunamadı", L"MainOption.json could not be read");
    case oem::Lock::MainOptionFormat: return Tw(L"MainOption.json beklenen biçimde değil", L"MainOption.json is not in the expected format");
    case oem::Lock::ProfileNotActive:
      return Tw(L"etkin OEM profili Turbo profil 1 (Mode3_Profile1) değil", L"the active OEM profile is not Turbo profile 1 (Mode3_Profile1)");
    case oem::Lock::ProfileIo: return Tw(L"Mode3_Profile1.json okunamadı", L"Mode3_Profile1.json could not be read");
    case oem::Lock::ProfileEncoding: return Tw(L"Mode3_Profile1.json kodlaması tanınmıyor", L"the encoding of Mode3_Profile1.json is not recognized");
    case oem::Lock::ProfileFormat:
      return Tw(L"Mode3_Profile1.json biçimi beklenenden farklı", L"the format of Mode3_Profile1.json differs from what is expected");
    case oem::Lock::ProfileNotActivated: return Tw(L"OEM profili etkin değil (Activated)", L"the OEM profile is not activated (Activated)");
    case oem::Lock::SwitchOff: return Tw(L"OEM profilinde ConfigurableTGPSwitch kapalı", L"ConfigurableTGPSwitch is off in the OEM profile");
    case oem::Lock::DynamicBoostOn: return Tw(L"OEM profilinde DynamicBoostSwitch açık", L"DynamicBoostSwitch is on in the OEM profile");
    case oem::Lock::TargetUnsupported:
      return Tw(L"OEM dosyasındaki hedef desteklenen değerlerden biri değil", L"the target in the OEM file is not one of the supported values");
    case oem::Lock::Changed:
      return Tw(L"OEM profil dosyası işlem sırasında değişti (başka bir program yazmış olabilir)",
                L"the OEM profile file changed during the operation (another program may have written to it)");
  }
  return Tw(L"bilinmeyen neden", L"unknown reason");
}

// Kartın durum satırı: GERÇEK OEM dosyasından + GERÇEK EC/NVIDIA ölçümünden türetilir.
inline Notice PersistStatus(const PersistView& v) {
  Notice n;
  switch (v.kind) {
    case PersistKind::Checking:
      n.text = Tw(L"OEM profil durumu denetleniyor…", L"Checking the OEM profile state…");
      return n;
    case PersistKind::Locked:
      n.level = Level::Warn;
      n.text = Tw(L"Kalıcı uygulama şu an yapılamıyor: ", L"Persistent apply is not possible right now: ") + LockReason(v.lock, v.serviceVersion) +
               Tw(L". Profiller yalnız kalıcı uygulandığı için bu durumda profil seçimi uygulanmaz.",
                  L". Because profiles are only applied persistently, selecting a profile applies nothing in this state.");
      if (v.oemWatts) n.text += Tw(L" OEM dosyasında ", L" The OEM file appears to hold ") + Watts(v.oemWatts) + Tw(L" kayıtlı görünüyor.", L".");
      return n;
    case PersistKind::Matches:
      n.text = Tw(L"OEM profilinde ", L"OEM profile holds ") + Watts(v.oemWatts) + Tw(L" kayıtlı  ·  Anlık profil: ", L"  ·  Live profile: ") + Watts(v.liveWatts);
      if (v.oemWatts == kOemMaxWatts) n.text += Tw(L"  (OEM varsayılanı)", L"  (OEM default)");
      else if (v.owner == OwnerKind::Mine) n.text += Tw(L"  ·  bu uygulama yazdı", L"  ·  written by this application");
      break;
    case PersistKind::Differs:
      n.level = Level::Warn;
      n.text = Tw(L"Anlık ", L"Live ") + Watts(v.liveWatts) + Tw(L" / OEM'de kayıtlı ", L" / saved in OEM ") + Watts(v.oemWatts) +
               Tw(L" (OEM hizmeti dosyayı yeniden okuyunca ", L" (the OEM service may apply ") + std::to_wstring(v.oemWatts) +
               Tw(L" W uygulanabilir)", L" W when it re-reads the file)");
      break;
    case PersistKind::LiveUnknown:
      n.level = Level::Warn;
      n.text = Tw(L"OEM profilinde ", L"OEM profile holds ") + (v.oemWatts ? Watts(v.oemWatts) : std::wstring(L"?")) +
               Tw(L" kayıtlı  ·  Anlık profil okunamadı ya da tanınmıyor (EC ve NVIDIA tutarlı değil)",
                  L"  ·  The live profile could not be read or is not recognized (EC and NVIDIA are inconsistent)");
      break;
  }
  if (v.owner == OwnerKind::Changed) {
    n.level = Level::Warn;
    n.text += Tw(L"  ·  Dosya bu uygulamanın son kaydından farklı (başka bir program değiştirmiş olabilir).",
                 L"  ·  The file differs from this application's last save (another program may have changed it).");
  }
  return n;
}

// ---- onay iletişim kutuları -------------------------------------------------------------------------------------------
// OEM dosyasında bu uygulamanın yazmadığı (ya da yazdığından sonra değişmiş) 140 W dışı bir değer var: üzerine yazmadan ÖNCE sorulur.
inline std::wstring PersistForeignConfirm(ForeignKind k, int fileWatts, int wantWatts) {
  std::wstring t;
  if (k == ForeignKind::ChangedAfterOurs)
    t = Tw(L"OEM profil dosyası, bu uygulamanın en son yazdığı sürümden FARKLI: şu an ", L"The OEM profile file DIFFERS from the version this application last wrote: it currently holds ") +
        Watts(fileWatts) +
        Tw(L" kayıtlı. Başka bir program (ör. Control Center, bir OEM güncellemesi) ya da elle yapılmış bir ayar dosyayı değiştirmiş olabilir.",
           L". Another program (e.g. Control Center, an OEM update) or a manual edit may have changed the file.");
  else
    t = Tw(L"OEM profil dosyasında şu an ", L"The OEM profile file currently holds ") + Watts(fileWatts) +
        Tw(L" kayıtlı ve bu değeri bu uygulama yazmadı (Control Center, başka bir program ya da elle yapılmış bir ayar olabilir).",
           L", and this application did not write that value (it may come from Control Center, another program or a manual edit).");
  t += Tw(L"\n\nSeçtiğiniz profil: ", L"\n\nSelected profile: ") + Watts(wantWatts) + L".\n\n";
  t += Tw(L"Devam ederseniz:\n• Önce anlık EC + NVIDIA değeri ", L"If you continue:\n• First the live EC + NVIDIA value is applied as ") + Watts(wantWatts) +
       Tw(L" olarak uygulanıp doğrulanır,\n• sonra OEM dosyasındaki hedef ", L" and verified,\n• then the target in the OEM file is CHANGED from ") +
       std::to_wstring(fileWatts) + Tw(L" W'tan ", L" W to ") + std::to_wstring(wantWatts) +
       Tw(L" W'a DEĞİŞTİRİLİR (önce dosyanın tam yedeği alınır; mevcut yedekler korunur).\n\nDevam edilsin mi?\n\n"
          L"(Hayır: hiçbir şey uygulanmaz; ne anlık profil ne de OEM dosyası değişir.)",
          L" W (a full backup of the file is taken first; existing backups are kept).\n\nContinue?\n\n"
          L"(No: nothing is applied; neither the live profile nor the OEM file changes.)");
  return t;
}
inline const wchar_t* kPersistForeignDeclined() {
  return T(L"İptal edildi: OEM profil dosyasındaki değer bu uygulamanın yazdığı değil; onay verilmediği için HİÇBİR ŞEY uygulanmadı "
           L"(ne anlık profil ne de OEM dosyası değişti).",
           L"Cancelled: the value in the OEM profile file was not written by this application; because it was not confirmed, NOTHING was "
           L"applied (neither the live profile nor the OEM file changed).");
}
inline const wchar_t* kPersistForeignStale() {
  return T(L"İşlem başlatılmadı: onay iletişim kutusu açıkken anlık durum değişti. Durum yeniden okunuyor; profili tekrar seçebilirsiniz. "
           L"Hiçbir şey uygulanmadı.",
           L"Not started: the live state changed while the confirmation dialog was open. The state is being re-read; you can select the "
           L"profile again. Nothing was applied.");
}

inline const wchar_t* kRestoreOnExitFailed() {
  return T(L"Çıkışta OEM 140 W profiline dönüş DOĞRULANAMADI.", L"Returning to the OEM 140 W profile on exit could NOT be verified.");
}
inline const wchar_t* kExitAnywayQuestion() {
  return T(L"Yine de çıkmak istiyor musunuz? (Hayır: uygulama açık kalır.)", L"Do you still want to exit? (No: the application stays open.)");
}

// ---- işlem sonuçları ---------------------------------------------------------------------------------------------------
inline std::wstring OemCause(const PersistTxnResult& t) {
  using oem::CommitKind;
  switch (t.oem.kind) {
    case CommitKind::Refused: return LockReason(t.oem.lock, std::string());
    case CommitKind::FailedNoChange:
      return Tw(L"dosya yazılamadı; bu uygulama OEM dosyasında hiçbir değişiklik yapmadı",
                L"the file could not be written; this application made no change to the OEM file");
    case CommitKind::FailedIndeterminate:
      return Tw(L"OEM dosyasının son durumu DOĞRULANAMADI (yedekler korundu, günlüğe bakın)",
                L"the final state of the OEM file could NOT be verified (backups were kept, see the log)");
    case CommitKind::Committed:
    case CommitKind::AlreadyCurrent: break;
  }
  return Tw(L"bilinmeyen neden", L"unknown reason");
}

// Profiller yalnız kalıcı uygulanır: OEM profil modülü kullanılamıyorsa (servis yok, sürüm farklı ...) HİÇBİR ŞEY uygulanmaz.
inline Notice PersistUnavailableNotice(oem::Lock lock, const std::string& version) {
  Notice n;
  n.level = Level::Warn;
  n.text = Tw(L"HİÇBİR ŞEY uygulanmadı: profiller yalnız kalıcı uygulanır ve OEM profil dosyası şu an kullanılamıyor (",
              L"NOTHING was applied: profiles are only applied persistently and the OEM profile file is not usable right now (") +
           LockReason(lock, version) + L").";
  return n;
}

inline Notice PersistTxnNotice(TxnClass c, const PersistTxnResult& t) {
  Notice n;
  const std::wstring w = Watts(t.live.target);
  const int prev = t.live.before.activeProfile ? t.live.before.activeProfile : t.live.before.ecProfile;
  const std::wstring nvTxt = t.live.after.nv.ok ? Num1(t.live.after.nv.limitW) + L" W" : Tw(L"yeni limiti", L"the new limit");
  switch (c) {
    case TxnClass::NeedsApproval:
      n.level = Level::Warn;
      n.text = Tw(L"OEM profil dosyasında bu uygulamanın yazmadığı bir değer var; onay verilmeden HİÇBİR ŞEY uygulanmadı.",
                  L"The OEM profile file holds a value this application did not write; NOTHING was applied because it was not confirmed.");
      return n;
    case TxnClass::Aborted:
      n.level = Level::Warn;
      n.text = Tw(L"OEM profil dosyası onayladığınız sürümden farklı olduğu için HİÇBİR ŞEY uygulanmadı. Durumu yenileyip tekrar deneyin.",
                  L"NOTHING was applied because the OEM profile file differs from the version you confirmed. Refresh the state and try again.");
      return n;
    case TxnClass::OemUnavailable:
      return PersistUnavailableNotice(t.pre.lock, t.pre.svc.versionRead ? t.pre.svc.version : std::string());
    case TxnClass::LiveFailed:
      n = ApplyNotice(t.live);
      n.text += Tw(L" OEM profil dosyasına dokunulmadı.", L" The OEM profile file was not touched.");
      return n;
    case TxnClass::BothOk:
      n.level = Level::Info;
      n.text = w + Tw(L" uygulandı ve kalıcı kaydedildi. (a) Anlık: EC değeri geri okundu, NVIDIA ", L" applied and saved persistently. (a) Live: the EC value was read back, NVIDIA confirmed ") +
               nvTxt + Tw(L" olarak doğruladı. ", L". ") +
               Tw(L"(b) OEM profil dosyası: GPU.ConfigurableTGPTarget = ", L"(b) OEM profile file: GPU.ConfigurableTGPTarget = ") + std::to_wstring(t.live.target) +
               Tw(L" yazıldı ve geri okundu (önce özgün dosyanın doğrulanmış yedeği alındı). Yeniden başlatma sonrası etkisini bu uygulama "
                  L"doğrulayamaz; cihazda ayrıca gözlemlenir.",
                  L" was written and read back (a verified backup of the original file was taken first). This application cannot verify the "
                  L"effect after a restart; it has to be observed on the device separately.");
      return n;
    case TxnClass::BothOkAlready:
      n.level = Level::Info;
      n.text = w + Tw(L": (a) Anlık EC + NVIDIA değeri doğrulandı. (b) OEM profili zaten ", L": (a) The live EC + NVIDIA value was verified. (b) The OEM profile was already ") + w +
               Tw(L" idi ve doğrulandı; dosyaya yazılmadı, yeni yedek alınmadı.", L" and was verified; nothing was written to the file and no new backup was taken.");
      return n;
    case TxnClass::OemFailedLiveRestored:
      n.level = Level::Error;
      n.text = w + Tw(L" anlık uygulanmıştı ancak OEM profil dosyasına kaydedilemedi (", L" was applied live but could not be saved to the OEM profile file (") + OemCause(t) +
               Tw(L"). Yarım sonuç kalmasın diye anlık değişiklik geri alındı: GPU ayarı tekrar ",
                  L"). So that no half-finished result remains, the live change was rolled back: the GPU setting is again ") +
               (prev ? Watts(prev) : Tw(L"önceki değer", L"the previous value")) + Tw(L" ve geri alma doğrulandı.", L" and the rollback was verified.");
      return n;
    case TxnClass::OemFailedRestoreUnverified:
      n.level = Level::Error;
      n.text = Tw(L"KRİTİK: ", L"CRITICAL: ") + w + Tw(L" anlık uygulandı, OEM profil dosyasına kaydedilemedi (", L" was applied live, could not be saved to the OEM profile file (") +
               OemCause(t) +
               Tw(L") ve önceki değere dönüş DOĞRULANAMADI. EC durumu belirsiz olabilir; durum yeniden okunuyor.",
                  L") and the return to the previous value could NOT be verified. The EC state may be uncertain; the state is being re-read.");
      if (t.liveRestore.outcome == Outcome::FailedRollbackUnverified)
        n.text += Tw(L" Güvenlik için yazma kapalı: bilgisayarı yeniden başlatın, sonra \"Yenile\"ye basıp EC ve NVIDIA değerlerini kontrol edin.",
                     L" Writing is disabled for safety: restart the computer, then press \"Refresh\" and check the EC and NVIDIA values.");
      else
        n.text += Tw(L" Anlık profili kontrol edin; gerekirse istediğiniz profili yeniden seçin.",
                     L" Check the live profile; select the profile you want again if necessary.");
      return n;
    case TxnClass::OemFailedLiveUnchanged:
      n.level = Level::Error;
      n.text = Tw(L"OEM profil dosyasına kaydedilemedi (", L"It could not be saved to the OEM profile file (") + OemCause(t) +
               Tw(L"). Anlık GPU ayarı zaten ", L"). The live GPU setting was already ") + w + Tw(L" idi ve değişmedi.", L" and did not change.");
      return n;
  }
  return n;
}

// OEM dosyasının çapraz-süreç kilidi alınamadı: HİÇBİR ŞEY uygulanmadı (geçici uygulama yoktur).
inline Notice PersistLockBusyNotice() {
  Notice n;
  n.level = Level::Warn;
  n.text = Tw(L"HİÇBİR ŞEY uygulanmadı: OEM profil dosyası başka bir işlem tarafından kullanılıyor (kilit alınamadı). "
              L"Biraz sonra profili tekrar seçin.",
              L"NOTHING was applied: the OEM profile file is in use by another process (the lock could not be acquired). "
              L"Select the profile again in a moment.");
  return n;
}

inline const wchar_t* kPersistNoOwnership() {
  return T(L" Not: dosyanın bu uygulamanın yazdığı sürüm olduğunu gösteren kanıt oluşturulamadı; sonraki seçimde dosyadaki değer yabancı sayılıp "
           L"yeniden onay sorulabilir.",
           L" Note: no evidence could be created that the file is the version written by this application; on the next selection the value "
           L"in the file may be treated as foreign and a confirmation may be asked again.");
}
inline const wchar_t* kPersistMetaSaveFailed() {
  return T(L" UYARI: uygulama ayarları (sahiplik kaydı) diskte saklanamadı; sonraki seçimde dosyadaki değer yabancı sayılıp yeniden onay "
           L"sorulabilir.",
           L" WARNING: the application settings (ownership record) could not be saved to disk; on the next selection the value in the file "
           L"may be treated as foreign and a confirmation may be asked again.");
}

// "Hakkında" düğmesi: yalnız kullanıcı tıklayınca varsayılan tarayıcıda proje sayfasını açar (uygulamanın kendisi ağ bağlantısı kurmaz).
inline const wchar_t* kAboutLabel() { return T(L"Hakkında", L"About"); }
inline Notice AboutFailedNotice() {
  Notice n;
  n.level = Level::Warn;
  n.text = Tw(L"Tarayıcı açılamadı. Proje sayfası: ", L"The browser could not be opened. Project page: ") + kRepoUrl;
  return n;
}

// ======================================================================================================================
// Açılışta başlatma (Görev Zamanlayıcı görevi: MonsterGpuPower_Autostart) ve çıkışta OEM 140 W'a dönüş.
// Açılış görevi uygulamayı tepside başlatır; hiçbir GPU profili UYGULAMAZ (seçilen profil zaten OEM profil dosyasında kalıcıdır).
// ======================================================================================================================
inline const wchar_t* kAutostartLabel() { return T(L"Uygulamayı açılışta başlat", L"Start the app at login"); }
inline const wchar_t* kAutostartDesc() {
  return T(L"Windows oturum açılışında uygulamayı tepside başlatır; kendi başına profil uygulamaz.",
           L"Starts the app in the tray at Windows sign-in; it does not apply a profile by itself.");
}
inline const wchar_t* kRestoreLabel() { return T(L"Çıkışta OEM 140 W'a dön", L"Return to OEM 140 W on exit"); }
inline const wchar_t* kRestoreDesc() {
  return T(L"Çıkarken anlık güç OEM 140 W'a döner; OEM dosyasındaki kalıcı seçiminiz değişmez.",
           L"On exit the live power returns to OEM 140 W; your persistent choice in the OEM file does not change.");
}

inline Notice AutostartOnNotice() {
  Notice n;
  n.text = Tw(L"Uygulama artık her Windows oturum açılışında (yaklaşık 10 sn sonra) tepside başlayacak. Bu görev profil uygulamaz; seçtiğiniz "
              L"profil zaten OEM profil dosyasında kalıcıdır.",
              L"The app will now start in the tray at every Windows sign-in (after about 10 s). This task does not apply a profile; the profile "
              L"you selected is already persistent in the OEM profile file.");
  return n;
}
inline Notice AutostartOffNotice() {
  Notice n;
  n.text = Tw(L"\"Uygulamayı açılışta başlat\" kapatıldı: açılış görevi silindi ve silindiği doğrulandı.",
              L"\"Start the app at login\" was turned off: the startup task was deleted and its deletion was verified.");
  return n;
}
inline Notice AutostartEnableFailedNotice(const std::wstring& err) {
  Notice n;
  n.level = Level::Error;
  n.text = Tw(L"\"Uygulamayı açılışta başlat\" açılamadı: ", L"\"Start the app at login\" could not be turned on: ") + err +
           Tw(L". Hiçbir ayar değiştirilmedi.", L". No setting was changed.");
  return n;
}
inline Notice AutostartNotVerifiedNotice() {
  Notice n;
  n.level = Level::Warn;
  n.text = Tw(L"Açılış görevi oluşturuldu ama Görev Zamanlayıcı'dan beklenen biçimde okunamadı (etkin, bu uygulamanın yolu, /minimized). "
              L"Anahtar kapalı gösteriliyor; bir sonraki açılışta uygulamanın başlayacağı varsayılmamalıdır.",
              L"The startup task was created but could not be read back from Task Scheduler as expected (enabled, this application's path, "
              L"/minimized). The switch is shown as off; do not assume the application will start at the next sign-in.");
  return n;
}
inline Notice AutostartDisableFailedNotice(const std::wstring& err) {
  Notice n;
  n.level = Level::Warn;
  n.text = Tw(L"Açılış görevi silinemedi ya da silindiği doğrulanamadı (", L"The startup task could not be deleted, or its deletion could not be verified (") + err +
           Tw(L"). Görev duruyor olabilir: uygulama açılışta başlamaya devam edebilir.",
              L"). The task may still exist: the application may keep starting at sign-in.");
  return n;
}
inline Notice AutostartUntrustedPathNotice() {
  Notice n;
  n.level = Level::Warn;
  n.text = Tw(L"Bu kopya kurulum sihirbazıyla Program Files altına kurulmadığı için açılışta başlatma etkinleştirilemez: yükseltilmiş bir açılış "
              L"görevi, kullanıcının yazabildiği bir klasördeki programı çalıştırmamalıdır.",
              L"Starting at login cannot be enabled because this copy was not installed under Program Files by the setup wizard: an elevated "
              L"startup task must not run a program from a folder the user can write to.");
  return n;
}
inline Notice AutostartBusyNotice() {
  Notice n;
  n.text = Tw(L"Açılış görevi işlemi sürüyor; bitince anahtarı tekrar deneyin.",
              L"A startup-task operation is in progress; try the switch again when it finishes.");
  return n;
}

// ======================================================================================================================
// Ana pencere: sabit etiketler ve satır içi metinler (main_window*.cpp)
// ======================================================================================================================
inline const wchar_t* ProfileSub(int i) {
  switch (i) {
    case 0: return T(L"varsayılan", L"default");
    case 1:
    case 2: return T(L"canlı test yok", L"no live test");
    case 3: return T(L"OEM profili", L"OEM profile");
    case 4: return T(L"OEM üstü", L"above OEM");
    default: return T(L"VBIOS azami", L"VBIOS max");
  }
}
inline const wchar_t* kRefresh() { return T(L"Yenile", L"Refresh"); }
inline const wchar_t* kResetConsents() { return T(L"Onayları sıfırla", L"Reset confirmations"); }
inline const wchar_t* kLangToggleName() { return T(L"Arayüz dili: EN / TR", L"Interface language: EN / TR"); }

inline const wchar_t* kWindowTitleDemo() {
  return T(L"MONSTER GPU POWER — DEMO (SİMÜLASYON, gerçek donanım değil)", L"MONSTER GPU POWER — DEMO (SIMULATION, not real hardware)");
}
inline const wchar_t* kHeaderSub() { return T(L"Monster Tulpar T5 V21.7  ·  RTX 3070 Laptop  ·  GPU güç profili", L"Monster Tulpar T5 V21.7  ·  RTX 3070 Laptop  ·  GPU power profile"); }
inline const wchar_t* kHeaderSubDemo() {
  return T(L"DEMO — SİMÜLASYON: sahte donanım, gerçek EC'ye yazılmaz", L"DEMO — SIMULATION: fake hardware, nothing is written to a real EC");
}
inline const wchar_t* kChipReading() { return T(L"Okunuyor…", L"Reading…"); }
inline const wchar_t* kChipApplying() { return T(L"Uygulanıyor…", L"Applying…"); }
inline const wchar_t* kChipReady() { return T(L"Hazır", L"Ready"); }
inline const wchar_t* kChipWriteOff() { return T(L"Yazma kapalı", L"Writing disabled"); }

inline const wchar_t* kCardActive() { return T(L"AKTİF PROFİL", L"ACTIVE PROFILE"); }
inline const wchar_t* kCardAgree() { return T(L"EC ve NVIDIA tutarlı", L"EC and NVIDIA agree"); }
inline const wchar_t* kCardDisagree() { return T(L"EC ile NVIDIA uyuşmuyor", L"EC and NVIDIA disagree"); }
inline const wchar_t* kCardNoNv() { return T(L"NVIDIA doğrulaması yok", L"No NVIDIA verification"); }
inline const wchar_t* kCardUnknownEc() { return T(L"Tanınmayan EC durumu", L"Unrecognized EC state"); }
inline const wchar_t* kCardUnverifiedDevice() { return T(L"Cihaz doğrulanamadı — salt okunur", L"Device not verified — read-only"); }
inline const wchar_t* kCardEcUnreadable() { return T(L"EC okunamadı", L"EC could not be read"); }

inline const wchar_t* kCellNvLimit() { return T(L"NVIDIA güç limiti", L"NVIDIA power limit"); }
inline const wchar_t* kCellEc() { return L"EC 0x0743 / 0x0744"; }
inline const wchar_t* kCellDraw() { return T(L"Anlık çekiş", L"Current draw"); }
inline const wchar_t* kCellTemp() { return T(L"Sıcaklık", L"Temperature"); }
inline const wchar_t* kCellUtil() { return T(L"GPU kullanımı", L"GPU usage"); }
inline const wchar_t* kCellVbios() { return T(L"VBIOS azami limit", L"VBIOS max limit"); }
inline const wchar_t* kStripDriver() { return T(L"Sürücü: ", L"Driver: "); }
inline const wchar_t* kStripIdentity() { return T(L"Cihaz kimliği: ", L"Device identity: "); }

inline const wchar_t* kNoticeReading() { return T(L"Cihaz ve sürücü durumu okunuyor…", L"Reading the device and driver state…"); }
inline Notice InconsistentNotice(const LiveState& s) {
  Notice n;
  n.level = Level::Warn;
  n.text = Tw(L"EC ", L"The EC shows ") + (s.ecProfile ? Watts(s.ecProfile) : Tw(L"tanınmayan bir durum", L"an unrecognized state")) +
           Tw(L" gösteriyor, NVIDIA ise ", L" while NVIDIA reports ") + (s.nv.ok ? Num1(s.nv.limitW) + L" W" : std::wstring(L"?")) +
           Tw(L" bildiriyor. NVML birkaç saniye gecikebilir; sürerse başka bir yazılım değiştirmiş olabilir.",
              L". NVML can lag by a few seconds; if this persists, another program may have changed it.");
  return n;
}
inline const wchar_t* kNoticeDemo() {
  return T(L"SİMÜLASYON: bu derleme sahte donanımla çalışır, gerçek EC'ye hiçbir şey yazılmaz.",
           L"SIMULATION: this build runs on fake hardware; nothing is written to a real EC.");
}
inline const wchar_t* kNoticeReady() {
  return T(L"Hazır. Bir profil seçin: değer EC'den geri okunur, NVIDIA sürücüsüyle doğrulanır ve OEM profil dosyasına kalıcı kaydedilir; "
           L"tutmazsa önceki değere dönülür.",
           L"Ready. Select a profile: the value is read back from the EC, verified with the NVIDIA driver and saved persistently to the OEM "
           L"profile file; if it does not hold, the previous value is restored.");
}
inline const wchar_t* kFooterTail() {
  return T(L"Ağ bağlantısı, telemetri ve reklam yoktur  ·  Üreticiyle bağlantısı olmayan bağımsız bir topluluk aracıdır",
           L"No network connection, telemetry or ads  ·  An independent community tool, not affiliated with the manufacturer");
}

inline const wchar_t* kMenuShow() { return T(L"Göster", L"Show"); }
inline const wchar_t* kMenuExit() { return T(L"Çıkış", L"Exit"); }
inline std::wstring MenuActive(int watts) {
  return Tw(L"Aktif profil: ", L"Active profile: ") + (watts ? Watts(watts) : Tw(L"bilinmiyor", L"unknown"));
}
inline Notice TrayFailedNotice() {
  Notice n;
  n.level = Level::Warn;
  n.text = Tw(L"Görev çubuğu bildirim alanı hazır olmadığı için tepside simge eklenemedi; pencere gösterildi.",
              L"The tray icon could not be added because the taskbar notification area is not ready; the window was shown instead.");
  return n;
}
inline Notice ExitWaitNotice() {
  Notice n;
  n.text = Tw(L"Profil uygulaması sürüyor; bitince uygulama kapanacak.", L"A profile is being applied; the application will close when it finishes.");
  return n;
}
inline Notice ExitRestoringNotice() {
  Notice n;
  n.text = Tw(L"Çıkış: OEM 140 W profiline dönülüyor…", L"Exit: returning to the OEM 140 W profile…");
  return n;
}
inline Notice ConsentsResetNotice() {
  Notice n;
  n.text = Tw(L"Kayıtlı onaylar sıfırlandı; ilgili profiller yeniden onay isteyecek.",
              L"The saved confirmations were reset; the related profiles will ask for confirmation again.");
  return n;
}
inline Notice StateReadingNotice() {
  Notice n;
  n.text = Tw(L"Durum okunuyor; birkaç saniye bekleyin.", L"Reading the state; please wait a few seconds.");
  return n;
}
inline Notice ApplyResultLostNotice() {
  Notice n;
  n.level = Level::Error;
  n.text = Tw(L"İşlem sonucu alınamadı; durum yeniden okunuyor. Gerekirse profili elle doğrulayın.",
              L"The operation result was not received; the state is being re-read. Verify the profile manually if necessary.");
  return n;
}
inline const wchar_t* kShutdownReason() {
  return T(L"GPU güç profili uygulanıyor (EC ve OEM profil dosyası yazımı sürüyor).",
           L"Applying the GPU power profile (EC and OEM profile file writes are in progress).");
}
inline const wchar_t* kPersistApplyingExit() { return T(L"OEM 140 W'a dönülüyor…", L"Returning to OEM 140 W…"); }
inline const wchar_t* kPersistApplying() {
  return T(L"Profil uygulanıyor; EC + NVIDIA doğrulanınca OEM profil dosyasına da kaydedilecek…",
           L"Applying the profile; it will be saved to the OEM profile file once EC + NVIDIA are verified…");
}
inline const wchar_t* kWindowCreateFailed() { return T(L"Pencere oluşturulamadı.", L"The window could not be created."); }
inline const wchar_t* kTagActive() { return T(L"AKTİF", L"ACTIVE"); }

}  // namespace txt
}  // namespace win
}  // namespace mgp
