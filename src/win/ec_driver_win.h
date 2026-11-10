// OEM Uniwill "ACPIDriver" çekirdek aygıtına doğrudan DeviceIoControl ile EC okuma/yazma.
// OEM ACPIDriverDll.dll / Control Center / GCUService GEREKMEZ ve yüklenmez.
// ABI, ACPIDriverDll.dll içindeki ReadEC/WriteEC makine kodundan çıkarıldı (docs/SURUCU_ABI.md).
#pragma once
#include "../core/interfaces.h"
#include "win_util.h"

namespace mgp {
namespace win {

class AcpiDriverEc : public IEcDriver {
 public:
  DriverState Probe(uint32_t* winError) override;
  EcByte Read(uint16_t addr) override;
  EcByte Write(uint16_t addr, uint8_t value) override;

 private:
  EcByte Transact(uint32_t ioctl, uint16_t addr, uint8_t value, bool isWrite);
};

}  // namespace win
}  // namespace mgp
