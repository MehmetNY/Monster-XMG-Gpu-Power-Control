// MONSTER GPU POWER - TEK MERKEZ: sürücü yolu, IOCTL, EC adresleri, profil haritası, güvenlik eşikleri.
//
// Kanıt kaynakları (cihaz sahibinin kendi ölçümleri ve OEM sürücü DLL'sinin incelenmesi; OEM ikilileri depoda yoktur):
//  * IOCTL / aygıt yolu: OEM ACPIDriverDll.dll içindeki ReadEC/WriteEC
//    makine kodundan okundu (CreateFileW + DeviceIoControl). Bkz. docs/SURUCU_ABI.md
//  * EC değerleri: cihaz sahibinin canlı ölçümleri (bkz. docs/SURUCU_ABI.md, profil tablosu)
//  * 0x0743 bit adları: Linux uniwill-acpi.c makroları (yalnız isim; Linux bunları kullanmıyor)
//
// Bir cihaz/BIOS/EC revizyonu değişirse yalnız BU dosya değiştirilir.
#pragma once
#include <cstdint>

namespace mgp {

// ---------------------------------------------------------------- Sürücü arayüzü
inline constexpr wchar_t kAcpiDevicePath[] = L"\\\\.\\ACPIDriver";
inline constexpr uint32_t kIoctlReadEc = 0x9C40A488u;   // ReadEC  (OEM DLL ile aynı)
inline constexpr uint32_t kIoctlWriteEc = 0x9C40A48Cu;  // WriteEC (OEM DLL ile aynı)
// OEM DLL her çağrıda 4 MiB giriş/çıkış tamponu verir. Daha küçük tamponun kabul edildiği
// doğrulanmadığı için birebir aynısı kullanılır (docs/SURUCU_ABI.md, "Tampon boyutu").
inline constexpr uint32_t kIoBufferBytes = 0x400000u;

// ---------------------------------------------------------------- EC adresleri
inline constexpr uint16_t kEcAddrCtrl = 0x0743;        // cTGP/DB kontrol baytı
inline constexpr uint16_t kEcAddrCtgpOffset = 0x0744;  // cTGP artışı (W)
inline constexpr uint16_t kEcAddrTpp = 0x0745;         // YAZMA YOK (testlerde 0)
inline constexpr uint16_t kEcAddrByte746 = 0x0746;     // YAZMA YOK (Linux: MAX_TGP; testlerde 0)

inline constexpr uint8_t kCtrlGeneral = 0x01;  // bit0   (bit1 = DynBoost: bu uygulama onu ASLA yazmaz)
inline constexpr uint8_t kCtrlCtgp = 0x04;     // bit2
inline constexpr uint8_t kCtrlOemOff = kCtrlGeneral;              // 0x01: OEM AutomaticOverboost KAPALI: canlı ölçüldü
inline constexpr uint8_t kCtrlOemOn = kCtrlGeneral | kCtrlCtgp;   // 0x05: OEM AutomaticOverboost AÇIK: canlı ölçüldü
static_assert(kCtrlOemOff == 0x01 && kCtrlOemOn == 0x05, "EC kontrol baytı değerleri canlı ölçülen değerlerdir; değiştirilemez");
inline constexpr int kMaxOffsetW = 25;
inline constexpr int kOffsetStepW = 5;

// Yalnız bu adresler okunabilir / yalnız şu adres+değer çiftleri yazılabilir (savunma katmanı).
constexpr bool IsReadableEcAddress(uint16_t a) { return a >= 0x0743 && a <= 0x0746; }
constexpr bool IsAllowedEcWrite(uint16_t a, uint8_t v) {
  return (a == kEcAddrCtrl && (v == kCtrlOemOff || v == kCtrlOemOn)) ||
         (a == kEcAddrCtgpOffset && v <= kMaxOffsetW && (v % kOffsetStepW) == 0);
}

// ---------------------------------------------------------------- Profil haritası
enum class Evidence {
  LiveOemOff,         // OEM overboost KAPALI iken canlı ölçüldü (125 W)
  LiveOemOn,          // OEM overboost AÇIK iken canlı ölçüldü (140 W)
  LiveIdleProbe,      // EC+NVIDIA canlı doğrulandı, yüksüz (145 W)
  LiveLoaded,         // EC+NVIDIA doğrulandı ve yükte ~150 W ölçüldü (150 W)
  ExpectedUnverified  // beklenen eşleme, CANLI TEST EDİLMEDİ (130/135 W)
};

struct ProfileDef {
  int watts;
  uint8_t ctrl;    // hedef 0x0743
  uint8_t offset;  // hedef 0x0744
  Evidence evidence;
};

inline constexpr int kBaseWatts = 125;      // NVIDIA varsayılan limit
inline constexpr int kOemMaxWatts = 140;    // OEM'in bu platformda belirttiği en yüksek profil
inline constexpr int kVbiosMaxWatts = 150;  // VBIOS Max Power Limit

inline constexpr ProfileDef kProfiles[6] = {
    {125, kCtrlOemOff, 0, Evidence::LiveOemOff},
    {130, kCtrlOemOn, 5, Evidence::ExpectedUnverified},
    {135, kCtrlOemOn, 10, Evidence::ExpectedUnverified},
    {140, kCtrlOemOn, 15, Evidence::LiveOemOn},
    {145, kCtrlOemOn, 20, Evidence::LiveIdleProbe},
    {150, kCtrlOemOn, 25, Evidence::LiveLoaded},
};

constexpr const ProfileDef* FindProfile(int watts) {
  for (const ProfileDef& p : kProfiles)
    if (p.watts == watts) return &p;
  return nullptr;
}

constexpr bool IsValidOffset(int off) { return off >= 0 && off <= kMaxOffsetW && (off % kOffsetStepW) == 0; }

// EC durumundan profil (watt). 0 = tanınmayan durum.
// (0x01,0) = OEM kapalı = 125 W. (0x05,N) = 125+N W; (0x05,0) de 125 W'a denk gelir.
constexpr int ProfileFromEc(uint8_t ctrl, uint8_t offset) {
  if (!IsValidOffset(offset)) return 0;
  if (ctrl == kCtrlOemOff) return offset == 0 ? kBaseWatts : 0;
  if (ctrl == kCtrlOemOn) return kBaseWatts + offset;
  return 0;
}

// ---------------------------------------------------------------- Donanım kimliği
inline constexpr char kExpectedManufacturer[] = "MONSTER";
inline constexpr char kExpectedProductPrefix[] = "TULPAR T5 V21.7";
inline constexpr char kExpectedBiosVersion[] = "N.1.07Mon08";
inline constexpr char kExpectedBoardTag[] = "GM5TG7Y";  // SMBIOS'ta görünürse ek doğrulama
inline constexpr char kExpectedGpuNameA[] = "3070";
inline constexpr char kExpectedGpuNameB[] = "LAPTOP";

// ---------------------------------------------------------------- Eşikler
inline constexpr double kNvToleranceW = 0.6;       // NVML limiti hedefle bu kadar uyuşmalı
inline constexpr int kMaxTempToRaiseC = 80;        // 140 W üzerine çıkarken bu sıcaklıkta reddet
inline constexpr int kTempWarnC = 83;              // izleme açıkken uyarı eşiği (>140 W profilde)

}  // namespace mgp
