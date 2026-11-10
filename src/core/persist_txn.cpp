#include "persist_txn.h"

#include <exception>

#include "ec_constants.h"

namespace mgp {

ForeignKind ClassifyForeign(const oem::Inspection& pre, const OemOwnership& own, int targetWatts) {
  if (!pre.ok || !pre.haveFile) return ForeignKind::None;     // modül kilitli: OEM dosyasına zaten yazılmaz
  if (pre.target == targetWatts) return ForeignKind::None;    // hedef zaten bu değer: dosyaya yazılmaz
  if (pre.target == kOemMaxWatts) return ForeignKind::None;   // OEM varsayılanı: kimsenin özelleştirmesi değil
  if (own.Present() && !pre.sha.empty() && pre.sha == own.ownedSha) return ForeignKind::None;  // bu uygulamanın bıraktığı sürüm
  return own.Present() ? ForeignKind::ChangedAfterOurs : ForeignKind::UnownedValue;
}

PersistTxnResult RunPersistTransaction(PowerProfileEngine& engine, oem::OemProfileStore& store, ILog& log, const PersistTxnInput& in) {
  PersistTxnResult res;
  res.live.target = in.watts;

  try {
    res.pre = store.Inspect();  // salt okunur ön kontrol: hiçbir şey yazılmaz
  } catch (const std::exception& e) {
    res.pre = oem::Inspection();
    res.pre.ok = false;
    res.pre.lock = oem::Lock::ProfileIo;
    res.pre.detail = std::string("internal exception during the OEM pre-check: ") + e.what();
  } catch (...) {
    res.pre = oem::Inspection();
    res.pre.ok = false;
    res.pre.lock = oem::Lock::ProfileIo;
    res.pre.detail = "internal exception during the OEM pre-check";
  }
  if (!in.approvedSha.empty() && res.pre.haveFile && res.pre.sha != in.approvedSha) {
    res.aborted = true;
    res.live.outcome = Outcome::BlockedNotReady;
    res.live.detail = "the OEM profile changed after the approval dialog; nothing was applied";
    log.Write(LogLevel::Warn, "persist txn aborted: " + res.live.detail);
    return res;
  }
  if (!res.pre.ok) {
    // Kalıcılık sağlanamıyorsa geçici (yalnız anlık) uygulama da yapılmaz.
    res.oemUnavailable = true;
    res.live.outcome = Outcome::BlockedNotReady;
    res.live.detail = std::string("the OEM profile module is unavailable (") + oem::LockName(res.pre.lock) + "): " + res.pre.detail + "; nothing was applied";
    log.Write(LogLevel::Warn, "persist txn refused: " + res.live.detail);
    return res;
  }
  // Yabancı değer kapısı: karar, kilitler altında ve GERÇEK dosya okumasıyla burada verilir (arayüzün eski görüntüsüne güvenilmez).
  res.foreign = ClassifyForeign(res.pre, in.own, in.watts);
  if (res.foreign != ForeignKind::None && !(in.foreignApproved && !in.approvedSha.empty() && in.approvedSha == res.pre.sha)) {
    res.needsApproval = true;
    res.live.outcome = Outcome::BlockedNotReady;
    res.live.detail = "the OEM profile holds a value this application did not write; the user must approve overwriting it first; nothing was applied";
    log.Write(LogLevel::Warn, "persist txn needs approval: OEM target " + std::to_string(res.pre.target) + " W is " +
                                  (res.foreign == ForeignKind::UnownedValue ? "not owned by this application" : "different from the version this application left") +
                                  "; requested " + std::to_string(in.watts) + " W; nothing applied");
    return res;
  }

  res.live = engine.Apply(in.watts, in.opts);
  if (!res.live.Succeeded()) return res;  // anlık uygulama başarısız -> OEM dosyasına DOKUNULMAZ

  res.persistAttempted = true;
  try {
    res.oem = store.Commit(in.watts, res.pre.sha);
  } catch (const std::exception& e) {
    res.oem = oem::CommitResult();
    res.oem.kind = oem::CommitKind::FailedIndeterminate;
    res.oem.toWatts = in.watts;
    res.oem.detail = std::string("internal exception during the OEM commit: ") + e.what();
    log.Write(LogLevel::Error, res.oem.detail);
  } catch (...) {
    res.oem = oem::CommitResult();
    res.oem.kind = oem::CommitKind::FailedIndeterminate;
    res.oem.toWatts = in.watts;
    res.oem.detail = "internal exception during the OEM commit";
    log.Write(LogLevel::Error, res.oem.detail);
  }
  if (res.oem.Success()) return res;

  // OEM yazımı başarısız. Canlı durum gerçekten DEĞİŞTİYSE (Ok; AlreadyActive'de yazma olmadı) önceki duruma dönmeyi dene.
  if (res.live.outcome == Outcome::Ok && res.live.before.ec.ok) {
    res.liveRestoreAttempted = true;
    res.liveRestore = engine.RestoreLive(res.live.before, std::string("OEM profile write did not succeed: ") + res.oem.detail);
  }
  return res;
}

TxnClass ClassifyTxn(const PersistTxnResult& t) {
  if (t.needsApproval) return TxnClass::NeedsApproval;
  if (t.aborted) return TxnClass::Aborted;
  if (t.oemUnavailable) return TxnClass::OemUnavailable;
  if (!t.live.Succeeded()) return TxnClass::LiveFailed;
  if (t.oem.Success()) return t.oem.kind == oem::CommitKind::AlreadyCurrent ? TxnClass::BothOkAlready : TxnClass::BothOk;
  if (t.liveRestoreAttempted) {
    // Yalnız DOĞRULANMIŞ dönüş (geri alındı ya da zaten eski değerdeydi) "geri alındı" sayılır; başka her sonuç doğrulanamamış demektir.
    const Outcome o = t.liveRestore.outcome;
    return (o == Outcome::FailedRolledBack || o == Outcome::FailedNoChange) ? TxnClass::OemFailedLiveRestored
                                                                             : TxnClass::OemFailedRestoreUnverified;
  }
  return TxnClass::OemFailedLiveUnchanged;
}

OemOwnership OwnershipFromTxn(const oem::Inspection& after, const PersistTxnResult& t) {
  OemOwnership o;
  if (!t.persistAttempted || !t.oem.Success()) return o;
  if (!after.ok || !after.haveFile || after.sha.empty() || t.oem.shaAfter.empty() || after.sha != t.oem.shaAfter) return o;
  if (!FindProfile(after.target)) return o;
  o.ownedSha = after.sha;
  o.ownedWatts = after.target;
  return o;
}

}  // namespace mgp
