#include "lang.h"

namespace mgp {
namespace win {
namespace {

#ifdef MGP_DEMO
std::string EnvA(const char* name) {
  char b[32] = {0};
  const DWORD n = GetEnvironmentVariableA(name, b, sizeof b);
  return (n > 0 && n < sizeof b) ? std::string(b) : std::string();
}
#endif

// HKLM\Software\MonsterGpuPower\Language (kurulum sihirbazının yazdığı dil kimliği). 64 ve 32 bit görünümü denenir.
std::string ReadInstallerLanguageRaw() {
  const REGSAM views[2] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
  for (REGSAM v : views) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\MonsterGpuPower", 0, KEY_QUERY_VALUE | v, &k) != ERROR_SUCCESS) continue;
    wchar_t buf[32] = {0};
    DWORD type = 0, size = sizeof(buf) - sizeof(wchar_t);
    const LONG rc = RegQueryValueExW(k, L"Language", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(k);
    if (rc == ERROR_SUCCESS && type == REG_SZ) return Narrow(std::wstring(buf));
  }
  return std::string();
}

bool SystemUiIsTurkish() {
#ifdef MGP_DEMO
  // YALNIZ demo: Windows arayüz dilini taklit eder (otomatik dil seçimini sınamak için).
  const std::string sim = EnvA("MGP_DEMO_SYSLANG");
  Lang l;
  if (ParseLangCode(sim, &l)) return l == Lang::Tr;
#endif
  return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_TURKISH;
}

}  // namespace

std::string InstallerLanguageCode() {
  Lang l;
  return ParseInstallerLanguage(ReadInstallerLanguageRaw(), &l) ? LangCode(l) : std::string();
}

static LangInputs InputsFor(const Settings& s) {
  LangInputs in;
  in.settingsLang = s.language;
  in.settingsInstaller = s.languageInstaller;
  in.installerLang = InstallerLanguageCode();
  in.systemIsTurkish = SystemUiIsTurkish();
  return in;
}

bool UserLanguageSuperseded(const Settings& s) {
#ifdef MGP_DEMO
  {
    Lang forced;  // YALNIZ demo/test: dil zorlanmışsa ayar dosyasına dokunma
    if (ParseLangCode(EnvA("MGP_DEMO_LANG"), &forced)) return false;
  }
#endif
  return UserChoiceSuperseded(InputsFor(s));
}

Lang DetectStartupLanguage(const Settings& s) {
#ifdef MGP_DEMO
  // YALNIZ demo/test: MGP_DEMO_LANG=tr|en dili doğrudan zorlar (ayar dosyası ve kayıt defteri yok sayılır).
  {
    Lang forced;
    if (ParseLangCode(EnvA("MGP_DEMO_LANG"), &forced)) return forced;
  }
#endif
  return ResolveLanguage(InputsFor(s));
}

}  // namespace win
}  // namespace mgp
