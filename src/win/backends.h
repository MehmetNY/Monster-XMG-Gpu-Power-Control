// Gerçek donanım adaptörlerini ve PowerProfileEngine'i bir araya getirir.
// Derleme bayrağı MGP_DEMO yalnız ARAYÜZ ÖNİZLEME build'inde (MonsterGpuPower_DEMO.exe) sahte adaptör kullanır;
// üretim EXE'sinde demo/mock kodu HİÇ derlenmez.
#pragma once
#include <memory>

#include "../core/oem_profile.h"
#include "../core/power_profile_engine.h"
#include "oem_platform_win.h"
#include "win_util.h"

namespace mgp {
namespace win {

class Backends {
 public:
  explicit Backends(ILog& log);
  ~Backends();
  PowerProfileEngine& engine() { return *engine_; }
  IEcDriver& ec() { return *ec_; }
  // OEM profil dosyası (kalıcılık) deposu. PowerProfileEngine'den BAĞIMSIZDIR; nesne iş parçacığı güvenli DEĞİLDİR: YALNIZ
  // Worker iş parçacığından, Apply kilidi (kApplyLockName) ve OEM kilidi (kOemLockName) altında kullanılır.
  oem::OemProfileStore& oemStore() { return *oemStore_; }
  // constexpr: üretim derlemesinde demo metinleri derleme zamanında elenir (EXE'de hiç bulunmaz).
  static constexpr bool IsDemoBuild() {
#ifdef MGP_DEMO
    return true;
#else
    return false;
#endif
  }

 private:
  SystemClock clock_;
  std::unique_ptr<IEcDriver> ec_;
  std::unique_ptr<INvidiaTelemetry> nv_;
  std::unique_ptr<ISmbiosSource> smb_;
  std::unique_ptr<PowerProfileEngine> engine_;
  std::unique_ptr<WinOemPlatform> oemPlatform_;
  std::unique_ptr<oem::OemProfileStore> oemStore_;
};

}  // namespace win
}  // namespace mgp
