// PowerProfileEngine: profil uygulama, ön koşul denetimi, EC read-back, NVML doğrulaması ve geri alma.
// Saf mantık (OS çağrısı yok): sahte EC/NVML/saat ile test edilir. Tek iş parçacığından çağrılmalıdır.
#pragma once
#include <exception>
#include <string>
#include <vector>

#include "ec_constants.h"
#include "hardware_identity_guard.h"
#include "interfaces.h"

namespace mgp {

enum class Blocker {
  IdentityUnreadable,
  IdentityMismatch,
  DriverAbsent,
  DriverAccessDenied,
  DriverError,
  EcReadInvalid,
  EcStateUnexpected,
  NvmlUnavailable,
  GpuMismatch,
  MaxLimitMismatch,
  FaultLatched  // doğrulanamayan geri alma sonrası: kullanıcı "Yenile" ile durumu doğrulayana dek yazma kapalı
};

struct EcSnapshot {
  bool ok = false;
  uint8_t ctrl = 0, offset = 0, tpp = 0, byte746 = 0;
  std::string error;
};

struct LiveState {
  IdentityResult identity;
  DriverState driver = DriverState::Absent;
  bool driverProbed = false;  // kimlik birebir değilse sürücü HİÇ denenmez (driver alanı anlamsızdır)
  uint32_t driverWinError = 0;
  bool ecAttempted = false;
  EcSnapshot ec;
  bool nvInitOk = false;
  NvStatic nvStatic;
  NvDynamic nv;
  int ecProfile = 0;      // yalnız EC'den çıkarılan profil (0 = tanınmıyor)
  int activeProfile = 0;  // EC ve NVML tutarlıysa profil
  bool consistent = false;
  std::vector<Blocker> blockers;
  std::vector<std::string> blockerDetails;  // blockers ile aynı sıra, teknik/İngilizce
  bool CanWrite() const { return blockers.empty(); }
  bool HasBlocker(Blocker b) const {
    for (Blocker x : blockers)
      if (x == b) return true;
    return false;
  }
};

struct WriteStep {
  uint16_t addr;
  uint8_t value;
};

enum class Outcome {
  Ok,
  AlreadyActive,
  BlockedUnknownProfile,
  BlockedNotReady,
  BlockedConsent,
  BlockedTemp,
  BlockedAboveVbiosMax,
  FailedNoChange,            // hata oldu, EC değişmedi (doğrulandı)
  FailedRolledBack,          // hata oldu, önceki değere dönüldü (doğrulandı)
  FailedRollbackUnverified   // hata oldu, geri alma DOĞRULANAMADI -> kritik
};

struct ApplyOptions {
  bool consentOverOem = false;     // >140 W için kullanıcı onayı verilmiş mi
  bool consentUnverified = false;  // 130/135 W (canlı doğrulanmamış) için onay
  bool consentModel = false;       // SMBIOS'ta GM5TG7Y kart etiketi YOKSA: kullanıcı "bu cihaz GM5TG7Y" dedi mi
};

struct ApplyResult {
  Outcome outcome = Outcome::BlockedNotReady;
  int target = 0;
  std::string detail;  // teknik, İngilizce
  LiveState before, after;
  bool ecVerified = false, nvVerified = false;
  std::vector<WriteStep> performed;
  bool rolledBack = false, rollbackVerified = false;
  bool Succeeded() const { return outcome == Outcome::Ok || outcome == Outcome::AlreadyActive; }
};

struct Policy {
  uint32_t stepSettleMs = 200;        // her EC yazımından sonra bekleme
  uint32_t verifyTimeoutMs = 8000;    // NVML'nin yeni limiti göstermesi için azami süre
  uint32_t verifyPollMs = 400;
  uint32_t finalConfirmDelayMs = 1200;  // son teyit öncesi bekleme
  uint32_t rollbackVerifyTimeoutMs = 6000;
};

class PowerProfileEngine {
 public:
  PowerProfileEngine(IEcDriver& ec, INvidiaTelemetry& nv, ISmbiosSource& smbios, IClock& clock, ILog& log,
                     Policy policy = Policy());

  // Salt-okunur tam değerlendirme (kimlik, sürücü, EC, NVML). light=true: yoklama için yalnız
  // 0x0743/0x0744 + NVML dinamik değerleri okunur (0x0745/0x0746 önbellekten).
  // clearFaultIfClean: yalnız kullanıcının elle "Yenile"si için; kilit varsa ve durum temiz+tutarlıysa kilidi açar.
  LiveState ReadLive(bool light = false, bool clearFaultIfClean = false);

  ApplyResult Apply(int watts, const ApplyOptions& opts);

  // OEM profil dosyasına (kalıcılık) yazma BAŞARISIZ olduğunda: EC+NVIDIA zaten doğrulanmış yeni profildeyken, kullanıcının
  // seçmediği yarım bir sonuç kalmasın diye, uygulama ÖNCESİ canlı duruma (before.ec) dönmeyi dener. Yeni bir EC yazma ilkesi
  // YOKTUR: mevcut Rollback kodunun kendisi (izin listesi, sıralı plan, her yazmadan sonra geri okuma, NVML teyidi) kullanılır.
  // Doğrulanamazsa mevcut kritik davranış (yazma kilidi) aynen geçerlidir. before.ec.ok olmalıdır; değilse hiçbir şey yazılmaz.
  ApplyResult RestoreLive(const LiveState& before, const std::string& why);

  bool FaultLatched() const { return faultLatched_; }

  void RefreshIdentity() { identityDone_ = false; }
  void Shutdown();

  // Test ve tanılama için açık: bir EC durumundan hedef EC durumuna güvenli sıralı yazma planı.
  // Yükselirken önce cTGP biti (ofset 0 iken = 125 W), sonra ofset; düşerken önce ofset, sonra bit.
  static std::vector<WriteStep> PlanRaw(uint8_t curCtrl, uint8_t curOff, uint8_t tCtrl, uint8_t tOff);

 private:
  EcSnapshot ReadEcSnapshot(bool light);
  bool EnsureNvml(LiveState& s);
  bool WaitNvmlLimit(double targetW, NvDynamic& last, uint32_t timeoutMs);
  // start != nullptr ise her adımın sonucu yazmadan önce hesaplanır; güvensiz ara durum üretecek plan hiç yazılmaz.
  bool ExecutePlan(const std::vector<WriteStep>& plan, std::vector<WriteStep>* performed, std::string& err,
                   const EcSnapshot* start);
  void Rollback(const EcSnapshot& pre, ApplyResult& r);
  ApplyResult FailWithRollback(const EcSnapshot& pre, ApplyResult r, const std::string& why);
  ApplyResult ApplyInner(int watts, const ApplyOptions& opts, ApplyResult& r, EcSnapshot& preEc, bool& havePre);
  ApplyResult AbortAfterException(ApplyResult& r, const EcSnapshot& pre, bool havePre, const char* what);
  void L(LogLevel lv, const std::string& m) { log_.Write(lv, m); }

  IEcDriver& ec_;
  INvidiaTelemetry& nv_;
  ISmbiosSource& smbios_;
  IClock& clock_;
  ILog& log_;
  Policy policy_;

  bool identityDone_ = false;
  IdentityResult identity_;
  bool nvInit_ = false;
  bool nvStaticDone_ = false;
  NvStatic nvStatic_;
  uint8_t cachedTpp_ = 0, cachedByte746_ = 0;
  bool cachedFullEc_ = false;
  bool faultLatched_ = false;
};

const char* OutcomeName(Outcome o);
const char* BlockerName(Blocker b);

}  // namespace mgp
