// Arayüz dili (saf mantık; Windows'a bağımlı değildir, birim testlerle sınanır).
// Dil seçimi yalnız kullanıcıya görünen metinleri değiştirir; EC/OEM davranışına hiçbir etkisi yoktur.
//
// Çözümleme sırası (ResolveLanguage):
//   1) Kullanıcı arayüzdeki EN/TR düğmesiyle bir dil seçtiyse (settings.ini: language) ve o seçimden SONRA kurulum sihirbazında farklı bir dil
//      seçilmediyse -> kullanıcı seçimi.
//   2) Kurulum sihirbazında seçilen dil (HKLM\Software\MonsterGpuPower\Language). Kurulum sonrası yeniden kurulum bu seçimi yeniler.
//   3) Windows arayüz dili Türkçe ise Türkçe; değilse İngilizce (ana dil İngilizcedir).
#pragma once
#include <string>

namespace mgp {

enum class Lang { Tr, En };

inline const char* LangCode(Lang l) { return l == Lang::Tr ? "tr" : "en"; }

// "tr" / "en" (büyük-küçük harf önemsiz). Başka her şey false (çıktı değişmez).
bool ParseLangCode(const std::string& s, Lang* out);

// Kurulum sihirbazının kaydettiği değer: NSIS dil kimliği ("1055" = Türkçe, "1033" = İngilizce) ya da "tr"/"en". Tanınmayan -> false.
bool ParseInstallerLanguage(const std::string& s, Lang* out);

struct LangInputs {
  std::string settingsLang;       // settings.ini: language ("" = kullanıcı hiç seçmedi)
  std::string settingsInstaller;  // settings.ini: language_installer = kullanıcı seçtiği anda kayıtlı kurulum dili ("" = kurulum dili yoktu)
  std::string installerLang;      // kayıt defterindeki kurulum dili, "tr"/"en" ya da "" (yok/tanınmıyor)
  bool systemIsTurkish = false;   // Windows arayüz dili Türkçe mi
};

Lang ResolveLanguage(const LangInputs& in);

// Kullanıcının eski dil seçimi, SONRADAN yapılan bir kurulumun dil seçimiyle geçersiz kılındı mı? (Kayıt defterinde kurulum dili var ve
// seçimin damgasındaki kurulum diliyle uyuşmuyor ya da damga yok.) true ise uygulama eski seçimi ayar dosyasından siler; böylece daha sonraki
// bir yeniden kurulumda aynı kurulum diline dönüldüğünde eski seçim "diriltilmez". Kurulum dili okunamıyorsa (kayıt değeri yok) false:
// geçici bir okuma sorunu kullanıcının seçimini silmemelidir.
bool UserChoiceSuperseded(const LangInputs& in);

}  // namespace mgp
