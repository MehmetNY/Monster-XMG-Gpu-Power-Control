// YALNIZ MGP_DEMO build'inde derlenir: arayüz önizleme/ekran görüntüsü için SAHTE donanım.
// Bu adaptörlerden gelen hiçbir değer gerçek GM5TG7Y doğrulaması DEĞİLDİR.
#pragma once
#ifdef MGP_DEMO
#include <cstdlib>
#include <cstring>

#include "../../tests/mocks.h"
#include "win_util.h"

namespace mgp {
namespace win {

class DemoNv : public mock::Nv {
 public:
  using mock::Nv::Nv;
  // Yük simülasyonu: çekiş/sıcaklık/kullanım değerleri zamana göre değişir (arayüz önizlemesi için).
  bool ReadDynamic(NvDynamic& o) override {
    bool ok = mock::Nv::ReadDynamic(o);
    if (!ok) return ok;
    const unsigned long long s = GetTickCount64() / 1000ull;
    const unsigned phase = static_cast<unsigned>(s % 24);
    if (phase < 14) {  // yük simülasyonu
      o.utilPct = 100;
      o.drawW = o.limitW - 0.3 + ((s * 7) % 10) / 10.0 * 0.6;
      o.tempC = 52 + static_cast<int>(phase);
    } else {
      o.utilPct = 2;
      o.drawW = 18.2 + (s % 5) * 0.2;
      o.tempC = 66 - static_cast<int>(phase - 14) * 2;
    }
    return ok;
  }
};

// Başlangıç durumu ortam değişkenleriyle seçilir (yalnız demo):
//   MGP_DEMO_INIT  : "off" (EC OEM kapalı, 125 W) | "0".."25" (EC ofseti; 125 + ofset W). Varsayılan: 140 W.
//   MGP_DEMO_FAULT : nofollow | driver | model | nvml | writefail (hata enjeksiyonu).
struct DemoRig {
  mock::Ec ec;
  DemoNv nv;
  mock::Smbios smb;
  DemoRig() : nv(ec) {
    const char* init = std::getenv("MGP_DEMO_INIT");
    if (init && std::strcmp(init, "off") == 0) { ec.SetOemOff(); nv.limit = 125; }
    else if (init && *init) { int off = std::atoi(init); ec.SetOemOn(off); nv.limit = 125.0 + off; }
    else { ec.SetOemOn(15); nv.limit = 140; }
    nv.lagSamples = 3;
    smb.raw = mock::GoodSmbios();
    const char* f = std::getenv("MGP_DEMO_FAULT");
    if (f) {
      if (!std::strcmp(f, "nofollow")) nv.followEc = false;
      else if (!std::strcmp(f, "driver")) { ec.state = DriverState::Absent; ec.winErr = 2; }
      else if (!std::strcmp(f, "model")) smb.raw = mock::BuildSmbios("MONSTER", "TULPAR T7 V20.3", "N.1.07Mon08", "X");
      else if (!std::strcmp(f, "nvml")) nv.initOk = false;
      else if (!std::strcmp(f, "writefail")) ec.failWriteNumber = 2;
    }
  }
};

}  // namespace win
}  // namespace mgp
#endif
