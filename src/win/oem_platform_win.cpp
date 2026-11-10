#include "oem_platform_win.h"

#include <shlobj.h>
#include <tlhelp32.h>
#include <bcrypt.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>

#include "../core/oem_json.h"
#include "../core/oem_names.h"

namespace mgp {
namespace win {
namespace {

using oem::Io;
using oem::IoCode;

class Handle {
 public:
  explicit Handle(HANDLE x = INVALID_HANDLE_VALUE) : h(x) {}
  ~Handle() { Close(); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  bool ok() const { return h != INVALID_HANDLE_VALUE; }
  void Close() {
    if (h != INVALID_HANDLE_VALUE) {
      CloseHandle(h);
      h = INVALID_HANDLE_VALUE;
    }
  }
  HANDLE h;
};

IoCode MapCode(DWORD e) {
  switch (e) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return IoCode::NotFound;
    case ERROR_ACCESS_DENIED:
    case ERROR_WRITE_PROTECT:
    case ERROR_PRIVILEGE_NOT_HELD:
      return IoCode::AccessDenied;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
      return IoCode::Sharing;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
      return IoCode::DiskFull;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
      return IoCode::Exists;
    default:
      return IoCode::Other;
  }
}

Io Fail(DWORD e, const std::string& what) {
  Io io;
  io.code = MapCode(e);
  io.sys = e;
  io.msg = what + " (Win32 error " + std::to_string(e) + ")";
  return io;
}

Io Unsafe(const std::string& m) {
  Io io;
  io.code = IoCode::Unsafe;
  io.msg = m;
  return io;
}

std::wstring StripExtendedPrefix(const std::wstring& p) {
  if (p.compare(0, 8, L"\\\\?\\UNC\\") == 0) return L"\\" + p.substr(7);
  if (p.compare(0, 4, L"\\\\?\\") == 0) return p.substr(4);
  return p;
}

std::wstring Canon(std::wstring p) {
  for (wchar_t& c : p)
    if (c == L'/') c = L'\\';
  while (p.size() > 3 && p.back() == L'\\') p.pop_back();
  return p;
}

bool PathEq(const std::wstring& a, const std::wstring& b) { return _wcsicmp(Canon(a).c_str(), Canon(b).c_str()) == 0; }

// Açık tanıtıcının GERÇEK (normalize) yolu. Bağlantı/junction/subst üzerinden gelen erişim burada ortaya çıkar.
std::wstring FinalPathOf(HANDLE h) {
  std::vector<wchar_t> buf(1024);
  for (int attempt = 0; attempt < 3; ++attempt) {
    const DWORD n = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n == 0) return std::wstring();
    if (n < buf.size()) return StripExtendedPrefix(std::wstring(buf.data(), n));
    buf.resize(static_cast<size_t>(n) + 1);
  }
  return std::wstring();
}

#ifdef MGP_DEMO
std::wstring EnvW(const wchar_t* name) {
  wchar_t b[4096];
  const DWORD n = GetEnvironmentVariableW(name, b, 4096);
  return (n > 0 && n < 4096) ? std::wstring(b, n) : std::wstring();
}
#endif

std::wstring ProgramFilesRoot() {
  for (REFKNOWNFOLDERID id : {FOLDERID_ProgramFilesX64, FOLDERID_ProgramFiles}) {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)) && p) r = p;
    if (p) CoTaskMemFree(p);
    if (!r.empty()) return r;
  }
  return std::wstring();
}

bool EnumGcuProcesses(std::vector<DWORD>& pids) {
  pids.clear();
  Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (!snap.ok()) return false;
  PROCESSENTRY32W pe;
  pe.dwSize = sizeof pe;
  if (Process32FirstW(snap.h, &pe)) {
    do {
      if (_wcsicmp(pe.szExeFile, L"GCUService.exe") == 0) pids.push_back(pe.th32ProcessID);
    } while (Process32NextW(snap.h, &pe));
  }
  return true;
}

bool ImagePathOf(DWORD pid, std::wstring& out, DWORD& err) {
  err = 0;
  HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!p) {
    err = GetLastError();
    return false;
  }
  std::vector<wchar_t> buf(32768);
  DWORD n = static_cast<DWORD>(buf.size());
  const BOOL ok = QueryFullProcessImageNameW(p, 0, buf.data(), &n);
  err = ok ? 0 : GetLastError();
  CloseHandle(p);
  if (!ok) return false;
  out.assign(buf.data(), n);
  return true;
}

