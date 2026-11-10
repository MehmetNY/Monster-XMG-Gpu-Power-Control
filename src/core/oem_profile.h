// OEM profil deposu: aktif OEM profil dosyasındaki GPU.ConfigurableTGPTarget'ı GÜVENLE okur ve değiştirir.
//
// Bu birim PowerProfileEngine'in EC IOCTL katmanından BAĞIMSIZDIR. EC/BIOS/registry/AdpType/230 W adaptör profiline ve
// GCUService'in çalışmasına DOKUNMAZ; yalnız Mode3_Profile1.json içindeki tek sayının 3 hanesini değiştirir.
// Cihaz sahibinin elle denediği PowerShell yönteminin (JSON'daki tek alanın değiştirilmesi) native karşılığıdır.
//
// Yazma algoritması ("Z"):
//   1. Ön koşullar (servis yolu/sürümü, güvenli dizin, MainOption: Turbo profil 1, Activated, Switch=1, DynamicBoostSwitch=0, izinli hedef).
//   2. Orijinalin TAM baytları, zaman damgalı ve çakışmasız bir .bak olarak YENİ dosyaya yazılır, flush edilir, geri okunup doğrulanır.
//   3. Yamalı içerik aynı dizinde geçici dosyaya yazılır, flush edilir, geri okunup çözümlenir (yalnız 3 bayt farklı).
//   4. TOCTOU: aktif dosya (SHA-256 + dosya kimliği + boyut + mtime) ve tüm ön koşullar YENİDEN denetlenir.
//   5. ReplaceFileW (atomik); eski içerik "swap" yedeğine taşınır. Swap, adım 2'deki doğrulanmış baytlarla AYNI olmalıdır; değilse
//      araya başka bir yazıcı girmiştir -> o sürüm geri konur (hiçbir yabancı yazma ezilmez).
//   6. Aktif dosya yeniden açılıp hedef ve tüm öbür baytlar doğrulanır. Başarısız/yarım durumda gerçek durum raporlanır, yedekler KORUNUR.
#pragma once
#include <string>
#include <vector>

#include "interfaces.h"
#include "oem_json.h"
#include "oem_names.h"
#include "oem_platform.h"

namespace mgp {
namespace oem {

// Modülün kilitlenme nedeni. Yalnız YENİ kalıcılık modülünü kilitler; uygulamanın diğer özellikleri etkilenmez.
enum class Lock {
  None,
  NotAdmin,            // uygulama yönetici hakkıyla çalışmıyor
  ServiceNotRunning,   // GCUService.exe çalışmıyor
  ServicePath,         // çalışan GCUService beklenen OEM yolunda değil
  ServiceVersion,      // sürüm 1.0.2.70 değil / okunamadı
  DirUnsafe,           // UserPofiles yok / link-reparse / kanonik değil
  MainOptionIo,        // MainOption.json okunamadı
  MainOptionFormat,    // MainOption.json beklenen biçimde değil
  ProfileNotActive,    // Turbo profil 1 (OperatingMode=2, TurboProfileIndex=0) aktif değil
  ProfileIo,           // Mode3_Profile1.json okunamadı
  ProfileEncoding,     // UTF-16 / geçersiz UTF-8 / boş / çok büyük
  ProfileFormat,       // bozuk JSON, eksik/çoklu hedef, sayı yerine dize ...
  ProfileNotActivated, // Activated != true
  SwitchOff,           // GPU.ConfigurableTGPSwitch != 1
  DynamicBoostOn,      // GPU.DynamicBoostSwitch != 0
  TargetUnsupported,   // hedef 125/130/135/140/145/150 dışında
  Changed,             // dosya ön kontrol ile commit arasında değişti (başka yazıcı)
};
const char* LockName(Lock l);

struct Inspection {
  bool ok = false;             // modül kullanılabilir: tüm ön koşullar sağlandı
  Lock lock = Lock::None;      // ok değilse neden
  std::string detail;          // teknik açıklama (günlük)
  ServiceInfo svc;
  bool haveFile = false;       // aşağıdaki dosya alanları dolu (kilitli olsa bile okunabildiyse)
  int target = 0;              // dosyadaki GPU.ConfigurableTGPTarget (haveFile && ayrıştırılabildiyse)
  std::string sha;             // aktif dosyanın SHA-256'sı
  FileStamp stamp;
  size_t size = 0;
};

enum class CommitKind {
  Committed,            // yazıldı, geri okundu, doğrulandı
  AlreadyCurrent,       // hedef zaten bu değer: yazma ve yeni yedek YOK
  Refused,              // ön koşul/doğrulama reddetti; aktif dosya DEĞİŞMEDİ
  FailedNoChange,       // G/Ç hatası; aktif dosya DEĞİŞMEDİ (doğrulandı)
  FailedIndeterminate   // dosyanın son durumu doğrulanamadı: kullanıcıya gerçek durum + yedek gösterilir
};

struct CommitResult {
  CommitKind kind = CommitKind::Refused;
  Lock lock = Lock::None;          // Refused ise neden
  std::string detail;              // teknik
  int fromWatts = 0, toWatts = 0;
  std::string shaBefore, shaAfter;
  std::string backupName, backupSha;  // bu işlemde oluşturulan doğrulanmış yedek (varsa)
  bool jsonVerified = false;       // yazıldıktan sonra geri okuma doğrulandı (veya zaten hedefte)
  std::vector<std::string> leftovers;  // silinemeyen geçici/swap dosya adları (zararsız, bilgi)
  bool Success() const { return kind == CommitKind::Committed || kind == CommitKind::AlreadyCurrent; }
};

class OemProfileStore {
 public:
  OemProfileStore(IOemPlatform& platform, ILog& log) : p_(platform), log_(log) {}

  // Salt okunur: servis + dosyalar denetlenir. Hiçbir şey yazmaz.
  Inspection Inspect();

  // GPU.ConfigurableTGPTarget'ı `watts` yapar. expectSha boş değilse aktif dosyanın SHA-256'sı BUNA eşit olmalıdır
  // (ön kontrolde görülen / kullanıcının onayladığı sürüm). Hedef zaten `watts` ise YAZMA YOK.
  CommitResult Commit(int watts, const std::string& expectSha);

 private:
  struct Loaded {
    Inspection insp;
    std::vector<uint8_t> bytes;
    ProfileFacts facts;
  };
  bool Load(Loaded& l);
  void Log(LogLevel lv, const std::string& m) { log_.Write(lv, m); }

  IOemPlatform& p_;
  ILog& log_;
};

}  // namespace oem
}  // namespace mgp
