// Windows gerçeklemesi: OEM GCUService'in mevcut profil dosyasına (Mode3_Profile1.json) GÜVENLİ erişim.
//
// Bu katman EC/BIOS/sürücü/kayıt defteri/AdpType/230 W adaptör profiline ASLA dokunmaz. Yalnız:
//   * çalışan GCUService.exe'yi bulur ve TAM yolunu/sürümünü doğrular,
//   * bu yoldan türetilen "UserPofiles" dizinini doğrular (bağlantı/reparse/kanonik yol),
//   * o dizindeki yalnız izinli düz dosya adlarını okur / CREATE_NEW ile yeni dosya açar / ReplaceFileW ile değiştirir.
// Kullanıcı yol girdisi YOKTUR. Tüm çağrılar worker iş parçacığından yapılır (nesne iş parçacığı güvenli değildir).
#pragma once
#include <string>
#include <vector>

#include "../core/oem_platform.h"
#include "win_util.h"

namespace mgp {
namespace win {

// OEM dosyası için çapraz-süreç kilit. Her zaman Apply kilidinin (kApplyLockName) İÇİNDE alınır: sıra Apply -> OEM.
inline constexpr wchar_t kOemLockName[] = L"Global\\MonsterGpuPower.OemProfileLock";

class WinOemPlatform : public oem::IOemPlatform {
 public:
  oem::ServiceInfo ProbeService() override;
  oem::FileData ReadFile(const std::string& leaf) override;
  oem::Io CreateNew(const std::string& leaf, const std::vector<uint8_t>& data, bool readOnly) override;
  oem::Io AtomicReplace(const std::string& active, const std::string& replacement, const std::string& swapBackup) override;
  oem::Io RemoveOwn(const std::string& leaf) override;
  std::string StampNow() override;
  std::string RandomId8() override;

 private:
  bool PathFor(const std::string& leaf, std::wstring& out, oem::Io& io) const;
  std::wstring dir_;  // son ProbeService'in doğruladığı UserPofiles dizini (doğrulanamadıysa boş: hiçbir dosya işlemi yapılmaz)
};

}  // namespace win
}  // namespace mgp
