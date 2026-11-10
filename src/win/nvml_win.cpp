#include "nvml_win.h"

#include <shlobj.h>

#include <cstdio>
#include <cstring>

#include "../core/ec_constants.h"
#include "../core/hardware_identity_guard.h"

namespace mgp {
namespace win {
namespace {

typedef int nvmlReturn_t;       // 0 = NVML_SUCCESS
typedef void* nvmlDevice_t;
struct nvmlUtilization_t { unsigned int gpu; unsigned int memory; };

typedef nvmlReturn_t (*PFN_Init)();
typedef nvmlReturn_t (*PFN_Shutdown)();
typedef nvmlReturn_t (*PFN_GetCount)(unsigned int*);
typedef nvmlReturn_t (*PFN_GetHandle)(unsigned int, nvmlDevice_t*);
typedef nvmlReturn_t (*PFN_GetName)(nvmlDevice_t, char*, unsigned int);
typedef nvmlReturn_t (*PFN_GetUInt)(nvmlDevice_t, unsigned int*);
typedef nvmlReturn_t (*PFN_GetTemp)(nvmlDevice_t, int, unsigned int*);
typedef nvmlReturn_t (*PFN_GetUtil)(nvmlDevice_t, nvmlUtilization_t*);
typedef nvmlReturn_t (*PFN_GetConstraints)(nvmlDevice_t, unsigned int*, unsigned int*);
typedef nvmlReturn_t (*PFN_GetPci)(nvmlDevice_t, void*);
typedef const char* (*PFN_ErrStr)(nvmlReturn_t);

std::string Hex(unsigned v) {
  char b[16];
  std::snprintf(b, sizeof b, "%u", v);
  return b;
}

}  // namespace

struct NvmlTelemetry::Fns {
  PFN_Init init = nullptr;
  PFN_Shutdown shutdown = nullptr;
  PFN_GetCount count = nullptr;
  PFN_GetHandle handle = nullptr;
  PFN_GetName name = nullptr;
  PFN_GetUInt powerUsage = nullptr;
  PFN_GetTemp temp = nullptr;
  PFN_GetUtil util = nullptr;
  PFN_GetUInt mgmtLimit = nullptr;
  PFN_GetUInt enforcedLimit = nullptr;
  PFN_GetConstraints constraints = nullptr;
  PFN_GetUInt defaultLimit = nullptr;
  PFN_GetPci pci = nullptr;
  PFN_ErrStr errStr = nullptr;
};

NvmlTelemetry::NvmlTelemetry() {}
NvmlTelemetry::~NvmlTelemetry() { Shutdown(); }

static HMODULE LoadNvmlFromTrustedPaths() {
  // Çalışma dizini/PATH aranmaz (DLL ele geçirme riski); yalnız sistem klasörü ve NVIDIA'nın kurulum klasörü.
  wchar_t sys[MAX_PATH];
  UINT n = GetSystemDirectoryW(sys, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    std::wstring p = std::wstring(sys) + L"\\nvml.dll";
    if (FileExists(p)) {
      HMODULE m = LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (m) return m;
    }
  }
  PWSTR pf = nullptr;
  std::wstring p;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, &pf)) && pf)
    p = std::wstring(pf) + L"\\NVIDIA Corporation\\NVSMI\\nvml.dll";
  if (pf) CoTaskMemFree(pf);  // başarısız olsa da dönen işaretçi serbest bırakılır (belge)
  if (!p.empty() && FileExists(p)) {
    HMODULE m = LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (m) return m;
  }
  return nullptr;
}

template <typename T>
static T Sym(HMODULE m, const char* a, const char* b = nullptr) {
  FARPROC p = GetProcAddress(m, a);
  if (!p && b) p = GetProcAddress(m, b);
  return reinterpret_cast<T>(reinterpret_cast<void*>(p));
}

