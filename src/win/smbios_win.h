#pragma once
#include "../core/interfaces.h"
#include "win_util.h"

namespace mgp {
namespace win {

// GetSystemFirmwareTable('RSMB') ile ham SMBIOS okur. WMI/COM kullanmaz (hafif).
class WinSmbiosSource : public ISmbiosSource {
 public:
  bool ReadRaw(std::vector<uint8_t>& out, std::string& err) override;
};

}  // namespace win
}  // namespace mgp
