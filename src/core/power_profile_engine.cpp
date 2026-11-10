#include "power_profile_engine.h"

#include <cmath>
#include <cstdio>

namespace mgp {
namespace {

std::string Hex2(unsigned v) {
  char b[16];
  std::snprintf(b, sizeof b, "0x%02X", v & 0xFFu);
  return b;
}
std::string Hex4(unsigned v) {
  char b[16];
  std::snprintf(b, sizeof b, "0x%04X", v & 0xFFFFu);
  return b;
}
std::string Num1(double d) {
  char b[32];
  std::snprintf(b, sizeof b, "%.1f", d);
  return b;
}
std::string Join(const std::vector<std::string>& v) {
  std::string r;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) r += "; ";
    r += v[i];
  }
  return r;
}

bool ValidateEc(const EcSnapshot& e, std::string& why) {
  if (e.ctrl != kCtrlOemOff && e.ctrl != kCtrlOemOn) {
    why = "EC 0x0743=" + Hex2(e.ctrl) + " is not a known OEM state (expected 0x01 or 0x05)";
    return false;
  }
  if (!IsValidOffset(e.offset)) {
    why = "EC 0x0744=" + std::to_string(e.offset) + " is not a valid 5 W step in 0..25";
    return false;
  }
  if (e.ctrl == kCtrlOemOff && e.offset != 0) {
    why = "inconsistent EC state: cTGP disabled (0x01) but offset=" + std::to_string(e.offset);
    return false;
  }
  if (e.tpp != 0 || e.byte746 != 0) {
    why = "EC 0x0745/0x0746 are non-zero (" + Hex2(e.tpp) + "/" + Hex2(e.byte746) +
          "); Dynamic Boost/TPP may be active - not the measured baseline";
    return false;
  }
  return true;
}

// Bir (0x0743, 0x0744) çiftinin etkin watt karşılığı. cTGP biti kapalıyken ofset etkisizdir (taban 125 W).
int WattsOfPair(uint8_t ctrl, uint8_t off) { return kBaseWatts + ((ctrl & kCtrlCtgp) ? static_cast<int>(off) : 0); }

}  // namespace

const char* OutcomeName(Outcome o) {
  switch (o) {
    case Outcome::Ok: return "Ok";
    case Outcome::AlreadyActive: return "AlreadyActive";
    case Outcome::BlockedUnknownProfile: return "BlockedUnknownProfile";
    case Outcome::BlockedNotReady: return "BlockedNotReady";
    case Outcome::BlockedConsent: return "BlockedConsent";
    case Outcome::BlockedTemp: return "BlockedTemp";
    case Outcome::BlockedAboveVbiosMax: return "BlockedAboveVbiosMax";
    case Outcome::FailedNoChange: return "FailedNoChange";
    case Outcome::FailedRolledBack: return "FailedRolledBack";
    case Outcome::FailedRollbackUnverified: return "FailedRollbackUnverified";
  }
  return "?";
}

const char* BlockerName(Blocker b) {
  switch (b) {
    case Blocker::IdentityUnreadable: return "IdentityUnreadable";
    case Blocker::IdentityMismatch: return "IdentityMismatch";
    case Blocker::DriverAbsent: return "DriverAbsent";
    case Blocker::DriverAccessDenied: return "DriverAccessDenied";
    case Blocker::DriverError: return "DriverError";
    case Blocker::EcReadInvalid: return "EcReadInvalid";
    case Blocker::EcStateUnexpected: return "EcStateUnexpected";
    case Blocker::NvmlUnavailable: return "NvmlUnavailable";
    case Blocker::GpuMismatch: return "GpuMismatch";
    case Blocker::MaxLimitMismatch: return "MaxLimitMismatch";
    case Blocker::FaultLatched: return "FaultLatched";
  }
  return "?";
}

PowerProfileEngine::PowerProfileEngine(IEcDriver& ec, INvidiaTelemetry& nv, ISmbiosSource& smbios, IClock& clock,
                                       ILog& log, Policy policy)
    : ec_(ec), nv_(nv), smbios_(smbios), clock_(clock), log_(log), policy_(policy) {}

