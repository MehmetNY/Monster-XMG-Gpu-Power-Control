#include "oem_profile.h"


#include "ec_constants.h"
#include "sha256.h"

namespace mgp {
namespace oem {
namespace {

std::string Short(const std::string& sha) { return sha.size() > 12 ? sha.substr(0, 12) : sha; }

bool Lk(Inspection& i, Lock l, const std::string& why) {
  i.ok = false;
  i.lock = l;
  i.detail = why;
  return false;
}

std::string W(int w) { return std::to_string(w) + " W"; }

}  // namespace

const char* LockName(Lock l) {
  switch (l) {
    case Lock::None: return "none";
    case Lock::NotAdmin: return "not-admin";
    case Lock::ServiceNotRunning: return "service-not-running";
    case Lock::ServicePath: return "service-path";
    case Lock::ServiceVersion: return "service-version";
    case Lock::DirUnsafe: return "profile-dir-unsafe";
    case Lock::MainOptionIo: return "mainoption-io";
    case Lock::MainOptionFormat: return "mainoption-format";
    case Lock::ProfileNotActive: return "profile-not-active";
    case Lock::ProfileIo: return "profile-io";
    case Lock::ProfileEncoding: return "profile-encoding";
    case Lock::ProfileFormat: return "profile-format";
    case Lock::ProfileNotActivated: return "profile-not-activated";
    case Lock::SwitchOff: return "tgp-switch-off";
    case Lock::DynamicBoostOn: return "dynamic-boost-on";
    case Lock::TargetUnsupported: return "target-unsupported";
    case Lock::Changed: return "changed-by-other";
  }
  return "?";
}

// ---------------------------------------------------------------------------------------------------- okuma
bool OemProfileStore::Load(Loaded& ld) {
  ld = Loaded();
  Inspection& i = ld.insp;
  i.svc = p_.ProbeService();
  if (!i.svc.elevated) return Lk(i, Lock::NotAdmin, "the application is not running with administrator rights");
  if (!i.svc.running) return Lk(i, Lock::ServiceNotRunning, "GCUService.exe is not running");
  if (!i.svc.pathMatches) return Lk(i, Lock::ServicePath, "the running GCUService.exe is not at the expected OEM path");
  if (!i.svc.versionRead || i.svc.version != kExpectedGcuVersion)
    return Lk(i, Lock::ServiceVersion,
              std::string("GCUService version is '") + (i.svc.versionRead ? i.svc.version : std::string("unreadable")) + "'; only " + kExpectedGcuVersion + " is supported");
  if (!i.svc.dirSafe) return Lk(i, Lock::DirUnsafe, "OEM profile directory is not usable: " + i.svc.detail);

  FileData m = p_.ReadFile(kMainOptionLeaf);
  if (!m.io.ok()) return Lk(i, Lock::MainOptionIo, std::string(kMainOptionLeaf) + ": " + m.io.msg);
  const MainOptionFacts mf = ParseMainOption(m.bytes);
  if (!mf.ok) return Lk(i, Lock::MainOptionFormat, std::string(kMainOptionLeaf) + ": " + mf.detail);
  if (mf.operatingMode != kExpectedOperatingMode || mf.turboProfileIndex != kExpectedTurboProfileIndex)
    return Lk(i, Lock::ProfileNotActive,
              "OperatingMode=" + std::to_string(mf.operatingMode) + " TurboProfileIndex=" + std::to_string(mf.turboProfileIndex) +
                  "; Mode3_Profile1 (Turbo profile 1) is not the active OEM profile");

  FileData f = p_.ReadFile(kProfileLeaf);
  if (!f.io.ok()) return Lk(i, Lock::ProfileIo, std::string(kProfileLeaf) + ": " + f.io.msg);
  ld.bytes = std::move(f.bytes);
  i.haveFile = true;
  i.size = ld.bytes.size();
  i.stamp = f.stamp;
  i.sha = Sha256Hex(ld.bytes);
  ld.facts = ParseProfile(ld.bytes);
  const ProfileFacts& pf = ld.facts;
  if (!pf.ok)
    return Lk(i, pf.defect == Defect::Envelope ? Lock::ProfileEncoding : Lock::ProfileFormat, std::string(kProfileLeaf) + ": " + pf.detail);
  i.target = pf.target;
  if (pf.activated != 1) return Lk(i, Lock::ProfileNotActivated, "Activated is not true");
  if (pf.tgpSwitch != 1) return Lk(i, Lock::SwitchOff, "GPU.ConfigurableTGPSwitch is not 1");
  if (pf.dynBoostSwitch != 0) return Lk(i, Lock::DynamicBoostOn, "GPU.DynamicBoostSwitch is not 0 (unknown power configuration)");
  if (!FindProfile(pf.target)) return Lk(i, Lock::TargetUnsupported, "GPU.ConfigurableTGPTarget=" + std::to_string(pf.target) + " is not one of 125/130/135/140/145/150");
  i.ok = true;
  i.lock = Lock::None;
  i.detail.clear();
  return true;
}

Inspection OemProfileStore::Inspect() {
  Loaded ld;
  Load(ld);
  return ld.insp;
}

// ---------------------------------------------------------------------------------------------------- yazma
CommitResult OemProfileStore::Commit(int watts, const std::string& expectSha) {
  CommitResult r;
  r.toWatts = watts;
  const std::string tag = "OEM persist";
  auto refuse = [&](Lock lk, const std::string& why) {
    r.kind = CommitKind::Refused;
    r.lock = lk;
    r.detail = why;
    Log(LogLevel::Warn, tag + " refused (" + LockName(lk) + "): " + why);
    return r;
  };
  auto noChange = [&](const std::string& why) {
    r.kind = CommitKind::FailedNoChange;
    r.detail = why;
    Log(LogLevel::Error, tag + " failed, OEM profile unchanged: " + why);
    return r;
  };
  auto indeterminate = [&](const std::string& why) {
    r.kind = CommitKind::FailedIndeterminate;
    r.detail = why;
    Log(LogLevel::Error, tag + " INDETERMINATE: " + why);
    return r;
  };

  if (!FindProfile(watts)) return refuse(Lock::TargetUnsupported, W(watts) + " is not a supported profile");

  Loaded a;
  if (!Load(a)) return refuse(a.insp.lock, a.insp.detail);
  r.fromWatts = a.insp.target;
  r.shaBefore = a.insp.sha;
  if (!expectSha.empty() && a.insp.sha != expectSha)
    return refuse(Lock::Changed, "the OEM profile is not the version that was inspected/approved (sha " + Short(a.insp.sha) + " != " + Short(expectSha) + ")");

  if (a.insp.target == watts) {
    r.kind = CommitKind::AlreadyCurrent;
    r.jsonVerified = true;
    r.shaAfter = a.insp.sha;
    r.detail = "OEM profile is already " + W(watts) + ", verified";
    Log(LogLevel::Info, tag + ": OEM profile zaten " + W(watts) + ", dogrulandi (already " + W(watts) + ", verified; no write, no new backup) sha=" + Short(a.insp.sha));
    return r;
  }

  std::vector<uint8_t> patched;
  std::string perr;
  if (!PatchTarget(a.bytes, watts, patched, perr)) return refuse(Lock::ProfileFormat, perr);

  const std::string stamp = p_.StampNow(), id = p_.RandomId8();
  if (!IsStamp(stamp) || !IsHex8(id)) return noChange("internal: platform returned an invalid timestamp/id for the backup name");
  const std::string bakName = MakeBackupName(stamp, id), tmpName = MakeTempName(kTmpInfix, id), swapName = MakeTempName(kSwapInfix, id),
                    rejName = MakeTempName(kRejectedInfix, id);
  Log(LogLevel::Info, tag + " begin: " + W(a.insp.target) + " -> " + W(watts) + " sha0=" + Short(a.insp.sha) + " backup=" + bakName);

  auto dropOwn = [&](const std::string& leaf) {
    const Io x = p_.RemoveOwn(leaf);
    if (!x.ok() && x.code != IoCode::NotFound) r.leftovers.push_back(leaf);
  };

  // (2) doğrulanmış tam yedek: ÖNCE yedek, sonra değişiklik.
  Io io = p_.CreateNew(bakName, a.bytes, true);
  if (!io.ok()) return noChange("the backup file could not be created (" + io.msg + "); nothing was changed");
  {
    const FileData bk = p_.ReadFile(bakName);
    if (!bk.io.ok() || bk.bytes != a.bytes) {
      r.leftovers.push_back(bakName);  // kendi oluşturduğumuz ama doğrulanamayan dosya: silinmez, raporlanır
      return noChange("the backup could not be verified by reading it back; nothing was changed");
    }
  }
  r.backupName = bakName;
  r.backupSha = a.insp.sha;
  Log(LogLevel::Info, tag + ": original backed up and verified: " + bakName);

  // (3) geçici dosya: yaz, flush, geri oku, çözümle.
  io = p_.CreateNew(tmpName, patched, false);
  if (!io.ok()) return noChange("the temporary file could not be created (" + io.msg + "); nothing was changed");
  {
    const FileData t = p_.ReadFile(tmpName);
    if (!t.io.ok() || t.bytes != patched) {
      dropOwn(tmpName);
      return noChange("the temporary file failed read-back verification; nothing was changed");
    }
    const ProfileFacts tf = ParseProfile(t.bytes);
    if (!tf.ok || tf.target != watts) {
      dropOwn(tmpName);
      return noChange("the temporary file failed re-parse verification; nothing was changed");
    }
  }

  // (4) TOCTOU: tüm ön koşullar + aktif dosyanın kimliği değişmedi mi?
  {
    Loaded b;
    const bool ok = Load(b);
    if (!ok || b.insp.sha != a.insp.sha || !SameStamp(b.insp.stamp, a.insp.stamp) || b.insp.target != a.insp.target) {
      dropOwn(tmpName);
      return refuse(ok ? Lock::Changed : b.insp.lock,
                    ok ? "the OEM profile changed between the pre-check and the commit (another writer); nothing was written"
                       : "a precondition no longer holds right before the commit: " + b.insp.detail);
    }
  }

  // (5) atomik değiştirme; eski içerik swap yedeğine taşınır.
  const Io rp = p_.AtomicReplace(kProfileLeaf, tmpName, swapName);
  if (!rp.ok()) {
    dropOwn(tmpName);
    const FileData cur = p_.ReadFile(kProfileLeaf);
    if (cur.io.ok() && cur.bytes == a.bytes) return noChange("ReplaceFile failed (" + rp.msg + "); the OEM profile is unchanged");
    if (cur.io.code == IoCode::NotFound) {
      // ReplaceFileW'nin "değiştirilen dosya asıl adında artık yok" penceresi: doğrulanmış özgün baytlardan yeniden oluştur.
      const Io rc = p_.CreateNew(kProfileLeaf, a.bytes, false);
      const FileData chk = p_.ReadFile(kProfileLeaf);
      if (rc.ok() && chk.io.ok() && chk.bytes == a.bytes) {
        r.leftovers.push_back(swapName);
        return noChange("ReplaceFile failed (" + rp.msg + ") and the active file was missing; it was re-created from the verified original bytes");
      }
      return indeterminate("ReplaceFile failed (" + rp.msg + ") and the active OEM profile is missing; the verified original is saved as " + bakName);
    }
    if (cur.io.ok() && cur.bytes == patched)
      return indeterminate("ReplaceFile reported a failure (" + rp.msg + ") but the active OEM profile already contains the new content; its state around the replace cannot be proven; the verified original is saved as " +
                           bakName);
    // Dosya bizim doğruladığımız baytlardan DA, yazdığımız baytlardan DA farklı: araya başka bir yazıcı girmiştir. Biz hiçbir şey yazmadık
    // ama "dosya değişmedi" DEMEYİZ (değişti); gerçek neden "başka program" olarak bildirilir.
    if (cur.io.ok()) return refuse(Lock::Changed, "ReplaceFile failed (" + rp.msg + "); the OEM profile was changed by another program meanwhile; nothing of ours was written");
    return indeterminate("ReplaceFile failed (" + rp.msg + ") and the active file cannot be read back (" + cur.io.msg + "); the verified original is saved as " + bakName);
  }

  // (6) değiştirilen (swap) içerik, doğruladığımız özgün baytlarla AYNI mı? Değilse araya başka bir yazıcı girmiştir.
  const FileData sw = p_.ReadFile(swapName);
  const bool swapIsOriginal = sw.io.ok() && sw.bytes == a.bytes;
  if (!swapIsOriginal) {
    if (!sw.io.ok())
      return indeterminate("the replaced file cannot be read back (" + sw.io.msg + "), so what was replaced cannot be proven; the verified original is saved as " + bakName);
    const Io rv = p_.AtomicReplace(kProfileLeaf, swapName, rejName);
    const FileData back = p_.ReadFile(kProfileLeaf);
    if (rv.ok() && back.io.ok() && back.bytes == sw.bytes) {
      dropOwn(rejName);
      r.kind = CommitKind::Refused;
      r.lock = Lock::Changed;
      r.detail = "another program modified the OEM profile between the pre-check and the commit; its version was put back, nothing of ours remains";
      Log(LogLevel::Warn, tag + " refused (changed-by-other): " + r.detail);
      return r;
    }
    r.leftovers.push_back(swapName);
    return indeterminate("another program modified the OEM profile right before the commit and putting its version back failed (" + rv.msg + "); " +
                         "its version is kept as " + swapName + ", the verified original as " + bakName);
  }

  const FileData act = p_.ReadFile(kProfileLeaf);
  if (!act.io.ok() || act.bytes != patched) {
    r.leftovers.push_back(swapName);
    r.shaAfter = act.io.ok() ? Sha256Hex(act.bytes) : std::string();
    return indeterminate(act.io.ok() ? "after the replace the OEM profile does not contain exactly what was written (another program wrote it right after?); the verified original is saved as " + bakName
                                     : "after the replace the OEM profile cannot be read back (" + act.io.msg + "); the verified original is saved as " + bakName);
  }
  const ProfileFacts af = ParseProfile(act.bytes);
  if (!af.ok || af.target != watts || af.activated != 1 || af.tgpSwitch != 1 || af.dynBoostSwitch != 0) {
    r.leftovers.push_back(swapName);
    r.shaAfter = Sha256Hex(act.bytes);
    return indeterminate("after the replace the OEM profile failed schema verification; the verified original is saved as " + bakName);
  }

  r.shaAfter = Sha256Hex(act.bytes);
  r.jsonVerified = true;
  r.kind = CommitKind::Committed;
  r.detail = "OEM profile written and read back: " + W(a.insp.target) + " -> " + W(watts);
  dropOwn(swapName);  // swap, doğrulanmış yedekle bayt bayt aynıdır; yalnız bu işlemin geçici dosyasıdır
  Log(LogLevel::Info, tag + " done: " + W(a.insp.target) + " -> " + W(watts) + " sha0=" + Short(r.shaBefore) + " sha1=" + Short(r.shaAfter) +
                          " backup=" + bakName + " read-back verified");
  return r;
}

}  // namespace oem
}  // namespace mgp
