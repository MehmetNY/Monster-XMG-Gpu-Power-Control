#include "smbios_win.h"

namespace mgp {
namespace win {

bool WinSmbiosSource::ReadRaw(std::vector<uint8_t>& out, std::string& err) {
  const DWORD kSigRSMB = 0x52534D42;  // 'RSMB'
  UINT n = GetSystemFirmwareTable(kSigRSMB, 0, nullptr, 0);
  if (n == 0 || n > (4u << 20)) {
    err = "GetSystemFirmwareTable(RSMB) size query failed";
    return false;
  }
  out.assign(n, 0);
  UINT got = GetSystemFirmwareTable(kSigRSMB, 0, out.data(), n);
  if (got == 0 || got > n) {
    err = "GetSystemFirmwareTable(RSMB) read failed";
    out.clear();
    return false;
  }
  out.resize(got);
  return true;
}

}  // namespace win
}  // namespace mgp
