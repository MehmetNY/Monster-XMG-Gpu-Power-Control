#include "language.h"

#include <cctype>

namespace mgp {
namespace {
std::string Lower(const std::string& s) {
  std::string o;
  for (char c : s) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return o;
}
std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}
}  // namespace

bool ParseLangCode(const std::string& s, Lang* out) {
  const std::string v = Lower(Trim(s));
  if (v == "tr") {
    if (out) *out = Lang::Tr;
    return true;
  }
  if (v == "en") {
    if (out) *out = Lang::En;
    return true;
  }
  return false;
}

bool ParseInstallerLanguage(const std::string& s, Lang* out) {
  const std::string v = Trim(s);
  if (v == "1055") {
    if (out) *out = Lang::Tr;
    return true;
  }
  if (v == "1033") {
    if (out) *out = Lang::En;
    return true;
  }
  return ParseLangCode(v, out);
}

bool UserChoiceSuperseded(const LangInputs& in) {
  Lang user = Lang::En, inst = Lang::En, stamped = Lang::En;
  if (!ParseLangCode(in.settingsLang, &user)) return false;   // ortada kullanıcı seçimi yok
  if (!ParseLangCode(in.installerLang, &inst)) return false;  // kurulum dili yok/okunamıyor: dokunma
  const bool haveStamp = ParseLangCode(in.settingsInstaller, &stamped);
  return !(haveStamp && stamped == inst);
}

Lang ResolveLanguage(const LangInputs& in) {
  Lang user = Lang::En, inst = Lang::En;
  const bool haveUser = ParseLangCode(in.settingsLang, &user);
  const bool haveInst = ParseLangCode(in.installerLang, &inst);
  if (haveUser) {
    // Kullanıcı seçimi, seçtiği andaki kurulum dilinde YAŞAR. Kurulum dili sonradan değiştiyse (yeniden kurulumda başka dil seçildi) yeni
    // kurulum seçimi kazanır; aksi hâlde yeniden kurulumda seçilen dil hiç görünmezdi.
    Lang stamped = Lang::En;
    const bool haveStamp = ParseLangCode(in.settingsInstaller, &stamped);
    const bool installerUnchanged = haveInst ? (haveStamp && stamped == inst) : !haveStamp;
    if (installerUnchanged) return user;
  }
  if (haveInst) return inst;
  return in.systemIsTurkish ? Lang::Tr : Lang::En;
}

}  // namespace mgp
