#include "file_logger.h"

#include <cstdio>

namespace mgp {
namespace win {

static const unsigned kMaxLogBytes = 1u << 20;

FileLogger::FileLogger() { InitializeCriticalSection(&cs_); }
FileLogger::~FileLogger() { DeleteCriticalSection(&cs_); }

std::wstring FileLogger::CurrentLogPath() { return LogDir() + L"\\mgp.log"; }

void FileLogger::Write(LogLevel level, const std::string& message) {
  static const char* kNames[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};
  const char* lv = kNames[static_cast<int>(level) & 3];
  char head[128];
  // Yerel saat + UTC birlikte: günlük satırlarını başka kaynaklarla (Olay Görüntüleyicisi vb.) eşlemek için.
  std::snprintf(head, sizeof head, "%s (UTC %s) [%s] [%lu] ", NowStamp().c_str(), NowStampUtc().c_str(), lv,
                static_cast<unsigned long>(GetCurrentProcessId()));
  std::string line = head;
  // Satır sonu enjeksiyonunu engelle.
  for (char c : message) line += (c == '\r' || c == '\n') ? ' ' : c;
  line += "\r\n";

  EnterCriticalSection(&cs_);
  const std::wstring path = CurrentLogPath();
  WIN32_FILE_ATTRIBUTE_DATA fad;
  if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad) && fad.nFileSizeLow > kMaxLogBytes) {
    const std::wstring b1 = LogDir() + L"\\mgp.1.log", b2 = LogDir() + L"\\mgp.2.log";
    MoveFileExW(b1.c_str(), b2.c_str(), MOVEFILE_REPLACE_EXISTING);
    MoveFileExW(path.c_str(), b1.c_str(), MOVEFILE_REPLACE_EXISTING);
  }
  HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD wr = 0;
    ::WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &wr, nullptr);
    CloseHandle(h);
  }
  LeaveCriticalSection(&cs_);
}

}  // namespace win
}  // namespace mgp
