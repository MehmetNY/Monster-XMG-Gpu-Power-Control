// Donanım/OS soyutlamaları. Çekirdek mantık yalnız bu arayüzlere bağlıdır (windows.h yok),
// bu yüzden sahte (mock) EC/NVML ile Linux'ta bile birim testi yapılabilir.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mgp {

enum class LogLevel { Debug, Info, Warn, Error };

class ILog {
 public:
  virtual ~ILog() = default;
  virtual void Write(LogLevel level, const std::string& message) = 0;
};

class IClock {
 public:
  virtual ~IClock() = default;
  virtual uint64_t NowMs() = 0;
  virtual void SleepMs(uint32_t ms) = 0;
};

// ---------------------------------------------------------------- EC / sürücü
enum class DriverState { Connected, Absent, AccessDenied, Error };

struct EcByte {
  bool ok = false;
  uint8_t value = 0;
  uint32_t bytesReturned = 0;
  uint32_t winError = 0;
  std::string error;
};

class IEcDriver {
 public:
  virtual ~IEcDriver() = default;
  // Aygıt tanıtıcısını açmayı dener (IOCTL göndermez).
  virtual DriverState Probe(uint32_t* winError) = 0;
  virtual EcByte Read(uint16_t addr) = 0;
  virtual EcByte Write(uint16_t addr, uint8_t value) = 0;
};

// ---------------------------------------------------------------- NVIDIA
struct NvStatic {
  bool ok = false;
  std::string name;
  double minW = 0, maxW = 0, defaultW = 0;
  uint32_t pciDeviceId = 0;
  std::string busId;
};

struct NvDynamic {
  bool ok = false;             // en az "geçerli güç limiti" okunabildi
  bool haveMgmt = false, haveEnforced = false;
  double mgmtW = 0;            // nvmlDeviceGetPowerManagementLimit
  double enforcedW = 0;        // nvmlDeviceGetEnforcedPowerLimit
  double limitW = 0;           // birincil: mgmt (nvidia-smi "Current Power Limit"), yoksa enforced
  bool haveDraw = false, haveTemp = false, haveUtil = false;
  double drawW = 0;
  int tempC = 0;
  int utilPct = 0;
  int mgmtRc = 0;              // NVML dönüş kodu (0 = başarılı); yalnız günlük için
  int enforcedRc = 0;
  std::string error;
};

class INvidiaTelemetry {
 public:
  virtual ~INvidiaTelemetry() = default;
  virtual bool Init(std::string& err) = 0;
  virtual bool ReadStatic(NvStatic& out, std::string& err) = 0;
  virtual bool ReadDynamic(NvDynamic& out) = 0;
  virtual void Shutdown() = 0;
};

// ---------------------------------------------------------------- SMBIOS
class ISmbiosSource {
 public:
  virtual ~ISmbiosSource() = default;
  // Ham SMBIOS tablosu (GetSystemFirmwareTable 'RSMB' çıktısı, 8 baytlık başlık dahil).
  virtual bool ReadRaw(std::vector<uint8_t>& out, std::string& err) = 0;
};

}  // namespace mgp
