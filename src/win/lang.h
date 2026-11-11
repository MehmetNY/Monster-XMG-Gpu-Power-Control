// Arayüz dili: işlem genelinde tek değer + T() yardımcısı. Metinler strings.h içindedir.
// UI language: one process-wide value + the T() helper. All user-visible texts live in strings.h (Turkish + English side by side).
#pragma once
#include <atomic>
#include <string>

#include "../core/language.h"
#include "../core/settings.h"
#include "win_util.h"

namespace mgp {
namespace win {

inline std::atomic<int>& LangCell() {
  static std::atomic<int> v{static_cast<int>(Lang::En)};
  return v;
}
inline Lang CurrentLang() { return static_cast<Lang>(LangCell().load()); }
inline void SetCurrentLang(Lang l) { LangCell().store(static_cast<int>(l)); }

// (tr, en) çiftinden geçerli dilin metnini seçer.
inline const wchar_t* T(const wchar_t* tr, const wchar_t* en) { return CurrentLang() == Lang::Tr ? tr : en; }
inline std::wstring Tw(const wchar_t* tr, const wchar_t* en) { return std::wstring(T(tr, en)); }

// Başlangıç dilini çözer (ayar -> kurulum sihirbazı değeri -> Windows arayüz dili -> İngilizce). Salt okunur; hiçbir şey yazmaz.
Lang DetectStartupLanguage(const Settings& s);
// Kullanıcının eski dil seçimi, sonradan yapılan bir kurulumun dil seçimiyle geçersiz kılındıysa true (ayar dosyasından silinmelidir).
bool UserLanguageSuperseded(const Settings& s);
// Kayıt defterindeki kurulum dili ("tr"/"en"; yoksa ""). Kullanıcı seçimini kurulum diliyle damgalamak için.
std::string InstallerLanguageCode();

}  // namespace win
}  // namespace mgp
