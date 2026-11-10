// HardwareIdentityGuard: yalnız bilinen tek hedef (Monster TULPAR T5 V21.7 + BIOS N.1.07Mon08)
// için EC erişimine izin verir. Başka sistemde EC'ye HİÇ dokunulmaz (okuma dahil).
#pragma once
#include <string>

#include "smbios.h"

namespace mgp {

enum class IdentityVerdict {
  Exact,                 // üretici + model + BIOS birebir eşleşti
  Unreadable,            // SMBIOS okunamadı / çözümlenemedi
  ManufacturerMismatch,
  ModelMismatch,
  BiosMismatch
};

struct IdentityResult {
  IdentityVerdict verdict = IdentityVerdict::Unreadable;
  bool boardConfirmed = false;  // SMBIOS alanlarında GM5TG7Y geçiyor mu (zorunlu değil)
  SmbiosInfo info;
  std::string summary;          // günlük için kısa, kişisel veri içermeyen özet
};

IdentityResult EvaluateIdentity(const SmbiosInfo& info, bool smbiosReadOk);

// Yardımcılar (testler için açık)
bool EqualsNoCase(const std::string& a, const std::string& b);
bool StartsWithNoCase(const std::string& s, const std::string& prefix);
bool ContainsNoCase(const std::string& s, const std::string& needle);

}  // namespace mgp