// FileVersion DİZESİ (PowerShell FileVersionInfo.FileVersion ile aynı kaynak). Dize yoksa sabit sürüm bilgisi.
bool ReadFileVersion(const std::wstring& path, std::string& out) {
  DWORD dummy = 0;
  const DWORD sz = GetFileVersionInfoSizeW(path.c_str(), &dummy);
  if (sz == 0 || sz > (1u << 20)) return false;
  std::vector<BYTE> buf(sz);
  if (!GetFileVersionInfoW(path.c_str(), 0, sz, buf.data())) return false;
  struct Lang {
    WORD lang, cp;
  };
  Lang* tr = nullptr;
  UINT trLen = 0;
  if (VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<LPVOID*>(&tr), &trLen) && tr) {
    for (UINT i = 0; i < trLen / sizeof(Lang); ++i) {
      wchar_t sub[96];
      _snwprintf(sub, 96, L"\\StringFileInfo\\%04x%04x\\FileVersion", static_cast<unsigned>(tr[i].lang), static_cast<unsigned>(tr[i].cp));
      sub[95] = 0;
      wchar_t* v = nullptr;
      UINT vl = 0;
      if (VerQueryValueW(buf.data(), sub, reinterpret_cast<LPVOID*>(&v), &vl) && v && vl > 0 && v[0]) {
        out = Narrow(std::wstring(v));
        return true;
      }
    }
  }
  VS_FIXEDFILEINFO* ffi = nullptr;
  UINT fl = 0;
  if (VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &fl) && ffi && fl >= sizeof(VS_FIXEDFILEINFO)) {
    char b[64];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u", static_cast<unsigned>(HIWORD(ffi->dwFileVersionMS)), static_cast<unsigned>(LOWORD(ffi->dwFileVersionMS)),
                  static_cast<unsigned>(HIWORD(ffi->dwFileVersionLS)), static_cast<unsigned>(LOWORD(ffi->dwFileVersionLS)));
    out = b;
    return true;
  }
  return false;
}

// Dizin güvenliği: zincirdeki HER dizin gerçek dizin (bağlantı/junction/sembolik bağ DEĞİL) ve açık tanıtıcının kanonik yolu beklenenle aynı.
bool CheckDirChain(const std::wstring& root, const std::vector<std::wstring>& parts, std::string& why) {
  std::wstring cur = root;
  for (const std::wstring& part : parts) {
    cur += L"\\" + part;
    const DWORD a = GetFileAttributesW(cur.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
      why = "directory component missing: " + Narrow(part) + " (Win32 error " + std::to_string(GetLastError()) + ")";
      return false;
    }
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) {
      why = "path component is not a directory: " + Narrow(part);
      return false;
    }
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) {
      why = "path component is a link/junction/reparse point: " + Narrow(part);
      return false;
    }
  }
  return true;
}

