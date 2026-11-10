#include "backends.h"

#ifdef MGP_DEMO
#include "demo_backends.h"
#else
#include "ec_driver_win.h"
#include "nvml_win.h"
#include "smbios_win.h"
#endif

namespace mgp {
namespace win {

#ifdef MGP_DEMO
namespace {
// Demo bileşenleri tek bir DemoRig'e referans verir; Backends ömrü boyunca yaşar.
struct DemoHolder {
  DemoRig rig;
};
// Sahiplik: DemoRig'i heap'te tutup adaptörleri ona bağlayan ince sarmalayıcılar.
class EcRef : public IEcDriver {
 public:
  explicit EcRef(DemoHolder* h) : h_(h) {}
  ~EcRef() override { delete h_; }
  DriverState Probe(uint32_t* w) override { return h_->rig.ec.Probe(w); }
  EcByte Read(uint16_t a) override { return h_->rig.ec.Read(a); }
  EcByte Write(uint16_t a, uint8_t v) override { return h_->rig.ec.Write(a, v); }
  DemoHolder* h_;
};
class NvRef : public INvidiaTelemetry {
 public:
  explicit NvRef(DemoHolder* h) : h_(h) {}
  bool Init(std::string& e) override { return h_->rig.nv.Init(e); }
  bool ReadStatic(NvStatic& o, std::string& e) override { return h_->rig.nv.ReadStatic(o, e); }
  bool ReadDynamic(NvDynamic& o) override { return h_->rig.nv.ReadDynamic(o); }
  void Shutdown() override { h_->rig.nv.Shutdown(); }
  DemoHolder* h_;
};
class SmbRef : public ISmbiosSource {
 public:
  explicit SmbRef(DemoHolder* h) : h_(h) {}
  bool ReadRaw(std::vector<uint8_t>& o, std::string& e) override { return h_->rig.smb.ReadRaw(o, e); }
  DemoHolder* h_;
};
}  // namespace
#endif

Backends::Backends(ILog& log) {
#ifdef MGP_DEMO
  DemoHolder* h = new DemoHolder();
  ec_.reset(new EcRef(h));       // EcRef DemoHolder'ı siler
  nv_.reset(new NvRef(h));
  smb_.reset(new SmbRef(h));
#else
  ec_.reset(new AcpiDriverEc());
  nv_.reset(new NvmlTelemetry());
  smb_.reset(new WinSmbiosSource());
#endif
  engine_.reset(new PowerProfileEngine(*ec_, *nv_, *smb_, clock_, log));
  oemPlatform_.reset(new WinOemPlatform());
  oemStore_.reset(new oem::OemProfileStore(*oemPlatform_, log));
}

Backends::~Backends() {
  oemStore_.reset();
  oemPlatform_.reset();
  if (engine_) engine_->Shutdown();
  engine_.reset();
  nv_.reset();   // demo: NvRef/SmbRef holder'ı silmez; EcRef siler (en son)
  smb_.reset();
  ec_.reset();
}

}  // namespace win
}  // namespace mgp