void PowerProfileEngine::Shutdown() {
  if (nvInit_) nv_.Shutdown();
  nvInit_ = false;
  nvStaticDone_ = false;
}

// ------------------------------------------------------------------ plan
std::vector<WriteStep> PowerProfileEngine::PlanRaw(uint8_t curCtrl, uint8_t curOff, uint8_t tCtrl, uint8_t tOff) {
  // Ön koşul (yalnız 0x01/0x05 durumlarında yazılır) sağlandığı için hedef değeri mutlak yazmak,
  // "yalnız cTGP bitini değiştir, diğer bitleri koru" ile birebir aynı sonucu verir.
  std::vector<WriteStep> steps;
  const bool tgtOn = (tCtrl & kCtrlCtgp) != 0;
  if (tgtOn) {
    // Yükselirken: önce cTGP biti (ofset 0 iken = taban 125 W), sonra ofset.
    // cTGP kapalıyken kalıntı ofset varsa (tutarsız durum) bit açılmadan ÖNCE ofset sıfırlanır; böylece
    // geçiş sırasında hiçbir zaman hedeften yüksek bir (0x05, ofset) çifti oluşmaz.
    uint8_t effOff = curOff;
    if (!(curCtrl & kCtrlCtgp) && curOff != 0) {
      steps.push_back({kEcAddrCtgpOffset, 0});
      effOff = 0;
    }
    if (curCtrl != tCtrl) steps.push_back({kEcAddrCtrl, tCtrl});
    if (effOff != tOff) steps.push_back({kEcAddrCtgpOffset, tOff});
  } else {
    // Düşerken: önce ofset 0, sonra cTGP bitini kapat.
    if (curOff != tOff) steps.push_back({kEcAddrCtgpOffset, tOff});
    if (curCtrl != tCtrl) steps.push_back({kEcAddrCtrl, tCtrl});
  }
  return steps;
}

// ------------------------------------------------------------------ okuma
EcSnapshot PowerProfileEngine::ReadEcSnapshot(bool light) {
  EcSnapshot s;
  auto rd = [&](uint16_t addr, uint8_t& out) -> bool {
    for (int attempt = 0; attempt < 2; ++attempt) {
      EcByte b = ec_.Read(addr);
      if (b.ok) {
        out = b.value;
        return true;
      }
      s.error = "ReadEC " + Hex4(addr) + " failed: " + b.error;
      if (attempt == 0) clock_.SleepMs(40);
    }
    return false;
  };
  if (!rd(kEcAddrCtrl, s.ctrl) || !rd(kEcAddrCtgpOffset, s.offset)) return s;
  if (!light || !cachedFullEc_) {
    if (!rd(kEcAddrTpp, s.tpp) || !rd(kEcAddrByte746, s.byte746)) return s;
    cachedTpp_ = s.tpp;
    cachedByte746_ = s.byte746;
    cachedFullEc_ = true;
  } else {
    s.tpp = cachedTpp_;
    s.byte746 = cachedByte746_;
  }
  s.ok = true;
  s.error.clear();
  return s;
}

bool PowerProfileEngine::EnsureNvml(LiveState& s) {
  if (!nvInit_) {
    std::string err;
    if (!nv_.Init(err)) {
      s.blockers.push_back(Blocker::NvmlUnavailable);
      s.blockerDetails.push_back("NVML init failed: " + err);
      return false;
    }
    nvInit_ = true;
  }
  if (!nvStaticDone_) {
    std::string err;
    NvStatic st;
    if (!nv_.ReadStatic(st, err)) {
      s.blockers.push_back(Blocker::NvmlUnavailable);
      s.blockerDetails.push_back("NVML static query failed: " + err);
      return false;
    }
    nvStatic_ = st;
    nvStaticDone_ = true;
    L(LogLevel::Info, "GPU: name='" + st.name + "' min=" + Num1(st.minW) + "W max=" + Num1(st.maxW) +
                          "W default=" + Num1(st.defaultW) + "W pciDevice=" + Hex4(st.pciDeviceId) + " bus=" + st.busId);
  }
  s.nvInitOk = true;
  s.nvStatic = nvStatic_;
  return true;
}

