// OEM profil dosyası arka ucunun (platform) arayüzü. PowerProfileEngine'in EC IOCTL katmanından BİLİNÇLİ olarak ayrıdır:
// bu katman donanıma değil, OEM GCUService'in mevcut profil DOSYASINA dokunur. Win32 gerçeklemesi win/oem_platform_win.cpp,
// testlerdeki bellek içi sahte gerçeklemesi tests/mocks.h içindedir.
//
// Güvenlik sözleşmesi:
//  * Tüm dosya işlemleri YALNIZ ProbeService()'in doğruladığı OEM "UserPofiles" dizini içindedir. Çağıran yalnız DÜZ dosya adı
//    (leaf) verir; yol ayracı, ".." veya sürücü içeren ad platformda reddedilir. Kullanıcı yol girdisi yoktur.
//  * Dosya/dizin bir bağlantı noktası (reparse), sembolik/sert bağ ise veya kanonik yolu dizin dışına çıkıyorsa işlem reddedilir.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mgp {
namespace oem {

// Bir dosyanın o andaki kimliği/durumu. Değişiklik tespiti içindir (SHA-256 ile birlikte).
struct FileStamp {
  bool valid = false;
  uint64_t id = 0;     // birim seri no + 64 bit dosya dizini (opak)
  uint64_t size = 0;
  uint64_t mtime = 0;  // FILETIME (100 ns)
  uint32_t links = 1;
};
inline bool SameStamp(const FileStamp& a, const FileStamp& b) {
  return a.valid && b.valid && a.id == b.id && a.size == b.size && a.mtime == b.mtime;
}

enum class IoCode { Ok, NotFound, AccessDenied, Sharing, DiskFull, Exists, Unsafe, TooLarge, Other };

struct Io {
  IoCode code = IoCode::Ok;
  uint32_t sys = 0;  // Win32 hata kodu (varsa)
  std::string msg;   // teknik açıklama (kullanıcı adı içermez)
  bool ok() const { return code == IoCode::Ok; }
};

struct FileData {
  Io io;
  std::vector<uint8_t> bytes;
  FileStamp stamp;
};

struct ServiceInfo {
  bool elevated = true;      // bu işlem yönetici hakkıyla çalışıyor (üretimde manifest zorunlu kılar; yine de her yazmada denetlenir)
  bool running = false;      // 'GCUService.exe' adlı çalışan proses bulundu
  bool pathMatches = false;  // çalışan prosesin TAM yolu = beklenen OEM yolu (Program Files\OEM\Control Center\UniwillService\MyControlCenter\GCUService.exe)
  bool versionRead = false;
  std::string version;       // dosya sürümü dizesi (FileVersion), ör. "1.0.2.70"
  bool dirSafe = false;      // UserPofiles bulundu, gerçek dizin, link/reparse yok, kanonik yol beklenen
  std::string detail;        // teknik açıklama (günlük); kullanıcı adı içermez
};

class IOemPlatform {
 public:
  virtual ~IOemPlatform() = default;

  // Çalışan GCUService'i ve UserPofiles dizinini belirler/doğrular. Her yazma ve sahiplenme öncesi yeniden çağrılır.
  virtual ServiceInfo ProbeService() = 0;

  // Dosyanın TAMAMINI okur (azami kMaxJsonBytes; fazlası TooLarge). Paylaşım: okuma+yazma+silme serbest (OEM hizmetini engellemez).
  virtual FileData ReadFile(const std::string& leaf) = 0;

  // YENİ dosya: CREATE_NEW (var olanı asla ezmez), tüm baytlar yazılır, FlushFileBuffers. Hata olursa yarım dosyayı kaldırır.
  virtual Io CreateNew(const std::string& leaf, const std::vector<uint8_t>& data, bool readOnly) = 0;

  // AtomicReplace (Win32: ReplaceFileW): `replacement` dosyası `active` adını alır; eski `active` içeriği `swapBackup` adıyla KORUNUR (atomik).
  // swapBackup boş olamaz ve var olmamalıdır. Birleştirme hatalarını (ACL/öznitelik) YOK SAYMAZ.
  virtual Io AtomicReplace(const std::string& active, const std::string& replacement, const std::string& swapBackup) = 0;

  // Yalnız bu işlem tarafından üretilmiş geçici adları (.mgp-tmp-/.mgp-swap-/.mgp-rejected-) siler. Başka ad reddedilir.
  virtual Io RemoveOwn(const std::string& leaf) = 0;

  virtual std::string StampNow() = 0;   // yyyyMMdd-HHmmss (yerel saat)
  virtual std::string RandomId8() = 0;  // 8 küçük harfli onaltılık hane
};

}  // namespace oem
}  // namespace mgp
