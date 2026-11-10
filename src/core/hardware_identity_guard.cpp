#include "hardware_identity_guard.h"

#include <algorithm>
#include <cctype>

#include "ec_constants.h"

namespace mgp {

static std::string Upper(const std::string& s) {
  std::string r = s;
  std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return r;
}

bool EqualsNoCase(const std::string& a, const std::string& b) { return Upper(a) == Upper(b); }

bool StartsWithNoCase(const std::string& s, const std::string& prefix) {
  const std::string us = Upper(s), up = Upper(prefix);
  return us.size() >= up.size() && us.compare(0, up.size(), up) == 0;
}

bool ContainsNoCase(const std::string& s, const std::string& needle) {
  return Upper(s).find(Upper(needle)) != std::string::npos;
}

IdentityResult EvaluateIdentity(const SmbiosInfo& info, bool smbiosReadOk) {
  IdentityResult r;
  r.info = info;
  if (!smbiosReadOk || !info.valid) {
    r.verdict = IdentityVerdict::Unreadable;
    r.summary = "SMBIOS okunamadi";
    return r;
  }
  r.boardConfirmed = ContainsNoCase(info.boardProduct, kExpectedBoardTag) ||
                     ContainsNoCase(info.sysSku, kExpectedBoardTag) ||
                     ContainsNoCase(info.sysFamily, kExpectedBoardTag) ||
                     ContainsNoCase(info.sysVersion, kExpectedBoardTag) ||
                     ContainsNoCase(info.boardVersion, kExpectedBoardTag);
  if (!EqualsNoCase(info.sysManufacturer, kExpectedManufacturer))
    r.verdict = IdentityVerdict::ManufacturerMismatch;
  else if (!StartsWithNoCase(info.sysProduct, kExpectedProductPrefix))
    r.verdict = IdentityVerdict::ModelMismatch;
  else if (!EqualsNoCase(info.biosVersion, kExpectedBiosVersion))
    r.verdict = IdentityVerdict::BiosMismatch;
  else
    r.verdict = IdentityVerdict::Exact;
  // Seri numarası/UUID içermez: yalnız üretici, model, BIOS, kart etiketi.
  r.summary = "uretici='" + info.sysManufacturer + "' model='" + info.sysProduct + "' bios='" +
              info.biosVersion + "' kart='" + info.boardProduct + "' GM5TG7Y_etiketi=" +
              (r.boardConfirmed ? "var" : "yok");
  return r;
}

}  // namespace mgp
