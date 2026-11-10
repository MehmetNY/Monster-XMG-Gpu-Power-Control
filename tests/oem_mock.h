// OEM profil platformunun BELLEK İÇİ SAHTESİ (gerçek NTFS / GCUService / ReplaceFileW DEĞİLDİR).
// Amaç: tüm ret/arıza/yarış yollarını (disk dolu, salt okunur, yazma-okuma-değiştirme arızası, araya giren harici yazıcı ...)
// deterministik olarak sınamak. Gerçek Win32 dosya işlemleri Wine altında win/oem_platform_win.cpp ile ayrıca denenir.
#pragma once
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../src/core/oem_names.h"
#include "../src/core/oem_platform.h"

namespace mock {
using namespace mgp;
using namespace mgp::oem;

// Gerçekçi bir OEM profili üretir (Türkçe karakter, iç içe nesneler, dizi). nl: satır sonu ("\n" / "\r\n" / "" = sıkıştırılmış).
inline std::string ProfileText(int target, const char* nl = "\n", bool activated = true, int tgpSwitch = 1, int dyn = 0,
                               const std::string& extraGpu = "") {
  const std::string n = nl;
  const std::string sp = n.empty() ? "" : "  ";
  std::string s;
  s += "{" + n;
  s += sp + "\"Name\": \"Turbo 1 - \xC3\x87\xC4\x9F\xC5\x9F\xC3\xB6\xC3\xBC\xC4\xB1\"," + n;
  s += sp + std::string("\"Activated\": ") + (activated ? "true" : "false") + "," + n;
  s += sp + "\"CPU\": { \"PL1\": 55, \"PL2\": 80, \"Notes\": \"ConfigurableTGPTargetX\" }," + n;
  s += sp + "\"GPU\": {" + n;
  s += sp + sp + "\"ConfigurableTGPSwitch\": " + std::to_string(tgpSwitch) + "," + n;
  s += sp + sp + "\"DynamicBoostSwitch\": " + std::to_string(dyn) + "," + n;
  s += sp + sp + "\"DynamicBoost\": 15," + n;
  s += extraGpu;
  s += sp + sp + "\"ConfigurableTGPTarget\": " + std::to_string(target) + n;
  s += sp + "}," + n;
  s += sp + "\"Fan\": { \"Curve\": [ { \"T\": 40, \"P\": 30 }, { \"T\": 70, \"P\": 60 } ] }" + n;
  s += "}" + n;
  return s;
}
inline std::vector<uint8_t> Bytes(const std::string& s, bool bom = false) {
  std::vector<uint8_t> v;
  if (bom) { v.push_back(0xEF); v.push_back(0xBB); v.push_back(0xBF); }
  v.insert(v.end(), s.begin(), s.end());
  return v;
}
inline std::vector<uint8_t> Profile(int target, const char* nl = "\n", bool bom = false) { return Bytes(ProfileText(target, nl), bom); }
inline std::string MainOptionText(int mode = 2, int idx = 0) {
  return "{\n  \"OperatingMode\": " + std::to_string(mode) + ",\n  \"TurboProfileIndex\": " + std::to_string(idx) + ",\n  \"X\": 1\n}\n";
}

struct OemFake : IOemPlatform {
  struct Entry {
    std::vector<uint8_t> bytes;
    uint64_t id = 0, mtime = 0;
    bool readOnly = false;
    uint32_t links = 1;
  };
  std::map<std::string, Entry> files;
  ServiceInfo svc;
  uint64_t nextId = 100, clock = 1000;
  int stampSeq = 0, idSeq = 0;
  std::string fixedStamp;  // boş değilse StampNow bunu döndürür

  // sayaçlar
  int creates = 0, replaces = 0, removes = 0, reads = 0, writesToActive = 0;
  // arıza enjeksiyonu (1 tabanlı sıra numaraları; 0 = kapalı)
  int failCreateNumber = 0;              // N. CreateNew başarısız (DiskFull)
  std::string failCreateName;            // bu adı içeren CreateNew başarısız
  IoCode failCreateCode = IoCode::DiskFull;
  bool corruptCreateReadback = false;    // CreateNew sessizce bir bayt bozar (okuma-geri doğrulaması yakalamalı)
  std::string corruptName;               // yalnız bu ada (boş = hepsi)
  bool failReplace = false;              // ReplaceFile erişim engeli (salt okunur / izin yok)
  bool replaceDropsActive = false;       // ReplaceFile başarısız ve aktif dosya KAYBOLUR (1177 penceresi)
  int failReadNumberOfProfile = 0;       // aktif profilin N. okuması başarısız
  bool failAllProfileReads = false;      // aktif profilin HER okuması başarısız (erişim engeli)
  bool failRemove = false;
  std::function<void(OemFake&)> beforeReplace;                       // ReplaceFile içinde, işlemden hemen önce
  std::function<void(OemFake&, const std::string&, int)> beforeRead; // (leaf, o leaf'in kaçıncı okuması)
  std::map<std::string, int> readCount;
  bool postReplaceForeignWrite = false;  // ReplaceFile'dan hemen sonra yabancı yazıcı aktif dosyayı değiştirir