LiveState PowerProfileEngine::ReadLive(bool light, bool clearFaultIfClean) {
  LiveState s;
  if (!identityDone_) {
    std::vector<uint8_t> raw;
    std::string err;
    SmbiosInfo info;
    const bool ok = smbios_.ReadRaw(raw, err) && ParseRawSmbios(raw.data(), raw.size(), info);
    identity_ = EvaluateIdentity(info, ok);
    identityDone_ = true;
    L(LogLevel::Info, "Identity verdict=" + std::to_string(static_cast<int>(identity_.verdict)) + " " +
                          identity_.summary + (ok ? "" : " err=" + err));
    if (ok && info.ecMajor >= 0)
      L(LogLevel::Info, "SMBIOS EC firmware release " + std::to_string(info.ecMajor) + "." + std::to_string(info.ecMinor));
  }
  s.identity = identity_;

  if (identity_.verdict == IdentityVerdict::Unreadable) {
    s.blockers.push_back(Blocker::IdentityUnreadable);
    s.blockerDetails.push_back("SMBIOS unreadable");
  } else if (identity_.verdict != IdentityVerdict::Exact) {
    s.blockers.push_back(Blocker::IdentityMismatch);
    s.blockerDetails.push_back("hardware identity mismatch: " + identity_.summary);
  }

  // Kimlik birebir değilse EC'ye HİÇ dokunulmaz (okuma dahil).
  if (identity_.verdict == IdentityVerdict::Exact) {
    s.driver = ec_.Probe(&s.driverWinError);
    s.driverProbed = true;
    switch (s.driver) {
      case DriverState::Connected:
        break;
      case DriverState::Absent:
        s.blockers.push_back(Blocker::DriverAbsent);
        s.blockerDetails.push_back("\\\\.\\ACPIDriver not present (win32=" + std::to_string(s.driverWinError) + ")");
        break;
      case DriverState::AccessDenied:
        s.blockers.push_back(Blocker::DriverAccessDenied);
        s.blockerDetails.push_back("\\\\.\\ACPIDriver access denied (win32=" + std::to_string(s.driverWinError) + ")");
        break;
      case DriverState::Error:
        s.blockers.push_back(Blocker::DriverError);
        s.blockerDetails.push_back("\\\\.\\ACPIDriver open error (win32=" + std::to_string(s.driverWinError) + ")");
        break;
    }
    if (s.driver == DriverState::Connected) {
      s.ecAttempted = true;
      s.ec = ReadEcSnapshot(light);
      if (!s.ec.ok) {
        s.blockers.push_back(Blocker::EcReadInvalid);
        s.blockerDetails.push_back(s.ec.error.empty() ? "EC read failed" : s.ec.error);
      } else {
        std::string why;
        if (!ValidateEc(s.ec, why)) {
          s.blockers.push_back(Blocker::EcStateUnexpected);
          s.blockerDetails.push_back(why);
        }
      }
    }
  }

  // NVIDIA (kimlik uyuşmasa bile yalnız göstermek için okunur).
  if (EnsureNvml(s)) {
    nv_.ReadDynamic(s.nv);
    if (!s.nv.ok) {
      s.blockers.push_back(Blocker::NvmlUnavailable);
      s.blockerDetails.push_back("NVML power limit unreadable: " + s.nv.error + " (mgmtRc=" + std::to_string(s.nv.mgmtRc) +
                                 " enforcedRc=" + std::to_string(s.nv.enforcedRc) + ")");
      // GPU sıfırlama/geçersiz tanıtıcı sonrası kalıcı takılmayı önle: bir sonraki okuma NVML'yi yeniden başlatır.
      nv_.Shutdown();
      nvInit_ = false;
      nvStaticDone_ = false;
    }
    // "RTX 3070 Ti Laptop" gibi kardeş modeller de "3070"+"LAPTOP" içerir; açıkça reddedilir.
    if (!ContainsNoCase(s.nvStatic.name, kExpectedGpuNameA) || !ContainsNoCase(s.nvStatic.name, kExpectedGpuNameB) ||
        ContainsNoCase(s.nvStatic.name, "3070 TI")) {
      s.blockers.push_back(Blocker::GpuMismatch);
      s.blockerDetails.push_back("GPU is not RTX 3070 Laptop: '" + s.nvStatic.name + "'");
    }
    if (std::fabs(s.nvStatic.maxW - kVbiosMaxWatts) > kNvToleranceW) {
      s.blockers.push_back(Blocker::MaxLimitMismatch);
      s.blockerDetails.push_back("NVIDIA max power limit " + Num1(s.nvStatic.maxW) + " W != expected " +
                                 std::to_string(kVbiosMaxWatts) + " W");
    }
  }

  if (s.ec.ok) s.ecProfile = ProfileFromEc(s.ec.ctrl, s.ec.offset);
  if (s.ecProfile != 0 && s.nv.ok && std::fabs(s.nv.limitW - s.ecProfile) <= kNvToleranceW) {
    s.consistent = true;
    s.activeProfile = s.ecProfile;
  }

  // Doğrulanamayan geri alma sonrası kilit: kullanıcı elle "Yenile" ile durumu yeniden okuyup EC/NVIDIA temiz
  // ve tutarlı çıkana kadar (ya da uygulama yeniden başlayana kadar) hiçbir yazma yapılmaz.
  if (faultLatched_) {
    if (clearFaultIfClean && !light && s.blockers.empty() && s.consistent) {
      faultLatched_ = false;
      L(LogLevel::Warn, "Fault latch cleared: manual full re-read found a valid, consistent state (" +
                            std::to_string(s.activeProfile) + " W)");
    } else {
      s.blockers.push_back(Blocker::FaultLatched);
      s.blockerDetails.push_back("writes disabled after an unverified rollback; press Refresh once EC and NVIDIA agree");
    }
  }
  return s;
}

