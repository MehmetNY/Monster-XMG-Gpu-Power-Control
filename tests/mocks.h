// Test çifti (mock) sınıfları: SAHTE EC, SAHTE NVML, SAHTE saat. Bunlar gerçek donanım doğrulaması DEĞİLDİR.
#pragma once
#include <cmath>
#include <cstring>
#include <functional>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "../src/core/ec_constants.h"
#include "../src/core/interfaces.h"

namespace mock {
using namespace mgp;

struct Clock : IClock {
  uint64_t now = 1000;
  uint64_t NowMs() override { return now; }
  void SleepMs(uint32_t ms) override { now += ms; }
};

struct MemLog : ILog {
  std::vector<std::string> lines;
  void Write(LogLevel, const std::string& m) override { lines.push_back(m); }
  bool Has(const std::string& needle) const {
    for (auto& l : lines)
      if (l.find(needle) != std::string::npos) return true;
    return false;
  }
};

struct Ec : IEcDriver {
  uint8_t mem[0x1000];
  DriverState state = DriverState::Connected;
  uint32_t winErr = 0;
  int probes = 0, reads = 0, writes = 0;
  std::vector<std::pair<uint16_t, uint8_t>> writeLog;
  std::vector<uint16_t> readLog;
  bool failAllReads = false;
  bool echoAddress = false;      // IOCTL "başarısız" gibi: değer yerine adresin düşük baytını döndür
  bool dropWrites = false;       // yazma "başarılı" döner ama etkisizdir
  int failWriteNumber = -1;      // N. yazma (1 tabanlı) hata döner
  bool failAllWritesAfterFirst = false;
  bool failAllReadsAfterWrites = false;  // ilk yazmadan sonra okumalar çöker (kritik senaryo)
  std::function<void(Ec&)> afterWrite;   // dış müdahale simülasyonu
  std::function<void(Ec&)> beforeRead;   // okumadan hemen önce çağrılır (reads zaten artmış); okuma-yazma arası yarış simülasyonu

  Ec() { std::memset(mem, 0, sizeof mem); SetOemOn(15); }
  void SetOemOn(int off) { mem[0x743] = 0x05; mem[0x744] = (uint8_t)off; mem[0x745] = 0; mem[0x746] = 0; }
  void SetOemOff() { mem[0x743] = 0x01; mem[0x744] = 0; mem[0x745] = 0; mem[0x746] = 0; }

