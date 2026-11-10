#include "ec_driver_win.h"

#include <cstdio>

#include "../core/ec_constants.h"

namespace mgp {
namespace win {
namespace {

HANDLE OpenDevice(DWORD* err) {
  HANDLE h = CreateFileW(kAcpiDevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    if (err) *err = GetLastError();
    return INVALID_HANDLE_VALUE;
  }
  if (err) *err = 0;
  return h;
}

}  // namespace

DriverState AcpiDriverEc::Probe(uint32_t* winError) {
  DWORD err = 0;
  HANDLE h = OpenDevice(&err);
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
    if (winError) *winError = 0;
    return DriverState::Connected;
  }
  if (winError) *winError = err;
  switch (err) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
      return DriverState::Absent;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
      return DriverState::AccessDenied;
    default:
      return DriverState::Error;
  }
}

EcByte AcpiDriverEc::Read(uint16_t addr) { return Transact(kIoctlReadEc, addr, 0, false); }
EcByte AcpiDriverEc::Write(uint16_t addr, uint8_t value) { return Transact(kIoctlWriteEc, addr, value, true); }

// OEM DLL ile aynı işlem: CreateFileW(\\.\ACPIDriver) -> DeviceIoControl(ioctl, tampon, 4 MiB, tampon, 4 MiB)
// Tampon düzeni: DWORD[0] = EC adresi, (yazma için) DWORD[1] = değer; okuma sonucu ilk baytta döner.
// OEM DLL'den FARKI: DeviceIoControl dönüş değeri ve dönen bayt sayısı KONTROL EDİLİR
// (OEM DLL başarısızlıkta adresin düşük baytını "değer" diye döndürür).
EcByte AcpiDriverEc::Transact(uint32_t ioctl, uint16_t addr, uint8_t value, bool isWrite) {
  EcByte r;
  if (!IsReadableEcAddress(addr)) {
    r.error = "EC address not in read allow-list";
    return r;
  }
  if (isWrite && !IsAllowedEcWrite(addr, value)) {
    r.error = "EC write not in allow-list";
    return r;
  }

  DWORD err = 0;
  HANDLE h = OpenDevice(&err);
  if (h == INVALID_HANDLE_VALUE) {
    r.winError = err;
    char b[64];
    std::snprintf(b, sizeof b, "CreateFile failed (win32=%lu)", static_cast<unsigned long>(err));
    r.error = b;
    return r;
  }

  void* buf = VirtualAlloc(nullptr, kIoBufferBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!buf) {
    r.winError = GetLastError();
    r.error = "VirtualAlloc failed";
    CloseHandle(h);
    return r;
  }
  uint32_t* d = static_cast<uint32_t*>(buf);
  d[0] = addr;
  if (isWrite) d[1] = value;

  DWORD ret = 0;
  const BOOL ok = DeviceIoControl(h, ioctl, buf, kIoBufferBytes, buf, kIoBufferBytes, &ret, nullptr);
  const DWORD le = ok ? 0 : GetLastError();
  const uint8_t first = *static_cast<uint8_t*>(buf);
  VirtualFree(buf, 0, MEM_RELEASE);
  CloseHandle(h);

  r.bytesReturned = ret;
  r.winError = le;
  if (!ok) {
    char b[64];
    std::snprintf(b, sizeof b, "DeviceIoControl failed (win32=%lu)", static_cast<unsigned long>(le));
    r.error = b;
    return r;
  }
  if (!isWrite) {
    if (ret < 1) {  // veri dönmediyse tampondaki bayt, girişteki adresin yankısıdır: GEÇERSİZ
      r.error = "driver returned no data (stale echo suppressed)";
      return r;
    }
    r.value = first;
  }
  r.ok = true;
  return r;
}

}  // namespace win
}  // namespace mgp