bool PowerProfileEngine::WaitNvmlLimit(double targetW, NvDynamic& last, uint32_t timeoutMs) {
  const uint64_t start = clock_.NowMs();
  for (;;) {
    NvDynamic d;
    nv_.ReadDynamic(d);
    last = d;
    if (d.ok && std::fabs(d.limitW - targetW) <= kNvToleranceW) return true;
    if (clock_.NowMs() - start >= timeoutMs) return false;
    clock_.SleepMs(policy_.verifyPollMs);
  }
}

bool PowerProfileEngine::ExecutePlan(const std::vector<WriteStep>& plan, std::vector<WriteStep>* performed,
                                     std::string& err, const EcSnapshot* start) {
  // Geçiş güvenliği: bilinen başlangıç durumundan yola çıkarak her adımın SONUCUNU önceden hesaplar.
  //  - cTGP bitli kapalı + sıfırdan farklı ofset (tutarsız) çifti hiç üretilmez,
  //  - ara durum, başlangıç ve hedef watt değerinden yükseğine çıkmaz.
  // (Tek hata yolu: planı üreten kodda hata; o durumda hiçbir şey yazılmadan reddedilir.)
  if (start && start->ok) {
    uint8_t c = start->ctrl, o = start->offset;
    uint8_t fc = c, fo = o;
    for (const WriteStep& st : plan) {
      if (st.addr == kEcAddrCtrl) fc = st.value;
      else if (st.addr == kEcAddrCtgpOffset) fo = st.value;
    }
    const int limitW = WattsOfPair(c, o) > WattsOfPair(fc, fo) ? WattsOfPair(c, o) : WattsOfPair(fc, fo);
    for (const WriteStep& st : plan) {
      if (st.addr == kEcAddrCtrl) c = st.value;
      else if (st.addr == kEcAddrCtgpOffset) o = st.value;
      if ((!(c & kCtrlCtgp) && o != 0) || WattsOfPair(c, o) > limitW) {
        err = "plan rejected before any write: intermediate EC state (" + Hex2(c) + "," + std::to_string(o) +
              ") is unsafe";
        L(LogLevel::Error, err);
        return false;
      }
    }
  }
  bool allOk = true;
  for (const WriteStep& st : plan) {
    if (!IsAllowedEcWrite(st.addr, st.value)) {
      err = "write step not in allow-list: " + Hex4(st.addr) + "=" + std::to_string(st.value);
      L(LogLevel::Error, err);
      return false;
    }
    EcByte w = ec_.Write(st.addr, st.value);
    if (performed) performed->push_back(st);
    if (!w.ok) {
      err = "WriteEC " + Hex4(st.addr) + " failed: " + w.error;
      L(LogLevel::Error, err);
      allOk = false;
      break;
    }
    clock_.SleepMs(policy_.stepSettleMs);
    EcByte rb = ec_.Read(st.addr);
    L(LogLevel::Info, "EC[" + Hex4(st.addr) + "] <- " + std::to_string(st.value) + " readback=" +
                          (rb.ok ? std::to_string(rb.value) : std::string("FAIL")));
    if (!rb.ok || rb.value != st.value) {
      err = "read-back mismatch at " + Hex4(st.addr) + ": wrote " + std::to_string(st.value) + ", read " +
            (rb.ok ? std::to_string(rb.value) : std::string("failure"));
      L(LogLevel::Error, err);
      allOk = false;
      break;
    }
  }
  return allOk;
}