bool NvmlTelemetry::Init(std::string& err) {
  if (inited_) return true;
  if (!f_) f_ = new Fns();
  if (!mod_) {
    mod_ = LoadNvmlFromTrustedPaths();
    if (!mod_) {
      err = "nvml.dll not found (NVIDIA driver not installed or GPU driver not loaded)";
      return false;
    }
    f_->init = Sym<PFN_Init>(mod_, "nvmlInit_v2", "nvmlInit");
    f_->shutdown = Sym<PFN_Shutdown>(mod_, "nvmlShutdown");
    f_->count = Sym<PFN_GetCount>(mod_, "nvmlDeviceGetCount_v2", "nvmlDeviceGetCount");
    f_->handle = Sym<PFN_GetHandle>(mod_, "nvmlDeviceGetHandleByIndex_v2", "nvmlDeviceGetHandleByIndex");
    f_->name = Sym<PFN_GetName>(mod_, "nvmlDeviceGetName");
    f_->powerUsage = Sym<PFN_GetUInt>(mod_, "nvmlDeviceGetPowerUsage");
    f_->temp = Sym<PFN_GetTemp>(mod_, "nvmlDeviceGetTemperature");
    f_->util = Sym<PFN_GetUtil>(mod_, "nvmlDeviceGetUtilizationRates");
    f_->mgmtLimit = Sym<PFN_GetUInt>(mod_, "nvmlDeviceGetPowerManagementLimit");
    f_->enforcedLimit = Sym<PFN_GetUInt>(mod_, "nvmlDeviceGetEnforcedPowerLimit");
    f_->constraints = Sym<PFN_GetConstraints>(mod_, "nvmlDeviceGetPowerManagementLimitConstraints");
    f_->defaultLimit = Sym<PFN_GetUInt>(mod_, "nvmlDeviceGetPowerManagementDefaultLimit");
    f_->pci = Sym<PFN_GetPci>(mod_, "nvmlDeviceGetPciInfo_v3", "nvmlDeviceGetPciInfo_v2");
    f_->errStr = Sym<PFN_ErrStr>(mod_, "nvmlErrorString");
  }
  if (!f_->init || !f_->shutdown || !f_->count || !f_->handle || !f_->name) {
    err = "nvml.dll is missing required exports";
    FreeLibrary(mod_);
    mod_ = nullptr;
    return false;
  }
  nvmlReturn_t rc = f_->init();
  if (rc != 0) {
    err = std::string("nvmlInit failed: ") + (f_->errStr ? f_->errStr(rc) : Hex(static_cast<unsigned>(rc)).c_str());
    return false;
  }
  inited_ = true;

  unsigned int cnt = 0;
  rc = f_->count(&cnt);
  if (rc != 0 || cnt == 0) {
    err = "NVML reports no NVIDIA GPU";
    f_->shutdown();
    inited_ = false;
    return false;
  }
  // RTX 3070 Laptop'ı seç; yoksa 0. cihaz (kimlik koruması uyuşmazlığı zaten yazmayı engeller).
  nvmlDevice_t chosen = nullptr;
  for (unsigned i = 0; i < cnt; ++i) {
    nvmlDevice_t d = nullptr;
    if (f_->handle(i, &d) != 0 || !d) continue;
    char nm[96] = {0};
    f_->name(d, nm, sizeof nm);
    if (!chosen) chosen = d;
    if (ContainsNoCase(nm, kExpectedGpuNameA) && ContainsNoCase(nm, kExpectedGpuNameB)) {
      chosen = d;
      break;
    }
  }
  if (!chosen) {
    err = "NVML device handle unavailable";
    f_->shutdown();
    inited_ = false;
    return false;
  }
  dev_ = chosen;
  return true;
}

bool NvmlTelemetry::ReadStatic(NvStatic& out, std::string& err) {
  if (!inited_ || !dev_) {
    err = "NVML not initialised";
    return false;
  }
  out = NvStatic();
  char nm[96] = {0};
  if (f_->name(dev_, nm, sizeof nm) != 0) {
    err = "nvmlDeviceGetName failed";
    return false;
  }
  out.name = nm;
  unsigned mn = 0, mx = 0, df = 0;
  if (f_->constraints && f_->constraints(dev_, &mn, &mx) == 0) {
    out.minW = mn / 1000.0;
    out.maxW = mx / 1000.0;
  } else {
    err = "power limit constraints (min/max) unavailable";
    return false;
  }
  if (f_->defaultLimit && f_->defaultLimit(dev_, &df) == 0) out.defaultW = df / 1000.0;
  if (f_->pci) {
    // nvmlPciInfo_t: busIdLegacy[16], domain, bus, device, pciDeviceId, pciSubSystemId, busId[32].
    // Taşmayı önlemek için bol tampon; yalnız günlük amaçlı.
    unsigned char pci[256];
    std::memset(pci, 0, sizeof pci);
    if (f_->pci(dev_, pci) == 0) {
      std::memcpy(&out.pciDeviceId, pci + 28, 4);
      char bus[33] = {0};
      std::memcpy(bus, pci + 36, 32);
      out.busId = bus;
    }
  }
  out.ok = true;
  return true;
}

bool NvmlTelemetry::ReadDynamic(NvDynamic& o) {
  o = NvDynamic();
  if (!inited_ || !dev_) {
    o.error = "NVML not initialised";
    return false;
  }
  unsigned v = 0;
  if (f_->mgmtLimit) {
    o.mgmtRc = f_->mgmtLimit(dev_, &v);  // 0 = NVML_SUCCESS; 15 = GPU_IS_LOST, 3 = NOT_SUPPORTED, 999 = UNKNOWN ...
    if (o.mgmtRc == 0) {
      o.haveMgmt = true;
      o.mgmtW = v / 1000.0;
    }
  }
  if (f_->enforcedLimit) {
    o.enforcedRc = f_->enforcedLimit(dev_, &v);
    if (o.enforcedRc == 0) {
      o.haveEnforced = true;
      o.enforcedW = v / 1000.0;
    }
  }
  if (o.haveMgmt) o.limitW = o.mgmtW;
  else if (o.haveEnforced) o.limitW = o.enforcedW;
  else {
    o.error = std::string("neither management nor enforced power limit readable") +
              (f_->errStr && o.mgmtRc ? std::string(" (") + f_->errStr(o.mgmtRc) + ")" : std::string());
    return false;
  }
  o.ok = true;
  if (f_->powerUsage && f_->powerUsage(dev_, &v) == 0) {
    o.haveDraw = true;
    o.drawW = v / 1000.0;
  }
  if (f_->temp && f_->temp(dev_, 0 /*NVML_TEMPERATURE_GPU*/, &v) == 0) {
    o.haveTemp = true;
    o.tempC = static_cast<int>(v);
  }
  nvmlUtilization_t u = {0, 0};
  if (f_->util && f_->util(dev_, &u) == 0) {
    o.haveUtil = true;
    o.utilPct = static_cast<int>(u.gpu);
  }
  return true;
}

void NvmlTelemetry::Shutdown() {
  if (inited_ && f_ && f_->shutdown) f_->shutdown();
  inited_ = false;
  dev_ = nullptr;
  if (mod_) {
    FreeLibrary(mod_);
    mod_ = nullptr;
  }
  delete f_;
  f_ = nullptr;
}

}  // namespace win
}  // namespace mgp
