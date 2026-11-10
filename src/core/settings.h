// Kullanıcı ayarları (saf ayrıştırma/serileştirme). Dosya G/Ç src/win/settings_store.cpp içindedir.
// Dosya yalnızca açık/kapalı bayrakları, onaylar ve sahiplik kanıtı taşır; EC adresi veya ham değer TAŞIMAZ.
// Eski sürümlerin anahtarları (startup_apply, monitor, auto_reapply, wake_reapply, persist_oem, last_profile, oem_orig_*) okunmaz;
// bir sonraki kayıtta dosyadan düşer.
#pragma once
#include <string>

namespace mgp {

// OEM profil dosyasında bu uygulamanın YAZDIĞI sürümün kanıtı. Yalnız bir KANITTIR: gerçek durum her zaman OEM JSON'undan okunur.
// Bu kanıt, dosyadaki değerin bu uygulamadan mı yoksa başka bir yazılımdan mı geldiğini ayırt etmek için kullanılır (yabancı değer onayı).
// Boşsa sahiplik kaydı yoktur (ör. yeni kurulum, ayar dosyası silinmiş).
struct OemOwnership {
  std::string ownedSha;  // dosyanın bu uygulamanın bıraktığı halinin SHA-256'sı (64 küçük harfli onaltılık)
  int ownedWatts = 0;    // o haldeki hedef (125..150)
  bool Present() const { return !ownedSha.empty() && ownedWatts != 0; }
  void Clear() { *this = OemOwnership(); }
};

struct Settings {
  bool restoreOnExit = false;      // çıkışta anlık (EC + NVIDIA) değeri OEM 140 W'a döndür; OEM dosyasına DOKUNMAZ (VARSAYILAN KAPALI)
  bool consentOverOem = false;     // >140 W riskini kullanıcı onayladı
  bool consentUnverified = false;  // 130/135 W "canlı doğrulanmadı" bilgisini kullanıcı onayladı
  bool consentModel = false;       // "bu cihaz Monster Tulpar T5 V21.7 (GM5TG7Y)" teyidi (SMBIOS'ta kart etiketi yoksa)
  OemOwnership oem;                // OEM dosyasına son yazılan sürümün sahiplik kanıtı
};

Settings ParseSettings(const std::string& text);
std::string SerializeSettings(const Settings& s);

}  // namespace mgp