void PowerProfileEngine::Rollback(const EcSnapshot& pre, ApplyResult& r) {
  // En çok iki geçiş: her geçiş önce EC'nin GERÇEK anlık durumunu okur, planı ondan üretir ve ilk hatada durur
  // (bağımlı ikinci adım, ilki başarısızken asla yazılmaz). İkinci geçiş yeni okunan durumdan yeniden planlar.
  for (int pass = 0; pass < 2; ++pass) {
    EcSnapshot cur = ReadEcSnapshot(false);
    std::vector<WriteStep> plan;
    if (cur.ok) {
      if (cur.ctrl == pre.ctrl && cur.offset == pre.offset) break;  // zaten önceki durumda
      plan = PlanRaw(cur.ctrl, cur.offset, pre.ctrl, pre.offset);
    } else {
      // Mevcut durum okunamıyor: önceki değerleri güvenli sırayla "kör" geri yaz.
      if (pre.ctrl & kCtrlCtgp) plan = {{kEcAddrCtgpOffset, 0}, {kEcAddrCtrl, pre.ctrl}, {kEcAddrCtgpOffset, pre.offset}};
      else plan = {{kEcAddrCtgpOffset, pre.offset}, {kEcAddrCtrl, pre.ctrl}};
      L(LogLevel::Warn, "Rollback: current EC state unreadable, blind restore");
    }
    r.rolledBack = true;
    L(LogLevel::Warn, "Rollback pass " + std::to_string(pass + 1) + ": restoring EC ctrl=" + Hex2(pre.ctrl) +
                          " offset=" + std::to_string(pre.offset));
    std::string e;
    if (ExecutePlan(plan, nullptr, e, cur.ok ? &cur : nullptr)) break;
  }
  EcSnapshot after = ReadEcSnapshot(false);
  const bool ecOk = after.ok && after.ctrl == pre.ctrl && after.offset == pre.offset;
  // NVML, "önceki NVML değerine" değil, önceki EC durumunun türettiği watt değerine dönmelidir.
  const int nvTarget = ProfileFromEc(pre.ctrl, pre.offset);
  bool nvOk = true;
  if (nvTarget != 0) {
    NvDynamic last;
    nvOk = WaitNvmlLimit(static_cast<double>(nvTarget), last, policy_.rollbackVerifyTimeoutMs);
    if (!nvOk) L(LogLevel::Warn, "Rollback: NVML did not return to " + Num1(nvTarget) + " W (now " + Num1(last.limitW) + " W)");
  }
  r.rollbackVerified = ecOk && nvOk;
  L(r.rollbackVerified ? LogLevel::Info : LogLevel::Error,
    std::string("Rollback ") + (r.rollbackVerified ? "VERIFIED" : "NOT VERIFIED") + " ec=" + (ecOk ? "ok" : "bad") +
        " nvml=" + (nvOk ? "ok" : "bad"));
}

