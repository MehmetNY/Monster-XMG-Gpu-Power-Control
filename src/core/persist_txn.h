// Profil seçiminin işlem akışı (saf mantık; sahte EC/NVML + sahte OEM platformuyla test edilir). Her profil seçimi KALICIDIR:
//
//   Kullanıcı seçimi -> OEM ön kontrol (salt okunur) -> engine.Apply (EC + NVML doğrulaması) -> OEM JSON güvenli commit
//   -> JSON geri okuma -> arayüz İKİ katmanı ayrı raporlar.
//
//  * OEM profil modülü kullanılamıyorsa (servis yok/sürüm farklı ...) HİÇBİR ŞEY uygulanmaz: geçici (yalnız anlık) uygulama yoktur.
//  * OEM dosyasında bu uygulamanın yazmadığı (ya da yazdığından sonra değişmiş) 140 W dışı bir değer varsa ve hedef onu değiştirecekse,
//    kullanıcı o sürümü AÇIKÇA onaylamadan hiçbir şey uygulanmaz (needsApproval; canlı katmana da dokunulmaz).
//  * Anlık uygulama başarısızsa OEM dosyası ASLA değiştirilmez.
//  * Anlık uygulama başarılı, JSON yazımı başarısızsa: kullanıcının seçmediği yarım sonuç kalmasın diye, önceki canlı duruma
//    mevcut güvenlik ilkeleriyle (PowerProfileEngine::RestoreLive) dönülmeye çalışılır; dönülemezse mevcut CRITICAL kilidi korunur.
#pragma once
#include <string>

#include "interfaces.h"
#include "oem_profile.h"
#include "power_profile_engine.h"
#include "settings.h"

namespace mgp {

// OEM dosyasında, bu uygulamanın yazdığına dair kanıtı OLMAYAN (ya da bu uygulamanın bıraktığından sonra değişmiş) ve OEM varsayılanı
// (140 W) OLMAYAN bir değer var mı ve seçilen hedef onu DEĞİŞTİRECEK mi? Evetse kullanıcıya AÇIKÇA sorulmadan üzerine yazılmaz.
enum class ForeignKind {
  None,              // ezilecek yabancı değer yok (hedef zaten aynı / 140 W varsayılanı / bu uygulamanın bıraktığı sürüm / modül kilitli)
  UnownedValue,      // sahiplik kaydı yok ve dosyada 140 W dışında bir değer var (Control Center, başka program ya da elle yapılmış olabilir)
  ChangedAfterOurs   // sahiplik kaydı var ama dosya bu uygulamanın bıraktığı sürümden farklı ve değer 140 W değil
};
ForeignKind ClassifyForeign(const oem::Inspection& pre, const OemOwnership& own, int targetWatts);

struct PersistTxnInput {
  int watts = 0;
  ApplyOptions opts;
  std::string approvedSha;      // boş değilse: ön kontroldeki OEM dosyası TAM bu SHA-256'da olmalı (kullanıcı onayı sürümü); değilse HİÇBİR ŞEY uygulanmaz
  OemOwnership own;             // ayardaki sahiplik kanıtı (yabancı değer tespiti için)
  bool foreignApproved = false; // kullanıcı yabancı değerin üzerine yazılmasını approvedSha sürümü İÇİN onayladı
};

struct PersistTxnResult {
  ApplyResult live;                 // katman (a): EC + NVIDIA
  bool aborted = false;             // approvedSha uyuşmadı: hiçbir şey uygulanmadı
  bool needsApproval = false;       // OEM dosyasında yabancı bir değer var: kullanıcı onaylamadan HİÇBİR ŞEY uygulanmadı (canlı katman dahil)
  bool oemUnavailable = false;      // OEM profil modülü kullanılamıyor: HİÇBİR ŞEY uygulanmadı (neden: pre.lock)
  ForeignKind foreign = ForeignKind::None;
  oem::Inspection pre;              // ön kontrol
  bool persistAttempted = false;    // OEM commit çağrıldı
  oem::CommitResult oem;            // katman (b) (persistAttempted ise)
  bool liveRestoreAttempted = false;
  ApplyResult liveRestore;          // OEM yazımı başarısız olduktan sonra canlı duruma dönüş sonucu
};

PersistTxnResult RunPersistTransaction(PowerProfileEngine& engine, oem::OemProfileStore& store, ILog& log, const PersistTxnInput& in);

// İki katmanlı işlem sonucunun sınıfı. Kullanıcıya her katman AYRI söylenir; ikisi doğrulanmadan "tam başarı" denmez.
enum class TxnClass {
  NeedsApproval,                // OEM dosyasında yabancı bir değer var: kullanıcı onaylamadan hiçbir şey uygulanmadı
  Aborted,                      // onaylanan sürümden farklı: hiçbir şey uygulanmadı
  OemUnavailable,               // OEM profil modülü kullanılamıyor: hiçbir şey uygulanmadı
  LiveFailed,                   // anlık katman başarısız: OEM dosyasına DOKUNULMADI
  BothOk,                       // anlık tamam + OEM dosyası yazıldı ve geri okundu
  BothOkAlready,                // anlık tamam + OEM dosyası zaten bu değerde (yazma yok)
  OemFailedLiveRestored,        // OEM yazımı başarısız; anlık değişiklik geri alındı ve doğrulandı
  OemFailedRestoreUnverified,   // OEM yazımı başarısız ve anlık geri alma DOĞRULANAMADI (KRİTİK)
  OemFailedLiveUnchanged        // OEM yazımı başarısız; anlık profil zaten hedefteydi (geri alma gerekmedi)
};
TxnClass ClassifyTxn(const PersistTxnResult& t);

// İşlem sonrası okunan dosya, bu işlemin yazıp DOĞRULADIĞI dosya mı? Değilse (araya başka program girip değiştirmiş) sahiplik
// KAYDEDİLMEZ: bu uygulamanın yazmadığı bir sürüm "bizim" sayılmaz. Doğruysa sahiplik kanıtını üretir; değilse boş döner.
OemOwnership OwnershipFromTxn(const oem::Inspection& after, const PersistTxnResult& t);

}  // namespace mgp