  OemFake() {
    svc.running = svc.pathMatches = svc.versionRead = svc.dirSafe = true;
    svc.version = kExpectedGcuVersion;
    Put(kMainOptionLeaf, Bytes(MainOptionText()));
    Put(kProfileLeaf, Profile(140));
  }
  void Put(const std::string& leaf, const std::vector<uint8_t>& b) {
    Entry& e = files[leaf];
    e.bytes = b;
    if (!e.id) e.id = nextId++;
    e.mtime = ++clock;
  }
  void Foreign(const std::vector<uint8_t>& b) { Put(kProfileLeaf, b); }  // harici yazıcı (OEM servisi / Control Center)
  const std::vector<uint8_t>& Active() const { return files.at(kProfileLeaf).bytes; }
  size_t Count() const { return files.size(); }
  int CountWithInfix(const char* infix) const {
    int c = 0;
    for (auto& f : files) if (f.first.find(infix) != std::string::npos) ++c;
    return c;
  }
  int Mutations() const { return creates + replaces + removes + writesToActive; }

  ServiceInfo ProbeService() override { return svc; }

  FileData ReadFile(const std::string& leaf) override {
    FileData d;
    ++reads;
    const int n = ++readCount[leaf];
    if (beforeRead) beforeRead(*this, leaf, n);
    if (!IsKnownLeaf(leaf)) { d.io.code = IoCode::Unsafe; d.io.msg = "leaf not allowed"; return d; }
    if (leaf == kProfileLeaf && (failAllProfileReads || (failReadNumberOfProfile && n == failReadNumberOfProfile))) {
      d.io.code = IoCode::AccessDenied; d.io.sys = 5; d.io.msg = "access denied (injected)"; return d;
    }
    auto it = files.find(leaf);
    if (it == files.end()) { d.io.code = IoCode::NotFound; d.io.sys = 2; d.io.msg = "not found"; return d; }
    if (it->second.links != 1) { d.io.code = IoCode::Unsafe; d.io.msg = "hard-linked file"; return d; }
    d.bytes = it->second.bytes;
    d.stamp.valid = true; d.stamp.id = it->second.id; d.stamp.size = d.bytes.size(); d.stamp.mtime = it->second.mtime; d.stamp.links = it->second.links;
    return d;
  }

  Io CreateNew(const std::string& leaf, const std::vector<uint8_t>& data, bool readOnly) override {
    Io io;
    ++creates;
    if (!IsKnownLeaf(leaf)) { io.code = IoCode::Unsafe; io.msg = "leaf not allowed"; return io; }
    if ((failCreateNumber && creates == failCreateNumber) || (!failCreateName.empty() && leaf.find(failCreateName) != std::string::npos)) {
      io.code = failCreateCode; io.sys = 112; io.msg = "create failed (injected)"; return io;
    }
    if (files.count(leaf)) { io.code = IoCode::Exists; io.sys = 80; io.msg = "file exists"; return io; }
    Entry e; e.bytes = data; e.id = nextId++; e.mtime = ++clock; e.readOnly = readOnly;
    if (corruptCreateReadback && !e.bytes.empty() && (corruptName.empty() || leaf.find(corruptName) != std::string::npos)) e.bytes[e.bytes.size() / 2] ^= 0x01;
    files[leaf] = e;
    if (leaf == kProfileLeaf) ++writesToActive;
    return io;
  }

  Io AtomicReplace(const std::string& active, const std::string& replacement, const std::string& swapBackup) override {
    Io io;
    ++replaces;
    if (beforeReplace) beforeReplace(*this);
    if (!IsKnownLeaf(active) || !IsKnownLeaf(replacement) || !IsKnownLeaf(swapBackup) || swapBackup.empty()) { io.code = IoCode::Unsafe; io.msg = "leaf not allowed"; return io; }
    if (failReplace) { io.code = IoCode::AccessDenied; io.sys = 5; io.msg = "access denied (injected)"; return io; }
    if (replaceDropsActive) {
      files.erase(active);
      io.code = IoCode::Other; io.sys = 1177; io.msg = "replacement could not be renamed (injected); active file missing"; return io;
    }
    if (files.count(swapBackup)) { io.code = IoCode::Exists; io.sys = 80; io.msg = "swap exists"; return io; }
    auto a = files.find(active), r = files.find(replacement);
    if (a == files.end() || r == files.end()) { io.code = IoCode::NotFound; io.sys = 2; io.msg = "file not found"; return io; }
    if (a->second.readOnly) { io.code = IoCode::AccessDenied; io.sys = 5; io.msg = "target is read-only"; return io; }
    Entry old = a->second;
    Entry fresh = r->second;
    fresh.mtime = ++clock;
    files.erase(r);
    files[swapBackup] = old;
    files[active] = fresh;
    ++writesToActive;
    if (postReplaceForeignWrite) { Put(active, Profile(125)); }
    return io;
  }

  Io RemoveOwn(const std::string& leaf) override {
    Io io;
    ++removes;
    if (!IsOwnTempName(leaf)) { io.code = IoCode::Unsafe; io.msg = "not an own temp name"; return io; }
    if (failRemove) { io.code = IoCode::AccessDenied; io.sys = 5; io.msg = "remove denied (injected)"; return io; }
    if (!files.erase(leaf)) { io.code = IoCode::NotFound; io.sys = 2; io.msg = "not found"; }
    return io;
  }

  std::string StampNow() override {
    if (!fixedStamp.empty()) return fixedStamp;
    char b[32];
    std::snprintf(b, sizeof b, "20260101-1200%02d", stampSeq++ % 30);
    return b;
  }
  std::string RandomId8() override {
    char b[16];
    std::snprintf(b, sizeof b, "%08x", 0x1a2b3c48u + static_cast<unsigned>(idSeq++));
    return b;
  }
};

}  // namespace mock
