// SMBIOS ayrıştırıcı (saf C++). Seri numarası, UUID, asset tag GİBİ kimlik verilerini OKUMAZ/SAKLAMAZ.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace mgp {

struct SmbiosInfo {
  bool valid = false;  // en az Type 1 bulundu
  std::string sysManufacturer, sysProduct, sysVersion, sysSku, sysFamily;
  std::string biosVendor, biosVersion;
  std::string boardManufacturer, boardProduct, boardVersion;
  int ecMajor = -1, ecMinor = -1;  // Type 0 EC firmware sürümü (yoksa -1)
};

// Ham tablo (başlıksız yapı listesi).
bool ParseSmbiosStructures(const uint8_t* data, size_t len, SmbiosInfo& out);
// GetSystemFirmwareTable('RSMB') çıktısı: 8 baytlık RawSMBIOSData başlığı + tablo.
bool ParseRawSmbios(const uint8_t* raw, size_t len, SmbiosInfo& out);

}  // namespace mgp