ApplyResult PowerProfileEngine::FailWithRollback(const EcSnapshot& pre, ApplyResult r, const std::string& why) {
  r.detail = why;
  L(LogLevel::Error, "Apply " + std::to_string(r.target) + " W FAILED: " + why);
  Rollback(pre, r);
  if (r.rollbackVerified) r.outcome = r.rolledBack ? Outcome::FailedRolledBack : Outcome::FailedNoChange;
  else r.outcome = Outcome::FailedRollbackUnverified;
  if (r.outcome == Outcome::FailedRollbackUnverified) {
    faultLatched_ = true;
    L(LogLevel::Error, "FAULT LATCHED: rollback could not be verified; further writes disabled");
  }
  r.after = ReadLive(false);
  return r;
}

// ------------------------------------------------------------------ uygula
ApplyResult PowerProfileEngine::Apply(int watts, const ApplyOptions& opts) {
  ApplyResult r;
  r.target = watts;
  EcSnapshot pre;
  bool havePre = false;
  try {
    return ApplyInner(watts, opts, r, pre, havePre);
  } catch (const std::exception& ex) {
    return AbortAfterException(r, pre, havePre, ex.what());
  } catch (...) {
    return AbortAfterException(r, pre, havePre, "unknown exception");
  }
}

ApplyResult PowerProfileEngine::AbortAfterException(ApplyResult& r, const EcSnapshot& pre, bool havePre, const char* what) {
  r.detail = std::string("internal exception during apply: ") + what;
  try { L(LogLevel::Error, r.detail); } catch (...) {}
  if (r.performed.empty() || !havePre) {
    r.outcome = Outcome::BlockedNotReady;
    return r;
  }
  try {
    Rollback(pre, r);
    r.outcome = r.rollbackVerified ? Outcome::FailedRolledBack : Outcome::FailedRollbackUnverified;
  } catch (...) {
    r.rollbackVerified = false;
    r.outcome = Outcome::FailedRollbackUnverified;
  }
  if (r.outcome == Outcome::FailedRollbackUnverified) faultLatched_ = true;
  return r;
}

