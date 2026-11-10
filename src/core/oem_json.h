// OEM profil JSON'u için katı, bağımlılıksız okuma/yama katmanı (saf mantık; OS çağrısı yok).
//
// Neden kendi ayrıştırıcımız var: yalnız "GPU.ConfigurableTGPTarget" sayısının 3 hanesi DEĞİŞTİRİLİR; dosyanın geri kalanı
// (BOM, satır sonları, boşluklar, Türkçe karakterler, tüm diğer alanlar) bayt bayt aynı kalmalıdır. Bunun için sayının
// dosyadaki TAM bayt konumunu bilen, tek ve belirsizliksiz bir ayrıştırıcı gerekir. Hedef: tek benzersiz alan +
// ayrıştırma ile çapraz doğrulama (cihaz sahibinin elle denediği PowerShell yönteminin davranışı).
//
// Katılık (fail-closed): geçersiz UTF-8, UTF-16, BOM sonrası ek BOM, kopya anahtar, bozuk söz dizimi, aşırı derinlik,
// artık veri, hedefin birden fazla/yanlış yerde olması, sayı yerine dize -> HEPSİ reddedilir ve hiçbir bayt üretilmez.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mgp {
namespace oem {

inline constexpr size_t kMaxJsonBytes = 1u << 20;  // 1 MiB: OEM profil dosyaları birkaç KiB'dir; fazlası şüphelidir
inline constexpr int kMaxJsonDepth = 48;
inline constexpr size_t kMaxJsonNodes = 100000;

// ---------------------------------------------------------------------------------------------------- UTF-8 zarfı
enum class EnvelopeError { None, Empty, TooLarge, Utf16OrUtf32, InvalidUtf8 };

struct Envelope {
  bool bom = false;       // dosya EF BB BF ile başlıyor
  size_t bodyOffset = 0;  // JSON gövdesinin dosyadaki başlangıcı (BOM varsa 3)
};

// RFC 3629 katı UTF-8 (kısa kodlama, vekil çiftler, > U+10FFFF reddedilir).
bool IsStrictUtf8(const uint8_t* p, size_t n);

// Dosya baytlarının zarfını doğrular: boş değil, <= kMaxJsonBytes, UTF-16/32 BOM'u yok, BOM'dan sonrası katı UTF-8.
EnvelopeError CheckEnvelope(const std::vector<uint8_t>& bytes, Envelope& env);
const char* EnvelopeErrorName(EnvelopeError e);

// ---------------------------------------------------------------------------------------------------- JSON DOM
enum class JKind : uint8_t { Null, True, False, Number, String, Array, Object };

struct JNode {
  JKind kind = JKind::Null;
  size_t begin = 0, end = 0;     // gövde içindeki bayt aralığı [begin, end)
  bool isInteger = false;        // Number: -?(0|[1-9][0-9]*) ve kesir/üs YOK
  bool intFits = false;          // isInteger ve değer long long'a sığıyor (<= 18 hane)
  long long intValue = 0;
  std::string str;               // String: çözülmüş UTF-8
  std::vector<std::pair<std::string, int>> members;  // Object: (çözülmüş anahtar, düğüm indeksi) - belge sırasıyla
  std::vector<int> items;        // Array: düğüm indeksleri
};

class JsonDoc {
 public:
  // body: BOM'suz gövde (UTF-8 zaten doğrulanmış olmalı). Hata durumunda err teknik/İngilizce açıklama alır.
  bool Parse(const uint8_t* body, size_t n, std::string& err);
  const JNode& Root() const { return nodes_[0]; }
  // Nesnede tam eşleşen anahtar (kopya anahtar zaten reddedildiği için en çok bir tane). Yoksa nullptr.
  const JNode* Member(const JNode& obj, const char* key) const;
  // Belgenin HERHANGİ bir derinliğindeki nesnelerde bu anahtarın kaç kez geçtiği.
  size_t CountKeyAnywhere(const char* key) const;

 private:
  friend struct Parser;
  std::vector<JNode> nodes_;
};

// ---------------------------------------------------------------------------------------------------- Profil şeması
enum class Defect {
  None,
  Envelope,        // zarf hatası (Facts::envelope dolu)
  Syntax,          // JSON söz dizimi / kopya anahtar / derinlik / artık veri
  RootNotObject,
  Missing,         // beklenen alan yok
  WrongType,       // alan beklenen türde değil (ör. sayı yerine dize)
  TargetAmbiguous, // ConfigurableTGPTarget birden fazla veya GPU nesnesi dışında
  TargetFormat     // hedef tam 3 haneli tamsayı belirteci değil
};

// 0/1 bayrak: JSON true/false veya tamsayı 0/1 (PowerShell betiğinin -eq $true / [int] davranışı ile uyumlu). Başkası -1.
int FlagValue(const JsonDoc& d, const JNode* n);

struct ProfileFacts {
  bool ok = false;           // yapı geçerli ve hedefin bayt konumu belli
  Defect defect = Defect::None;
  EnvelopeError envelope = EnvelopeError::None;
  std::string detail;        // teknik/İngilizce
  bool bom = false;
  size_t bodyOffset = 0;
  int activated = -1;        // Activated (kök)
  int tgpSwitch = -1;        // GPU.ConfigurableTGPSwitch
  int dynBoostSwitch = -1;   // GPU.DynamicBoostSwitch
  int target = 0;            // GPU.ConfigurableTGPTarget (ok iken 100..999 arası 3 haneli)
  size_t targetFileOffset = 0;  // hedefin 3 hanesinin DOSYADAKİ (BOM dahil) bayt konumu
};

// Mode3_Profile1.json biçimli profil dosyasını çözümler. Politika (değer izinli mi, Activated/Switch uygun mu) ÇAĞIRANIN işidir.
ProfileFacts ParseProfile(const std::vector<uint8_t>& bytes);

struct MainOptionFacts {
  bool ok = false;
  Defect defect = Defect::None;
  EnvelopeError envelope = EnvelopeError::None;
  std::string detail;
  long long operatingMode = -1;
  long long turboProfileIndex = -1;
};
MainOptionFacts ParseMainOption(const std::vector<uint8_t>& bytes);

// ---------------------------------------------------------------------------------------------------- Yama
// original: tam dosya baytları; newWatts: izinli profil değeri (FindProfile). Yalnız hedefin 3 hanesi değiştirilir.
// Çıktı doğrulanır: aynı uzunluk, yalnız o 3 bayt farklı, yeniden çözümlenince yeni hedef, diğer tüm alanlar aynı.
bool PatchTarget(const std::vector<uint8_t>& original, int newWatts, std::vector<uint8_t>& out, std::string& err);

// a ve b geçerli profil dosyaları mı ve hedefin 3 hanesi DIŞINDA bayt bayt aynı mı? (yedek/sahiplik kanıtı)
bool SameModuloTarget(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b);

}  // namespace oem
}  // namespace mgp
