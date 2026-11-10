#include "settings.h"

#include <cstdlib>
#include <sstream>

#include "ec_constants.h"
#include "sha256.h"

namespace mgp {
namespace {
std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
  return s.substr(b, e - b);
}
bool Flag(const std::string& v) { return v == "1"; }
int WattsOrZero(const std::string& v) {
  char* endp = nullptr;
  const long n = std::strtol(v.c_str(), &endp, 10);
  return (endp && *endp == '\0' && !v.empty() && FindProfile(static_cast<int>(n)) != nullptr) ? static_cast<int>(n) : 0;
}
}  // namespace

Settings ParseSettings(const std::string& text) {
  Settings s;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    // UTF-8 BOM'u tolere et
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF)
      line = line.substr(3);
    line = Trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
    if (k == "restore_on_exit") s.restoreOnExit = Flag(v);
    else if (k == "consent_over_oem") s.consentOverOem = Flag(v);
    else if (k == "consent_unverified") s.consentUnverified = Flag(v);
    else if (k == "consent_model") s.consentModel = Flag(v);
    else if (k == "oem_owned_sha") {
      if (IsSha256Hex(v)) s.oem.ownedSha = v;
    } else if (k == "oem_owned_watts") {
      s.oem.ownedWatts = WattsOrZero(v);
    }
  }
  // Sahiplik kaydı ya bütündür ya yoktur: eksik/bozuk parçalar kanıt SAYILMAZ (fail-safe).
  if (!s.oem.Present()) s.oem.Clear();
  return s;
}

std::string SerializeSettings(const Settings& s) {
  std::ostringstream o;
  o << "# MONSTER GPU POWER ayarlari. Elle duzenlemeyin; yalnizca acik/kapali bayraklari, onaylar ve OEM sahiplik kaniti.\n";
  o << "restore_on_exit=" << (s.restoreOnExit ? 1 : 0) << "\n";
  o << "consent_over_oem=" << (s.consentOverOem ? 1 : 0) << "\n";
  o << "consent_unverified=" << (s.consentUnverified ? 1 : 0) << "\n";
  o << "consent_model=" << (s.consentModel ? 1 : 0) << "\n";
  // OEM sahiplik kanıtı yalnız varsa yazılır.
  if (s.oem.Present()) {
    o << "oem_owned_sha=" << s.oem.ownedSha << "\n";
    o << "oem_owned_watts=" << s.oem.ownedWatts << "\n";
  }
  return o.str();
}

}  // namespace mgp