ApplyResult PowerProfileEngine::ApplyInner(int watts, const ApplyOptions& opts, ApplyResult& r, EcSnapshot& preEc,
                                           bool& havePre) {
  const ProfileDef* def = FindProfile(watts);
  if (!def) {
    r.outcome = Outcome::BlockedUnknownProfile;
    r.detail = "unknown profile " + std::to_string(watts);
    L(LogLevel::Error, r.detail);
    return r;
  }

  LiveState pre = ReadLive(false);
  r.before = pre;
  r.after = pre;
  if (!pre.CanWrite()) {
    r.outcome = Outcome::BlockedNotReady;
    r.detail = Join(pre.blockerDetails);
    L(LogLevel::Warn, "Apply " + std::to_string(watts) + " W blocked: " + r.detail);
    return r;
  }
  if (!pre.identity.boardConfirmed && !opts.consentModel) {
    r.outcome = Outcome::BlockedConsent;
    r.detail = "SMBIOS has no GM5TG7Y board tag; user must confirm this device is a Monster Tulpar T5 V21.7 (GM5TG7Y)";
    L(LogLevel::Warn, r.detail);
    return r;
  }
  if (watts > kOemMaxWatts && !opts.consentOverOem) {
    r.outcome = Outcome::BlockedConsent;
    r.detail = "above-OEM profile requires explicit user consent";
    return r;
  }
  if (def->evidence == Evidence::ExpectedUnverified && !opts.consentUnverified) {
    r.outcome = Outcome::BlockedConsent;
    r.detail = "profile mapping is not live-verified; requires explicit user consent";
    return r;
  }
  if (static_cast<double>(watts) > pre.nvStatic.maxW + kNvToleranceW) {
    r.outcome = Outcome::BlockedAboveVbiosMax;
    r.detail = std::to_string(watts) + " W exceeds NVIDIA max " + Num1(pre.nvStatic.maxW) + " W";
    return r;
  }
  // Sıcaklık kapısı, ölçüm yoksa da KAPALI kalır (fail-closed): okunamayan sıcaklıkta OEM üzerine çıkılmaz.
  if (watts > kOemMaxWatts && watts > pre.ecProfile) {
    if (!pre.nv.haveTemp) {
      r.outcome = Outcome::BlockedTemp;
      r.detail = "GPU temperature is unreadable; refusing to raise above OEM profile";
      L(LogLevel::Warn, r.detail);
      return r;
    }
    if (pre.nv.tempC >= kMaxTempToRaiseC) {
      r.outcome = Outcome::BlockedTemp;
      r.detail = "GPU core is " + std::to_string(pre.nv.tempC) + " C (>= " + std::to_string(kMaxTempToRaiseC) +
                 " C); refusing to raise above OEM profile";
      L(LogLevel::Warn, r.detail);
      return r;
    }
  }

  // EC ve NVIDIA başlangıçta uyuşmuyorsa (örn. önceki değişiklik NVIDIA'ya henüz yansımadı) yazmadan önce
  // NVIDIA'nın EC durumunu izlemesi beklenir; izlemiyorsa hiçbir şey yazılmaz. Böylece "doğrulama" asla
  // yazmadan önce zaten doğru görünen bayat bir NVML değerine dayanmaz ve geri alma hedefi gerçek olur.
  if (!pre.consistent) {
    NvDynamic d;
    L(LogLevel::Warn, "Apply " + std::to_string(watts) + " W: EC says " + std::to_string(pre.ecProfile) +
                          " W but NVIDIA reads " + Num1(pre.nv.limitW) + " W; waiting for NVIDIA to follow");
    if (!WaitNvmlLimit(static_cast<double>(pre.ecProfile), d, policy_.verifyTimeoutMs)) {
      r.outcome = Outcome::BlockedNotReady;
      r.detail = "EC state (" + std::to_string(pre.ecProfile) + " W) and NVIDIA limit (" + Num1(d.ok ? d.limitW : pre.nv.limitW) +
                 " W) disagree before the write; not writing";
      L(LogLevel::Warn, r.detail);
      return r;
    }
    pre = ReadLive(false);
    r.before = pre;
    r.after = pre;
    if (!pre.CanWrite() || !pre.consistent) {
      r.outcome = Outcome::BlockedNotReady;
      r.detail = pre.CanWrite() ? "EC and NVIDIA still disagree after waiting; not writing" : Join(pre.blockerDetails);
      L(LogLevel::Warn, r.detail);
      return r;
    }
  }

  if (pre.ec.ctrl == def->ctrl && pre.ec.offset == def->offset && pre.nv.ok &&
      std::fabs(pre.nv.limitW - watts) <= kNvToleranceW) {
    r.outcome = Outcome::AlreadyActive;
    r.ecVerified = r.nvVerified = true;
    r.detail = "already active";
    L(LogLevel::Info, "Apply " + std::to_string(watts) + " W: already active (EC and NVIDIA agree), no write");
    return r;
  }

  // Yazmadan hemen önce EC'yi yeniden oku: plan ve geri alma hedefi, ilk yazmadan önceki GERÇEK durum olmalı.
  // Okuma ile yazma arasında başka bir yazılım değiştirdiyse hiçbir şey yazılmadan reddedilir.
  EcSnapshot fresh = ReadEcSnapshot(false);
  if (!fresh.ok || fresh.ctrl != pre.ec.ctrl || fresh.offset != pre.ec.offset || fresh.tpp != pre.ec.tpp ||
      fresh.byte746 != pre.ec.byte746) {
    r.outcome = Outcome::FailedNoChange;
    r.detail = fresh.ok ? "EC state changed between the pre-check and the first write (now ctrl=" + Hex2(fresh.ctrl) +
                              " off=" + std::to_string(fresh.offset) + "); nothing was written"
                        : "EC re-read before the first write failed (" + fresh.error + "); nothing was written";
    L(LogLevel::Warn, r.detail);
    r.after = ReadLive(false);
    return r;
  }
  preEc = fresh;
  havePre = true;

  const std::vector<WriteStep> plan = PlanRaw(fresh.ctrl, fresh.offset, def->ctrl, def->offset);
  L(LogLevel::Info, "Apply " + std::to_string(watts) + " W: from ctrl=" + Hex2(fresh.ctrl) + " off=" +
                        std::to_string(fresh.offset) + " nvidia=" + Num1(pre.nv.limitW) + " W, " +
                        std::to_string(plan.size()) + " write(s)");

  std::string err;
  if (!ExecutePlan(plan, &r.performed, err, &fresh)) return FailWithRollback(fresh, r, err);

  EcSnapshot e1 = ReadEcSnapshot(false);
  if (!e1.ok || e1.ctrl != def->ctrl || e1.offset != def->offset || e1.tpp != 0 || e1.byte746 != 0) {
    return FailWithRollback(fresh, r,
                            "EC state after write does not match target (ctrl=" + Hex2(e1.ctrl) + " off=" +
                                std::to_string(e1.offset) + ")");
  }
  r.ecVerified = true;

  NvDynamic nvLast;
  if (!WaitNvmlLimit(static_cast<double>(watts), nvLast, policy_.verifyTimeoutMs)) {
    return FailWithRollback(fresh, r,
                            "NVIDIA power limit did not reach " + std::to_string(watts) + " W (last " +
                                (nvLast.ok ? Num1(nvLast.limitW) + " W" : std::string("unreadable")) + ")");
  }
  r.nvVerified = true;

  // Son teyit: bir süre sonra EC ve NVIDIA hâlâ hedefte mi (başka yazılım geri aldı mı)?
  clock_.SleepMs(policy_.finalConfirmDelayMs);
  EcSnapshot e2 = ReadEcSnapshot(false);
  NvDynamic n2;
  nv_.ReadDynamic(n2);
  if (!e2.ok || e2.ctrl != def->ctrl || e2.offset != def->offset || !n2.ok ||
      std::fabs(n2.limitW - watts) > kNvToleranceW) {
    return FailWithRollback(fresh, r,
                            "state changed during final confirmation (external override?): ec ctrl=" +
                                Hex2(e2.ctrl) + " off=" + std::to_string(e2.offset) + " nvidia=" + Num1(n2.limitW) + " W");
  }

  r.outcome = Outcome::Ok;
  r.detail = "verified at EC and NVIDIA level";
  r.after = ReadLive(false);
  L(LogLevel::Info, "Apply " + std::to_string(watts) + " W OK: EC ctrl=" + Hex2(e2.ctrl) + " off=" +
                        std::to_string(e2.offset) + ", NVIDIA " + Num1(n2.limitW) + " W");
  return r;
}

