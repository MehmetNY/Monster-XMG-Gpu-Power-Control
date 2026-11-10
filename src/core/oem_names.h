// OEM profil dosya adları ve yedek/geçici ad kalıpları (saf mantık). Dosya adlarının TEK kaynağı burasıdır:
// platform katmanı yalnız buradaki kalıplara uyan düz adlarla çalışır; ayar dosyasından gelen yedek adı da aynı kalıpla doğrulanır.
#pragma once
#include <string>

namespace mgp {
namespace oem {

inline constexpr char kExpectedGcuVersion[] = "1.0.2.70";
inline constexpr char kProfileDirName[] = "UserPofiles";  // OEM'in yazım hatası bilerek AYNEN korunur
inline constexpr char kMainOptionLeaf[] = "MainOption.json";
inline constexpr char kProfileLeaf[] = "Mode3_Profile1.json";
inline constexpr long long kExpectedOperatingMode = 2;
inline constexpr long long kExpectedTurboProfileIndex = 0;

inline constexpr char kBackupInfix[] = ".mgp-original-";   // Mode3_Profile1.json.mgp-original-YYYYMMDD-HHMMSS-xxxxxxxx.bak
inline constexpr char kTmpInfix[] = ".mgp-tmp-";           // Mode3_Profile1.json.mgp-tmp-xxxxxxxx
inline constexpr char kSwapInfix[] = ".mgp-swap-";         // Mode3_Profile1.json.mgp-swap-xxxxxxxx
inline constexpr char kRejectedInfix[] = ".mgp-rejected-"; // Mode3_Profile1.json.mgp-rejected-xxxxxxxx

bool IsHex8(const std::string& s);                    // 8 küçük harfli onaltılık hane
bool IsStamp(const std::string& s);                   // yyyyMMdd-HHmmss (rakamlar)
std::string MakeBackupName(const std::string& stamp, const std::string& id);
// <profil>.mgp-original-<stamp>-<id>.bak kalıbı; stamp/id çıkarılır. Başka her şey false.
bool ParseBackupName(const std::string& leaf, std::string* stamp, std::string* id);
std::string MakeTempName(const char* infix, const std::string& id);  // infix: kTmpInfix / kSwapInfix / kRejectedInfix
bool IsOwnTempName(const std::string& leaf);          // yalnız bu işlemin ürettiği geçici adlar (RemoveOwn izin listesi)
// Platformun kabul ettiği AD kümesi: MainOption.json | Mode3_Profile1.json | yedek kalıbı | kendi geçici adları.
bool IsKnownLeaf(const std::string& leaf);

}  // namespace oem
}  // namespace mgp
