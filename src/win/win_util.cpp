#include "win_util.h"

#include <cstdlib>

#include <shlobj.h>

#include <cstdio>

namespace mgp {
namespace win {

std::wstring Widen(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return std::wstring();
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
  return w;
}

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return std::string();
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
  return s;
}

std::wstring DataDir() {
  std::wstring base;
  PWSTR p = nullptr;
  const bool got = SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &p)) && p;
  if (got) base = p;
  if (p) CoTaskMemFree(p);  // belgeye göre işlev başarısız olsa da dönen işaretçi çağıran tarafından serbest bırakılır
  if (!got) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) base = buf;
    else base = L".";
  }
  std::wstring dir = base + L"\\MonsterGpuPower";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::wstring LogDir() {
  std::wstring dir = DataDir() + L"\\logs";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::wstring SelfPath() {
  // Yığında 64 KB'lık tampon yerine yığın (heap) üzerinde büyüyen tampon: kısa yollarda 260 karakter yeter, uzun yollarda
  // (en çok 32767 karakter) iki katına çıkarılır. GetModuleFileNameW kesilirse tam tampon boyutunu döndürür.
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    const DWORD n = GetModuleFileNameW(nullptr, &buf[0], static_cast<DWORD>(buf.size()));
    if (n == 0) return std::wstring();
    if (n < buf.size()) {
      buf.resize(n);
      return buf;
    }
    if (buf.size() >= 32768) return std::wstring();
    buf.resize(buf.size() * 2 > 32768 ? 32768 : buf.size() * 2);
  }
}

namespace {
bool UnderDir(const std::wstring& p, const std::wstring& dirIn) {
  std::wstring d = dirIn;
  while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
  if (d.empty() || p.size() <= d.size() + 1) return false;
  if (_wcsnicmp(p.c_str(), d.c_str(), d.size()) != 0) return false;
  return p[d.size()] == L'\\';
}
bool KnownFolderUnder(const std::wstring& path, REFKNOWNFOLDERID id) {
  PWSTR k = nullptr;
  bool r = false;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &k)) && k) r = UnderDir(path, k);
  if (k) CoTaskMemFree(k);  // başarısız olsa da dönen işaretçi serbest bırakılır (belge)
  return r;
}
}  // namespace

// Yalnız yönetici yazabilen yer: Program Files. Ortam değişkenlerine DEĞİL, sistemin bilinen klasörlerine bakılır.
// Windows klasörü BİLEREK dışarıda: altında kullanıcıların dosya oluşturabildiği alt klasörler vardır (Temp, Tasks, tracing ...);
// oradaki bir kopya "güvenilir konum" sayılıp yükseltilmiş bir oturum-açılışı görevine bağlanmamalıdır (bağımsız gözden geçirme bulgusu).
bool IsTrustedInstallPath(const std::wstring& path) {
  if (path.empty() || path.find(L"..") != std::wstring::npos) return false;
  return KnownFolderUnder(path, FOLDERID_ProgramFilesX64) || KnownFolderUnder(path, FOLDERID_ProgramFiles);
}

std::wstring ExeDir() {
  std::wstring p = SelfPath();
  const size_t k = p.find_last_of(L'\\');
  return k == std::wstring::npos ? std::wstring() : p.substr(0, k);
}

std::wstring SettingsDir() {
  // Onaylar/başlangıç ayarı yükseltilmiş bir görev tarafından okunur. Kurulu (yönetici-yazılabilir) sürümde ayar dosyası
  // kurulum klasöründedir; taşınabilir kopyada kullanıcı klasörüdür (orada Windows açılışında uygulama zaten kapalıdır).
  const std::wstring dir = ExeDir();
  if (!dir.empty() && IsTrustedInstallPath(SelfPath())) return dir;
  return DataDir();
}

bool IsElevated() {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
  TOKEN_ELEVATION te = {};
  DWORD len = 0;
  const BOOL ok = GetTokenInformation(tok, TokenElevation, &te, sizeof te, &len);
  CloseHandle(tok);
  return ok && te.TokenIsElevated != 0;
}

bool FileExists(const std::wstring& path) {
  const DWORD a = GetFileAttributesW(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string NowStamp() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char b[48];
  std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d.%03d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                st.wSecond, st.wMilliseconds);
  return b;
}

std::string NowStampUtc() {
  SYSTEMTIME st;
  GetSystemTime(&st);
  char b[48];
  std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d.%03d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                st.wSecond, st.wMilliseconds);
  return b;
}


std::string FileStamp() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char b[32];
  std::snprintf(b, sizeof b, "%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  return b;
}

bool ReadFileAll(const std::wstring& path, std::string& out, size_t maxBytes) {
  out.clear();
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz;
  if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || static_cast<unsigned long long>(sz.QuadPart) > maxBytes) {
    CloseHandle(h);
    return false;
  }
  out.resize(static_cast<size_t>(sz.QuadPart));
  DWORD got = 0;
  bool ok = true;
  if (!out.empty()) ok = ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size();
  CloseHandle(h);
  if (!ok) out.clear();
  return ok;
}

bool WriteFileAtomic(const std::wstring& path, const std::string& bytes) {
  const std::wstring tmp = path + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD wr = 0;
  bool ok = bytes.empty() || (WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &wr, nullptr) && wr == bytes.size());
  ok = ok && FlushFileBuffers(h);
  CloseHandle(h);
  if (!ok) {
    DeleteFileW(tmp.c_str());
    return false;
  }
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(tmp.c_str());
    return false;
  }
  return true;
}

NamedLock::NamedLock(const wchar_t* name, DWORD timeoutMs) {
  h_ = CreateMutexW(nullptr, FALSE, name);
  if (!h_) return;
  const DWORD r = WaitForSingleObject(h_, timeoutMs);
  acquired_ = (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED);
  abandoned_ = (r == WAIT_ABANDONED);
}

NamedLock::~NamedLock() {
  if (h_) {
    if (acquired_) ReleaseMutex(h_);
    CloseHandle(h_);
  }
}

}  // namespace win
}  // namespace mgp