  DriverState Probe(uint32_t* w) override { ++probes; if (w) *w = winErr; return state; }
  EcByte Read(uint16_t a) override {
    ++reads; readLog.push_back(a);
    if (beforeRead) beforeRead(*this);
    EcByte b;
    if (failAllReads || (failAllReadsAfterWrites && writes > 0)) { b.error = "mock read failure"; return b; }
    b.ok = true; b.bytesReturned = 1;
    b.value = echoAddress ? (uint8_t)(a & 0xFF) : mem[a & 0xFFF];
    return b;
  }
  EcByte Write(uint16_t a, uint8_t v) override {
    ++writes; writeLog.push_back({a, v});
    EcByte b;
    if (failWriteNumber == writes || (failAllWritesAfterFirst && writes > 1)) { b.error = "mock write failure"; return b; }
    b.ok = true;
    if (!dropWrites) mem[a & 0xFFF] = v;
    if (afterWrite) afterWrite(*this);
    return b;
  }
};

struct Nv : INvidiaTelemetry {
  Ec& ec;
  std::string name = "NVIDIA GeForce RTX 3070 Laptop GPU";
  double maxW = 150, minW = 1, defW = 125;
  bool initOk = true, staticOk = true, dynOk = true;
  bool followEc = true;
  int lagSamples = 0;
  double limit = 140;
  double draw = 18.0; int temp = 40; int util = 2;
  bool haveTempSensor = true;
  int throwAtDynCall = -1;        // N. ReadDynamic çağrısında std::bad_alloc fırlatır (tek sefer)
  bool throwAlwaysAfterFirst = false;  // ilk çağrıdan sonra her çağrıda fırlatır
  int dynCalls = 0;
  uint32_t seenWrites = 0; int countdown = 0;
  // Ekran uyanışı senaryoları: NVIDIA tarafı EC'yi izlemeyip eski/sıfırlanmış bir değerde takılı kalır (B sınıfı).
  double stuckLimit = -1;          // >=0: EC'den bağımsız bu değer okunur
  bool clearStuckOnInit = false;   // NVML tanıtıcısı yeniden edinilince (Init) düzelir
  bool nudgeFixes = false;         // EC'ye herhangi bir yazma yapılınca düzelir (EC bildirimi NVIDIA'yı uyandırır)
  int inits = 0, shutdowns = 0;
  explicit Nv(Ec& e) : ec(e) { limit = Derive(); }
  double Derive() const { return (ec.mem[0x743] & 0x04) ? 125.0 + ec.mem[0x744] : 125.0; }
  bool Init(std::string& err) override {
    ++inits;
    if (!initOk) { err = "nvml.dll not found"; return false; }
    if (clearStuckOnInit) stuckLimit = -1;
    return true;
  }
  bool ReadStatic(NvStatic& o, std::string& err) override {
    if (!staticOk) { err = "no device"; return false; }
    o.ok = true; o.name = name; o.maxW = maxW; o.minW = minW; o.defaultW = defW; o.pciDeviceId = 0x249D; o.busId = "00000000:01:00.0";
    return true;
  }
  bool ReadDynamic(NvDynamic& o) override {
    ++dynCalls;
    if (dynCalls == throwAtDynCall || (throwAlwaysAfterFirst && dynCalls > 1)) throw std::bad_alloc();
    if (!dynOk) { o.ok = false; o.error = "mock NVML failure"; return false; }
    if (followEc) {
      if ((uint32_t)ec.writes != seenWrites) { seenWrites = (uint32_t)ec.writes; countdown = lagSamples; if (nudgeFixes) stuckLimit = -1; }
      if (countdown > 0) --countdown; else limit = (stuckLimit >= 0) ? stuckLimit : Derive();
    }
    o.ok = true; o.haveMgmt = o.haveEnforced = true; o.mgmtW = o.enforcedW = o.limitW = limit;
    o.haveDraw = o.haveUtil = true; o.haveTemp = haveTempSensor; o.drawW = draw; o.tempC = temp; o.utilPct = util;
    return true;
  }
  void Shutdown() override { ++shutdowns; }
};

struct Smbios : ISmbiosSource {
  std::vector<uint8_t> raw;
  bool ok = true;
  bool ReadRaw(std::vector<uint8_t>& out, std::string& err) override {
    if (!ok) { err = "mock smbios failure"; return false; }
    out = raw; return true;
  }
};

// Sentetik SMBIOS tablosu üretir (Type 0, 1, 2, 127). Seri numarası KASITLI olarak sahte bir değerdir.
inline std::vector<uint8_t> BuildSmbios(const std::string& manuf, const std::string& product, const std::string& bios,
                                        const std::string& board, const std::string& sku = "") {
  std::vector<uint8_t> t;
  auto addStruct = [&](std::vector<uint8_t> formatted, std::vector<std::string> strs) {
    t.insert(t.end(), formatted.begin(), formatted.end());
    if (strs.empty()) { t.push_back(0); t.push_back(0); return; }
    for (auto& s : strs) { t.insert(t.end(), s.begin(), s.end()); t.push_back(0); }
    t.push_back(0);
  };
  { std::vector<uint8_t> f(0x18, 0); f[0] = 0; f[1] = 0x18; f[4] = 1; f[5] = 2; f[8] = 3; f[0x14] = 1; f[0x15] = 7; f[0x16] = 1; f[0x17] = 7;
    addStruct(f, {"INSYDE", bios, "12/13/2021"}); }
  { std::vector<uint8_t> f(0x1B, 0); f[0] = 1; f[1] = 0x1B; f[4] = 1; f[5] = 2; f[6] = 3; f[7] = 4; f[0x19] = 5; f[0x1A] = 6;
    addStruct(f, {manuf, product, "Not Applicable", "SERIAL-DO-NOT-LEAK-123", sku.empty() ? "Not Applicable" : sku, "Tulpar"}); }
  { std::vector<uint8_t> f(0x08, 0); f[0] = 2; f[1] = 0x08; f[4] = 1; f[5] = 2; f[6] = 3; f[7] = 4;
    addStruct(f, {"Tongfang", board, "Not Applicable", "BOARD-SERIAL-DO-NOT-LEAK"}); }
  { std::vector<uint8_t> f(4, 0); f[0] = 127; f[1] = 4; addStruct(f, {}); }
  std::vector<uint8_t> raw(8, 0);
  raw[1] = 3; raw[2] = 3;
  uint32_t n = (uint32_t)t.size();
  raw[4] = n & 0xFF; raw[5] = (n >> 8) & 0xFF; raw[6] = (n >> 16) & 0xFF; raw[7] = (n >> 24) & 0xFF;
  raw.insert(raw.end(), t.begin(), t.end());
  return raw;
}

inline std::vector<uint8_t> GoodSmbios() { return BuildSmbios("MONSTER", "TULPAR T5 V21.7", "N.1.07Mon08", "GM5TG7Y"); }

}  // namespace mock