// ------------------------------------------------------------------ OEM kaydı başarısız olunca canlı durumu geri alma
ApplyResult PowerProfileEngine::RestoreLive(const LiveState& before, const std::string& why) {
  ApplyResult r;
  r.target = before.activeProfile ? before.activeProfile : before.ecProfile;
  try {
    r.before = ReadLive(false);
    r.after = r.before;
    if (!before.ec.ok) {
      r.outcome = Outcome::BlockedNotReady;
      r.detail = "the pre-apply EC state is unknown; the live state cannot be restored automatically";
      L(LogLevel::Error, "RestoreLive refused: " + r.detail);
      return r;
    }
    L(LogLevel::Warn, "RestoreLive: returning to the pre-apply state (ctrl=" + Hex2(before.ec.ctrl) + " off=" + std::to_string(before.ec.offset) +
                          ") because: " + why);
    Rollback(before.ec, r);
    r.outcome = r.rollbackVerified ? (r.rolledBack ? Outcome::FailedRolledBack : Outcome::FailedNoChange) : Outcome::FailedRollbackUnverified;
    r.detail = why;
    if (r.outcome == Outcome::FailedRollbackUnverified) {
      faultLatched_ = true;
      L(LogLevel::Error, "FAULT LATCHED: the live state could not be restored and verified after the OEM write failure; further writes disabled");
    }
    r.after = ReadLive(false);
  } catch (const std::exception& ex) {
    r.outcome = Outcome::FailedRollbackUnverified;
    r.rollbackVerified = false;
    r.detail = std::string("internal exception during RestoreLive: ") + ex.what();
    faultLatched_ = true;
    try { L(LogLevel::Error, r.detail); } catch (...) {}
  } catch (...) {
    r.outcome = Outcome::FailedRollbackUnverified;
    r.rollbackVerified = false;
    r.detail = "unknown exception during RestoreLive";
    faultLatched_ = true;
  }
  return r;
}


}  // namespace mgp