bool CheckCanonicalDir(const std::wstring& dir, std::string& why) {
  Handle d(CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!d.ok()) {
    why = "cannot open the profile directory (Win32 error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  BY_HANDLE_FILE_INFORMATION bi;
  if (!GetFileInformationByHandle(d.h, &bi) || !(bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    why = "the profile directory is not a plain directory";
    return false;
  }
  const std::wstring fin = FinalPathOf(d.h);
  if (fin.empty()) {
    why = "the canonical path of the profile directory could not be determined";
    return false;
  }
  if (!PathEq(fin, dir)) {
    why = "the canonical path of the profile directory differs from the expected path (link/junction/subst)";
    return false;
  }
  return true;
}

uint64_t Fnv(uint64_t h, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    h ^= (v >> (8 * i)) & 0xFFu;
    h *= 1099511628211ull;
  }
  return h;
}

uint64_t Ft(const FILETIME& f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

oem::FileStamp StampOf(const BY_HANDLE_FILE_INFORMATION& bi) {
  oem::FileStamp s;
  s.valid = true;
  uint64_t id = 1469598103934665603ull;
  id = Fnv(id, bi.dwVolumeSerialNumber);
  id = Fnv(id, bi.nFileIndexHigh);
  id = Fnv(id, bi.nFileIndexLow);
  s.id = id;
  s.size = (static_cast<uint64_t>(bi.nFileSizeHigh) << 32) | bi.nFileSizeLow;
  s.mtime = Ft(bi.ftLastWriteTime);
  s.links = bi.nNumberOfLinks;
  return s;
}

// Düz dosya mı (dizin/bağlantı değil), tek sert bağ mı, kanonik yolu beklenen mi? (okuma ve değiştirme öncesi ortak denetim)
Io VerifyPlainFile(HANDLE h, const std::wstring& expectedPath, BY_HANDLE_FILE_INFORMATION& bi) {
  if (!GetFileInformationByHandle(h, &bi)) return Fail(GetLastError(), "query file information");
  if (bi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return Unsafe("the path is a directory");
  if (bi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return Unsafe("the file is a symbolic link / reparse point");
  if (bi.nNumberOfLinks != 1) return Unsafe("the file has more than one hard link");
  const std::wstring fin = FinalPathOf(h);
  if (fin.empty()) return Unsafe("the canonical path of the file could not be determined");
  if (!PathEq(fin, expectedPath)) return Unsafe("the canonical path of the file differs from the expected path");
  return Io();
}

}  // namespace

// ---------------------------------------------------------------------------------------------------- servis ve dizin
oem::ServiceInfo WinOemPlatform::ProbeService() {
  oem::ServiceInfo s;
  dir_.clear();
  s.elevated = IsElevated();

#ifdef MGP_DEMO
  // YALNIZ demo/Wine derlemesi: gerçek dosya işlemlerini (CreateFileW/ReplaceFileW/FindFirstFile ...) sahte bir klasörde sınamak için.
  // Üretim EXE'sinde bu blok HİÇ derlenmez.
  const std::wstring demoDir = EnvW(L"MGP_DEMO_OEM_DIR");
  if (demoDir.empty()) {
    // Demo derlemesinin EC/NVML'si SAHTEDİR. Sahte klasör verilmediyse (demo yanlışlıkla GERÇEK makinede çalıştırılırsa) gerçek
    // GCUService/OEM klasörü ASLA aranmaz: sahte doğrulamaya dayanarak gerçek OEM dosyasına yazılmasın. Modül "çalışmıyor" olarak kilitlenir.
    s.running = false;
    s.detail = "demo build: no fake OEM folder configured (MGP_DEMO_OEM_DIR); the real OEM profile is never searched or touched";
    return s;
  }
  {
    const std::wstring mode = EnvW(L"MGP_DEMO_GCU");
    s.elevated = (mode != L"noadmin");
    s.running = (mode != L"none");
    s.pathMatches = s.running && mode != L"badpath";
    s.versionRead = s.running && mode != L"nover";
    s.version = (mode == L"badver") ? "1.0.2.71" : oem::kExpectedGcuVersion;
    std::string why;
    if (!s.running) {
      s.detail = "demo: GCUService.exe is not running";
      return s;
    }
    if (CheckCanonicalDir(demoDir, why)) {
      s.dirSafe = true;
      if (s.elevated && s.pathMatches) dir_ = demoDir;
    } else {
      s.detail = why;
    }
    return s;
  }
#endif

  std::vector<DWORD> pids;
  if (!EnumGcuProcesses(pids)) {
    s.detail = "the process list is unavailable (Win32 error " + std::to_string(GetLastError()) + ")";
    return s;
  }
  if (pids.empty()) {
    s.detail = "GCUService.exe is not running";
    return s;
  }
  s.running = true;

  const std::wstring pf = ProgramFilesRoot();
  if (pf.empty()) {
    s.detail = "the Program Files folder could not be determined";
    return s;
  }
  const std::wstring svcDir = pf + L"\\OEM\\Control Center\\UniwillService\\MyControlCenter";
  const std::wstring exePath = svcDir + L"\\GCUService.exe";

  // Çalışan HER GCUService.exe örneği beklenen tam yolda olmalı (başka yerden çalışan taklitçi ya da erişilemeyen örnek => kilit).
  s.pathMatches = true;
  for (DWORD pid : pids) {
    std::wstring img;
    DWORD err = 0;
    if (!ImagePathOf(pid, img, err)) {
      s.pathMatches = false;
      s.detail = "the image path of a running GCUService.exe could not be read (Win32 error " + std::to_string(err) + ")";
      break;
    }
    if (!PathEq(img, exePath)) {
      s.pathMatches = false;
      s.detail = "a running GCUService.exe is not at the expected OEM path";
      break;
    }
  }
  if (!s.pathMatches) return s;

  std::string ver;
  if (ReadFileVersion(exePath, ver)) {
    s.versionRead = true;
    s.version = ver;
  }

  const std::wstring profDir = svcDir + L"\\" + Widen(oem::kProfileDirName);
  std::string why;
  if (CheckDirChain(pf, {L"OEM", L"Control Center", L"UniwillService", L"MyControlCenter", Widen(oem::kProfileDirName)}, why) &&
      CheckCanonicalDir(profDir, why)) {
    s.dirSafe = true;
    if (s.elevated) dir_ = profDir;
  } else {
    s.detail = why;
  }
  return s;
}

bool WinOemPlatform::PathFor(const std::string& leaf, std::wstring& out, Io& io) const {
  if (dir_.empty()) {
    io = Unsafe("the OEM profile directory has not been verified");
    return false;
  }
  if (!oem::IsKnownLeaf(leaf)) {
    io = Unsafe("file name not allowed");
    return false;
  }
  out = dir_ + L"\\" + Widen(leaf);  // leaf yalnız ASCII kalıplarla eşleşir (IsKnownLeaf)
  return true;
}

// ---------------------------------------------------------------------------------------------------- okuma
oem::FileData WinOemPlatform::ReadFile(const std::string& leaf) {
  oem::FileData d;
  std::wstring p;
  if (!PathFor(leaf, p, d.io)) return d;
  Handle h(CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!h.ok()) {
    d.io = Fail(GetLastError(), "open " + leaf);
    return d;
  }
  BY_HANDLE_FILE_INFORMATION bi1;
  d.io = VerifyPlainFile(h.h, p, bi1);
  if (!d.io.ok()) return d;
  const oem::FileStamp st1 = StampOf(bi1);
  if (st1.size > oem::kMaxJsonBytes) {
    d.io.code = IoCode::TooLarge;
    d.io.msg = "the file is larger than the safety limit";
    return d;
  }
  d.bytes.resize(static_cast<size_t>(st1.size));
  size_t off = 0;
  while (off < d.bytes.size()) {
    DWORD got = 0;
    const DWORD want = static_cast<DWORD>(d.bytes.size() - off > (1u << 20) ? (1u << 20) : d.bytes.size() - off);
    if (!::ReadFile(h.h, d.bytes.data() + off, want, &got, nullptr)) {
      d.io = Fail(GetLastError(), "read " + leaf);
      d.bytes.clear();
      return d;
    }
    if (got == 0) break;
    off += got;
  }
  BY_HANDLE_FILE_INFORMATION bi2;
  if (!GetFileInformationByHandle(h.h, &bi2)) {
    d.io = Fail(GetLastError(), "query file information");
    d.bytes.clear();
    return d;
  }
  const oem::FileStamp st2 = StampOf(bi2);
  if (off != d.bytes.size() || !oem::SameStamp(st1, st2)) {
    d.io.code = IoCode::Sharing;
    d.io.msg = "the file changed while it was being read";
    d.bytes.clear();
    return d;
  }
  d.stamp = st2;
  return d;
}

// ---------------------------------------------------------------------------------------------------- yazma (yalnız YENİ dosya)
oem::Io WinOemPlatform::CreateNew(const std::string& leaf, const std::vector<uint8_t>& data, bool readOnly) {
  std::wstring p;
  Io io;
  if (!PathFor(leaf, p, io)) return io;
  HANDLE raw = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
  if (raw == INVALID_HANDLE_VALUE) return Fail(GetLastError(), "create " + leaf);  // var olan dosya ASLA ezilmez (CREATE_NEW)
  bool ok = true;
  DWORD err = 0;
  {
    Handle h(raw);
    size_t off = 0;
    while (off < data.size()) {
      DWORD wr = 0;
      const DWORD chunk = static_cast<DWORD>(data.size() - off > (1u << 20) ? (1u << 20) : data.size() - off);
      if (!WriteFile(h.h, data.data() + off, chunk, &wr, nullptr) || wr == 0) {
        ok = false;
        err = GetLastError();
        break;
      }
      off += wr;
    }
    if (ok && !FlushFileBuffers(h.h)) {
      ok = false;
      err = GetLastError();
    }
  }
  if (!ok) {
    DeleteFileW(p.c_str());  // yalnız az önce BİZİM oluşturduğumuz yarım dosya
    return Fail(err, "write " + leaf);
  }
  if (readOnly && !SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_READONLY)) {
    Io note;
    note.msg = "note: the read-only attribute could not be set (Win32 error " + std::to_string(GetLastError()) + ")";
    return note;  // başarı: içerik yazıldı ve geri okunarak doğrulanacak; öznitelik yalnız ek koruma
  }
  return Io();
}

oem::Io WinOemPlatform::AtomicReplace(const std::string& active, const std::string& replacement, const std::string& swapBackup) {
  Io io;
  std::wstring pa, pr, ps;
  if (!PathFor(active, pa, io) || !PathFor(replacement, pr, io) || !PathFor(swapBackup, ps, io)) return io;
  if (swapBackup.empty() || active == replacement || active == swapBackup || replacement == swapBackup) return Unsafe("invalid replace arguments");
#ifdef MGP_DEMO
  // YALNIZ demo/Wine derlemesi: OEM dosyasına yazma BAŞARISIZLIĞINDA arayüzün anlık değişikliği geri aldığını uçtan uca sınamak için
  // atomik değiştirmeden ÖNCE (hiçbir dosyaya dokunmadan) hata döndürür. Üretim EXE'sinde bu blok HİÇ derlenmez.
  if (EnvW(L"MGP_DEMO_OEM_FAULT") == L"replace") {
    Io e;
    e.code = IoCode::AccessDenied;
    e.sys = ERROR_ACCESS_DENIED;
    e.msg = "demo: injected failure before the atomic replace (no file was touched)";
    return e;
  }
#endif
  if (GetFileAttributesW(ps.c_str()) != INVALID_FILE_ATTRIBUTES) {
    Io e;
    e.code = IoCode::Exists;
    e.msg = "the swap file name already exists";
    return e;
  }
  {  // değiştirilecek dosya hâlâ düz bir dosya mı (araya sembolik bağ/sert bağ girmedi mi)?
    Handle h(CreateFileW(pa.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!h.ok()) return Fail(GetLastError(), "open the file to be replaced");
    BY_HANDLE_FILE_INFORMATION bi;
    io = VerifyPlainFile(h.h, pa, bi);
    if (!io.ok()) return io;
  }
  // Bayrak YOK: ACL/öznitelik birleştirme hataları yok sayılmaz (REPLACEFILE_IGNORE_*_ERRORS verilmez).
  if (!ReplaceFileW(pa.c_str(), pr.c_str(), ps.c_str(), 0, nullptr, nullptr)) return Fail(GetLastError(), "ReplaceFileW");
  return Io();
}

oem::Io WinOemPlatform::RemoveOwn(const std::string& leaf) {
  Io io;
  if (!oem::IsOwnTempName(leaf)) return Unsafe("only this application's own temporary files may be removed");
  std::wstring p;
  if (!PathFor(leaf, p, io)) return io;
  const DWORD a = GetFileAttributesW(p.c_str());
  if (a == INVALID_FILE_ATTRIBUTES) return Fail(GetLastError(), "stat " + leaf);
  if (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return Unsafe("not a plain file");
  if (a & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(p.c_str(), a & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
  if (!DeleteFileW(p.c_str())) return Fail(GetLastError(), "delete " + leaf);
  return Io();
}

std::string WinOemPlatform::StampNow() { return FileStamp(); }

std::string WinOemPlatform::RandomId8() {
  uint8_t b[4] = {0, 0, 0, 0};
  if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, b, 4, BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
    // Son çare (neredeyse hiç olmaz): zaman + sayaç + süreç no karışımı. Benzersizlik yine CREATE_NEW ile korunur.
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    uint64_t x = static_cast<uint64_t>(q.QuadPart) ^ (static_cast<uint64_t>(GetCurrentProcessId()) << 32) ^ GetTickCount64();
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    for (int i = 0; i < 4; ++i) b[i] = static_cast<uint8_t>(x >> (8 * i));
  }
  char out[16];
  std::snprintf(out, sizeof out, "%02x%02x%02x%02x", b[0], b[1], b[2], b[3]);
  return out;
}

}  // namespace win
}  // namespace mgp
