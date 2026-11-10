// Dosya günlüğü: %LOCALAPPDATA%\MonsterGpuPower\logs\mgp.log (1 MiB'ta döner, 2 yedek).
// Kişisel veri YAZILMAZ: seri numarası, MAC, kullanıcı adı, yol (kullanıcı profili) günlüğe girmez.
#pragma once
#include "../core/interfaces.h"
#include "win_util.h"

namespace mgp {
namespace win {

class FileLogger : public ILog {
 public:
  FileLogger();
  ~FileLogger() override;
  void Write(LogLevel level, const std::string& message) override;
  static std::wstring CurrentLogPath();

 private:
  CRITICAL_SECTION cs_;
};

}  // namespace win
}  // namespace mgp
