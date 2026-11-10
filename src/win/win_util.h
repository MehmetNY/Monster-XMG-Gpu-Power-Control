// Windows yardımcıları: UTF-8/UTF-16, veri klasörü, atomik dosya yazma, zaman damgası, çapraz-süreç kilit.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

#include "../core/interfaces.h"

namespace mgp {
namespace win {

std::wstring Widen(const std::string& utf8);
std::string Narrow(const std::wstring& w);

std::wstring DataDir();  // %LOCALAPPDATA%\MonsterGpuPower (yoksa oluşturur)
std::wstring LogDir();   // DataDir()\logs
std::wstring SelfPath(); // çalışan EXE'nin tam yolu
std::wstring ExeDir();
bool IsTrustedInstallPath(const std::wstring& path);  // Program Files altında mı (yalnız yönetici yazabilir; Windows klasörü bilerek hariç: Temp/Tasks kullanıcı-yazılabilir)
std::wstring SettingsDir();                           // kurulu sürümde kurulum klasörü, aksi hâlde DataDir()
bool IsElevated();
bool FileExists(const std::wstring& path);
std::string NowStamp();     // "YYYY-MM-DD HH:MM:SS.mmm" (yerel saat)
std::string NowStampUtc();  // "YYYY-MM-DD HH:MM:SS.mmm" (UTC); günlükte yerel+UTC birlikte yazılır
std::string FileStamp(); // "YYYYMMDD-HHMMSS"

bool ReadFileAll(const std::wstring& path, std::string& out, size_t maxBytes = (1u << 20));
bool WriteFileAtomic(const std::wstring& path, const std::string& bytes);

// Gerçek saat (IClock)
class SystemClock : public IClock {
 public:
  uint64_t NowMs() override { return GetTickCount64(); }
  void SleepMs(uint32_t ms) override { ::Sleep(ms); }
};

// Çapraz-süreç adlandırılmış mutex (oturum kapsamlı). Apply işlemlerini GUI ve başlangıç görevi arasında serileştirir.
class NamedLock {
 public:
  NamedLock(const wchar_t* name, DWORD timeoutMs);
  ~NamedLock();
  bool acquired() const { return acquired_; }
  // Önceki sahip kilidi bırakmadan öldüyse (yazma sırasında çökmüş olabilir) true; çağıran bunu günlüğe yazar.
  bool abandoned() const { return abandoned_; }
  NamedLock(const NamedLock&) = delete;
  NamedLock& operator=(const NamedLock&) = delete;

 private:
  HANDLE h_ = nullptr;
  bool acquired_ = false;
  bool abandoned_ = false;
};

// EC makine geneli tek kaynaktır: iki oturum (hızlı kullanıcı değiştirme) aynı anda yazmasın diye Global\.
inline constexpr wchar_t kApplyLockName[] = L"Global\\MonsterGpuPower.ApplyLock";

}  // namespace win
}  // namespace mgp
