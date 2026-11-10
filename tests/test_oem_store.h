// OEM profil DEPOSU ve KALICI UYGULAMA işlem akışı testleri (her profil seçimi: OEM ön kontrol -> EC+NVML -> OEM JSON güvenli commit).
// Bellek içi SAHTE OEM platformu (tests/oem_mock.h) + sahte EC/NVML kullanılır; gerçek NTFS/ReplaceFileW/GCUService DEĞİLDİR.
// (test_main.cpp içinden dahil edilir.)
#pragma once
#include <stdexcept>
#include "../src/core/oem_profile.h"
#include "../src/core/persist_txn.h"
#include "../src/core/persist_view.h"
#include "oem_mock.h"
#include "test_oem_json.h"

namespace oemtest {

using mgp::oem::CommitKind;
using mgp::oem::CommitResult;
using mgp::oem::Inspection;
using mgp::oem::Lock;

struct StoreRig {
  mock::OemFake fake;
  mock::MemLog log;
  mgp::oem::OemProfileStore store{fake, log};
  std::vector<uint8_t> original;
  explicit StoreRig(int target = 140, const char* nl = "\n", bool bom = false) {
    fake.Put(mgp::oem::kProfileLeaf, mock::Profile(target, nl, bom));
    original = fake.Active();
  }
  std::string Bak(const char* stamp, const char* id) { return mgp::oem::MakeBackupName(stamp, id); }
};

// EC+NVML+OEM birlikte: gerçek akışın sırası (engine -> OEM) burada denenir.
struct TxnRig {
  Rig r;
  mock::OemFake fake;
  mgp::oem::OemProfileStore store{fake, r.log};
  mgp::OemOwnership own;  // ayardaki sahiplik kanıtı: arayüzün yaptığı gibi YALNIZ tam başarıdan (BothOk) sonra güncellenir
  bool trackOwnership = true;
  mgp::PersistTxnResult Run(int watts, const std::string& approved = "", bool foreignApproved = false) {
    mgp::PersistTxnInput in;
    in.watts = watts;
    in.opts = AllConsent();
    in.approvedSha = approved;
    in.own = own;
    in.foreignApproved = foreignApproved;
    mgp::PersistTxnResult res = mgp::RunPersistTransaction(r.eng, store, r.log, in);
    if (trackOwnership && mgp::ClassifyTxn(res) == mgp::TxnClass::BothOk) own = mgp::OwnershipFromTxn(store.Inspect(), res);  // BothOkAlready: kanıt aynen kalır
    return res;
  }
};

}  // namespace oemtest

// ------------------------------------------------------------------ Inspect (salt okunur)
TEST(oem_inspect_reports_facts_from_the_real_file_and_writes_nothing) {
  using namespace oemtest;
  StoreRig t;
  const Inspection i = t.store.Inspect();
  CHECK(i.ok && i.lock == Lock::None && i.haveFile && i.target == 140);
  CHECK(i.sha == mgp::Sha256Hex(t.original) && i.size == t.original.size() && i.stamp.valid);
  CHECK(i.svc.running && i.svc.pathMatches && i.svc.version == "1.0.2.70");
  CHECK(t.fake.Mutations() == 0 && t.fake.files.size() == 2);
}

TEST(oem_module_locks_on_every_unmet_precondition_and_nothing_is_ever_written) {
  using namespace oemtest;
  using namespace mgp::oem;
  struct Case { const char* name; std::function<void(mock::OemFake&)> setup; Lock want; };
  const std::vector<Case> cases = {
      {"not-admin", [](mock::OemFake& f) { f.svc.elevated = false; }, Lock::NotAdmin},
      {"service-not-running", [](mock::OemFake& f) { f.svc.running = false; }, Lock::ServiceNotRunning},
      {"service-wrong-path", [](mock::OemFake& f) { f.svc.pathMatches = false; }, Lock::ServicePath},
      {"service-version-newer", [](mock::OemFake& f) { f.svc.version = "1.0.2.71"; }, Lock::ServiceVersion},
      {"service-version-older", [](mock::OemFake& f) { f.svc.version = "1.0.2.7"; }, Lock::ServiceVersion},
      {"service-version-empty", [](mock::OemFake& f) { f.svc.version.clear(); }, Lock::ServiceVersion},
      {"service-version-unreadable", [](mock::OemFake& f) { f.svc.versionRead = false; }, Lock::ServiceVersion},
      {"dir-unsafe", [](mock::OemFake& f) { f.svc.dirSafe = false; f.svc.detail = "reparse point"; }, Lock::DirUnsafe},
      {"mainoption-missing", [](mock::OemFake& f) { f.files.erase(kMainOptionLeaf); }, Lock::MainOptionIo},
      {"mainoption-corrupt", [](mock::OemFake& f) { f.Put(kMainOptionLeaf, mock::Bytes("{ bad")); }, Lock::MainOptionFormat},
      {"mainoption-utf16", [](mock::OemFake& f) { f.Put(kMainOptionLeaf, {0xFF, 0xFE, '{', 0, '}', 0}); }, Lock::MainOptionFormat},
      {"mode-not-2", [](mock::OemFake& f) { f.Put(kMainOptionLeaf, mock::Bytes(mock::MainOptionText(1, 0))); }, Lock::ProfileNotActive},
      {"mode-0", [](mock::OemFake& f) { f.Put(kMainOptionLeaf, mock::Bytes(mock::MainOptionText(0, 0))); }, Lock::ProfileNotActive},
      {"turbo-index-1", [](mock::OemFake& f) { f.Put(kMainOptionLeaf, mock::Bytes(mock::MainOptionText(2, 1))); }, Lock::ProfileNotActive},
      {"profile-missing", [](mock::OemFake& f) { f.files.erase(kProfileLeaf); }, Lock::ProfileIo},
      {"profile-access-denied", [](mock::OemFake& f) { f.failAllProfileReads = true; }, Lock::ProfileIo},
      {"profile-hardlinked", [](mock::OemFake& f) { f.files[kProfileLeaf].links = 2; }, Lock::ProfileIo},
      {"profile-utf16", [](mock::OemFake& f) { f.Put(kProfileLeaf, {0xFF, 0xFE, '{', 0, '}', 0}); }, Lock::ProfileEncoding},
      {"profile-invalid-utf8", [](mock::OemFake& f) { f.Put(kProfileLeaf, {'{', 0xFF, '}'}); }, Lock::ProfileEncoding},
      {"profile-empty", [](mock::OemFake& f) { f.Put(kProfileLeaf, {}); }, Lock::ProfileEncoding},
      {"profile-truncated", [](mock::OemFake& f) { auto b = mock::Profile(140); b.resize(b.size() / 2); f.Put(kProfileLeaf, b); }, Lock::ProfileFormat},
      {"profile-target-string", [](mock::OemFake& f) { f.Put(kProfileLeaf, mock::Bytes(Rep(mock::ProfileText(140), "\"ConfigurableTGPTarget\": 140", "\"ConfigurableTGPTarget\": \"140\""))); }, Lock::ProfileFormat},
      {"profile-duplicate-target", [](mock::OemFake& f) { f.Put(kProfileLeaf, mock::Bytes(Rep(mock::ProfileText(140), "\"PL1\": 55", "\"PL1\": 55, \"ConfigurableTGPTarget\": 150"))); }, Lock::ProfileFormat},
      {"profile-not-activated", [](mock::OemFake& f) { f.Put(kProfileLeaf, mock::Bytes(mock::ProfileText(140, "\n", false))); }, Lock::ProfileNotActivated},
      {"tgp-switch-off", [](mock::OemFake& f) { f.Put(kProfileLeaf, mock::Bytes(mock::ProfileText(140, "\n", true, 0, 0))); }, Lock::SwitchOff},
      {"dynamic-boost-on", [](mock::OemFake& f) { f.Put(kProfileLeaf, mock::Bytes(mock::ProfileText(140, "\n", true, 1, 1))); }, Lock::DynamicBoostOn},
      {"target-120", [](mock::OemFake& f) { f.Put(kProfileLeaf, mock::Profile(120)); }, Lock::TargetUnsupported},
      {"target-160", [](mock::OemFake& f) { f.Put(kProfileLeaf, mock::Profile(160)); }, Lock::TargetUnsupported},
      {"target-141", [](mock::OemFake& f) { f.Put(kProfileLeaf, mock::Profile(141)); }, Lock::TargetUnsupported},
  };
  for (const Case& c : cases) {
    StoreRig t;
    c.setup(t.fake);
    const std::vector<uint8_t> before = t.fake.files.count(kProfileLeaf) ? t.fake.Active() : std::vector<uint8_t>();
    const size_t nfiles = t.fake.files.size();
    const Inspection i = t.store.Inspect();
    if (i.ok || i.lock != c.want) std::printf("    case %s: ok=%d lock=%s (want %s) %s\n", c.name, i.ok, LockName(i.lock), LockName(c.want), i.detail.c_str());
    CHECK(!i.ok && i.lock == c.want && !i.detail.empty());
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::Refused && r.lock == c.want && !r.Success());
    CHECK(t.fake.Mutations() == 0 && t.fake.files.size() == nfiles);
    if (t.fake.files.count(kProfileLeaf)) CHECK(t.fake.Active() == before);
  }
}

// ------------------------------------------------------------------ Commit
TEST(oem_commit_writes_verified_backup_first_then_atomically_replaces_and_reads_back) {
  using namespace oemtest;
  using namespace mgp::oem;
  StoreRig t;
  const CommitResult r = t.store.Commit(150, "");
  CHECK(r.kind == CommitKind::Committed && r.Success() && r.jsonVerified && r.fromWatts == 140 && r.toWatts == 150);
  std::vector<uint8_t> want;
  std::string err;
  CHECK(PatchTarget(t.original, 150, want, err));
  CHECK(t.fake.Active() == want);
  CHECK(r.shaBefore == mgp::Sha256Hex(t.original) && r.shaAfter == mgp::Sha256Hex(want) && r.shaBefore != r.shaAfter);
  CHECK(r.backupName == "Mode3_Profile1.json.mgp-original-20260101-120000-1a2b3c48.bak");  // sahte platformun ilk damga/kimliğiyle aynı ad kalıbı
  CHECK(t.fake.files.count(r.backupName) == 1 && t.fake.files[r.backupName].bytes == t.original && t.fake.files[r.backupName].readOnly);
  CHECK(r.backupSha == mgp::Sha256Hex(t.original));
  CHECK(t.fake.CountWithInfix(".mgp-tmp-") == 0 && t.fake.CountWithInfix(".mgp-swap-") == 0 && t.fake.CountWithInfix(".mgp-rejected-") == 0);
  CHECK(t.fake.files.size() == 3);  // MainOption.json + aktif profil + tek yedek
  CHECK(t.fake.files.at(kMainOptionLeaf).bytes == mock::Bytes(mock::MainOptionText()));  // MainOption'a DOKUNULMADI
  CHECK(t.fake.creates == 2 && t.fake.replaces == 1 && t.fake.removes == 1);              // yedek, geçici, tek atomik değiştirme, swap temizliği
  CHECK(r.leftovers.empty());
  CHECK(t.log.Has("OEM persist begin") && t.log.Has("read-back verified") && t.log.Has("original backed up and verified"));
}

TEST(oem_commit_preserves_bom_line_endings_and_every_other_byte_in_all_layouts) {
  using namespace oemtest;
  for (const Variant& v : GoodVariants()) {
    for (int w : {125, 130, 135, 145, 150}) {
      StoreRig t;
      t.fake.Put(mgp::oem::kProfileLeaf, v.bytes);
      const CommitResult r = t.store.Commit(w, "");
      CHECK(r.kind == CommitKind::Committed);
      const auto& now = t.fake.Active();
      CHECK(now.size() == v.bytes.size());
      int diff = 0;
      for (size_t k = 0; k < now.size() && k < v.bytes.size(); ++k) diff += now[k] != v.bytes[k];
      CHECK(diff >= 1 && diff <= 3);
      CHECK((now.size() > 2 && now[0] == 0xEF) == (v.bytes.size() > 2 && v.bytes[0] == 0xEF));
      CHECK(mgp::oem::ParseProfile(now).target == w);
      CHECK(t.fake.files.at(r.backupName).bytes == v.bytes);  // yedek ÖZGÜN baytlarla birebir
    }
  }
}

TEST(oem_commit_to_the_value_already_present_is_a_verified_noop_with_no_write_and_no_new_backup) {
  using namespace oemtest;
  StoreRig t(150);
  const CommitResult r = t.store.Commit(150, "");
  CHECK(r.kind == CommitKind::AlreadyCurrent && r.Success() && r.jsonVerified && r.backupName.empty());
  CHECK(t.fake.Mutations() == 0 && t.fake.Active() == t.original && t.fake.files.size() == 2);
  CHECK(r.shaBefore == r.shaAfter && r.shaBefore == mgp::Sha256Hex(t.original));
  CHECK(t.log.Has("OEM profile zaten 150 W, dogrulandi"));
  // her profil için aynı ilke
  for (int w : {125, 130, 135, 140, 145, 150}) {
    StoreRig u(w);
    const CommitResult q = u.store.Commit(w, mgp::Sha256Hex(u.original));
    CHECK(q.kind == CommitKind::AlreadyCurrent && u.fake.Mutations() == 0);
  }
}

TEST(oem_commit_refuses_unsupported_values_before_touching_anything) {
  using namespace oemtest;
  StoreRig t;
  for (int w : {0, 99, 120, 124, 141, 151, 160, 1000, -150}) {
    const CommitResult r = t.store.Commit(w, "");
    CHECK(r.kind == CommitKind::Refused && r.lock == Lock::TargetUnsupported);
  }
  CHECK(t.fake.Mutations() == 0 && t.fake.reads == 0 && t.fake.Active() == t.original);
}

TEST(oem_commit_refuses_when_the_file_is_not_the_version_that_was_inspected_or_approved) {
  using namespace oemtest;
  StoreRig t;
  const Inspection i = t.store.Inspect();
  t.fake.Foreign(mock::Profile(145));  // Control Center / OEM güncellemesi araya girdi
  const auto foreign = t.fake.Active();
  const CommitResult r = t.store.Commit(150, i.sha);
  CHECK(r.kind == CommitKind::Refused && r.lock == Lock::Changed);
  CHECK(t.fake.Mutations() == 0 && t.fake.Active() == foreign);  // yabancı değer EZİLMEDİ
  const CommitResult r2 = t.store.Commit(150, std::string(64, '0'));
  CHECK(r2.kind == CommitKind::Refused && r2.lock == Lock::Changed && t.fake.Mutations() == 0);
}

TEST(oem_commit_never_overwrites_an_existing_backup_and_keeps_the_users_original_140_backup) {
  using namespace oemtest;
  using namespace mgp::oem;
  StoreRig t;
  // Sahte platformun ilk damga/kimliği, bu adı AYNEN üretir: bilinçli çakışma.
  const std::string userBak = t.Bak("20260101-120000", "1a2b3c48");
  t.fake.Put(userBak, t.original);
  t.fake.files[userBak].readOnly = true;
  CommitResult r = t.store.Commit(150, "");
  CHECK(r.kind == CommitKind::FailedNoChange && !r.Success());
  CHECK(t.fake.Active() == t.original && t.fake.replaces == 0);
  CHECK(t.fake.files.at(userBak).bytes == t.original && t.fake.files.at(userBak).readOnly);  // özgün yedek AYNEN duruyor
  // yeniden denemede yeni kimlik üretilir, eski yedek yine korunur
  r = t.store.Commit(150, "");
  CHECK(r.kind == CommitKind::Committed && r.backupName != userBak);
  CHECK(t.fake.CountWithInfix(".mgp-original-") == 2 && t.fake.files.at(userBak).bytes == t.original);
  // sonraki değişiklikler de yeni, benzersiz yedekler üretir; hiçbiri silinmez/ezilmez
  const CommitResult r3 = t.store.Commit(145, "");
  CHECK(r3.kind == CommitKind::Committed && t.fake.CountWithInfix(".mgp-original-") == 3);
  CHECK(r3.backupName != r.backupName && r3.backupName != userBak);
  CHECK(t.fake.files.at(r.backupName).bytes == t.original);               // 140 sürümü
  CHECK(mgp::oem::ParseProfile(t.fake.files.at(r3.backupName).bytes).target == 150);  // 150 sürümü
}

TEST(oem_commit_disk_full_or_denied_at_each_step_never_changes_the_active_file) {
  using namespace oemtest;
  using namespace mgp::oem;
  // 1) yedek oluşturulamıyor
  {
    StoreRig t;
    t.fake.failCreateNumber = 1;
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedNoChange && t.fake.Active() == t.original && t.fake.files.size() == 2 && t.fake.replaces == 0);
  }
  // 2) geçici dosya oluşturulamıyor: doğrulanmış yedek kalır, aktif dosya DEĞİŞMEZ
  {
    StoreRig t;
    t.fake.failCreateNumber = 2;
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedNoChange && t.fake.Active() == t.original && t.fake.replaces == 0 && !r.backupName.empty());
    CHECK(t.fake.CountWithInfix(".mgp-original-") == 1 && t.fake.CountWithInfix(".mgp-tmp-") == 0);
  }
  // 3) yedek geri okumada bozuk: işlem durur, yedek silinmez (rapor edilir)
  {
    StoreRig t;
    t.fake.corruptCreateReadback = true;
    t.fake.corruptName = ".mgp-original-";
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedNoChange && t.fake.Active() == t.original && t.fake.replaces == 0 && t.fake.creates == 1);
    CHECK(r.leftovers.size() == 1 && t.fake.CountWithInfix(".mgp-original-") == 1);
  }
  // 4) geçici dosya geri okumada bozuk: geçici silinir, aktif dosya değişmez
  {
    StoreRig t;
    t.fake.corruptCreateReadback = true;
    t.fake.corruptName = ".mgp-tmp-";
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedNoChange && t.fake.Active() == t.original && t.fake.replaces == 0 && t.fake.CountWithInfix(".mgp-tmp-") == 0);
  }
  // 5) ReplaceFile erişim engeli / salt okunur: aktif dosya değişmez, geçici silinir
  {
    StoreRig t;
    t.fake.failReplace = true;
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedNoChange && t.fake.Active() == t.original && t.fake.CountWithInfix(".mgp-tmp-") == 0 && t.fake.CountWithInfix(".mgp-swap-") == 0);
    CHECK(t.fake.CountWithInfix(".mgp-original-") == 1);
  }
  // 6) aktif dosya salt okunur öznitelikli: değiştirme reddedilir
  {
    StoreRig t;
    t.fake.files[kProfileLeaf].readOnly = true;
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedNoChange && t.fake.Active() == t.original && t.fake.CountWithInfix(".mgp-tmp-") == 0);
  }
  // 7) sürekli yedek oluşturma hatası (izin yok): hiçbir şey yazılmaz
  {
    StoreRig t;
    t.fake.failCreateName = ".mgp-original-";
    t.fake.failCreateCode = IoCode::AccessDenied;
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedNoChange && t.fake.Active() == t.original && t.fake.files.size() == 2 && t.fake.replaces == 0);
  }
}

TEST(oem_commit_replace_that_loses_the_active_file_recreates_the_verified_original_or_reports_indeterminate) {
  using namespace oemtest;
  using namespace mgp::oem;
  {
    StoreRig t;
    t.fake.replaceDropsActive = true;
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedNoChange);
    CHECK(t.fake.files.count(kProfileLeaf) == 1 && t.fake.Active() == t.original);  // özgün baytlardan yeniden oluşturuldu
    CHECK(r.detail.find("re-created") != std::string::npos && t.fake.CountWithInfix(".mgp-original-") == 1);
  }
  {
    StoreRig t;
    t.fake.replaceDropsActive = true;
    t.fake.failCreateNumber = 3;  // yeniden oluşturma da başarısız
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedIndeterminate && !r.Success());
    CHECK(t.fake.files.count(kProfileLeaf) == 0);                                                   // gerçek durum: dosya yok
    CHECK(!r.backupName.empty() && t.fake.files.at(r.backupName).bytes == t.original);              // ama doğrulanmış yedek elde
    CHECK(r.detail.find(r.backupName) != std::string::npos);                                         // kullanıcıya yedeğin adı söylenir
  }
}

TEST(oem_commit_external_writer_between_precheck_and_commit_is_never_overwritten) {
  using namespace oemtest;
  using namespace mgp::oem;
  // (a) ikinci denetimde (TOCTOU) fark edilir
  {
    StoreRig t;
    t.fake.beforeRead = [&](mock::OemFake& f, const std::string& leaf, int n) {
      if (leaf == kProfileLeaf && n == 2) f.Foreign(mock::Profile(125));
    };
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::Refused && r.lock == Lock::Changed);
    CHECK(t.fake.Active() == mock::Profile(125) && t.fake.replaces == 0 && t.fake.CountWithInfix(".mgp-tmp-") == 0);
  }
  // (b) denetimden SONRA, ReplaceFile içinde araya girer: yabancı sürüm geri konur, bizim yazımız kalmaz
  {
    StoreRig t;
    bool fired = false;
    t.fake.beforeReplace = [&](mock::OemFake& f) {
      if (!fired) { fired = true; f.Foreign(mock::Profile(145)); }
    };
    const CommitResult r = t.store.Commit(150, "");
    CHECK(fired && r.kind == CommitKind::Refused && r.lock == Lock::Changed && !r.Success());
    CHECK(t.fake.Active() == mock::Profile(145));  // yabancı yazının sürümü AYNEN geri geldi
    CHECK(t.fake.CountWithInfix(".mgp-swap-") == 0 && t.fake.CountWithInfix(".mgp-rejected-") == 0 && t.fake.CountWithInfix(".mgp-tmp-") == 0);
    CHECK(t.fake.CountWithInfix(".mgp-original-") == 1);
  }
  // (c) yabancı yazının sürümünü geri koymak da başarısız olursa: sürümleri kaybetme, durumu açıkça bildir
  {
    StoreRig t;
    int rep = 0;
    t.fake.beforeReplace = [&](mock::OemFake& f) {
      if (++rep == 1) f.Foreign(mock::Profile(145));
      if (rep == 2) f.failReplace = true;
    };
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedIndeterminate);
    CHECK(!r.leftovers.empty() && t.fake.files.count(r.leftovers[0]) == 1 && t.fake.files.at(r.leftovers[0]).bytes == mock::Profile(145));  // yabancı sürüm saklandı
    CHECK(r.detail.find(r.backupName) != std::string::npos);
  }
}

TEST(oem_commit_unprovable_or_foreign_state_after_replace_is_reported_not_claimed_as_success) {
  using namespace oemtest;
  using namespace mgp::oem;
  {  // değiştirmeden hemen sonra yabancı yazı
    StoreRig t;
    t.fake.postReplaceForeignWrite = true;
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedIndeterminate && !r.Success() && !r.jsonVerified);
    CHECK(t.fake.Active() == mock::Profile(125));  // onların yazısı ezilmedi
    CHECK(!r.leftovers.empty() && t.fake.CountWithInfix(".mgp-original-") == 1);
  }
  {  // aktif dosya değiştirmeden sonra okunamıyor
    StoreRig t;
    t.fake.failReadNumberOfProfile = 3;
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedIndeterminate && !r.jsonVerified && r.detail.find(r.backupName) != std::string::npos);
  }
  {  // swap dosyası okunamıyor: ne değiştirildiği kanıtlanamaz
    StoreRig t;
    t.fake.beforeRead = [&](mock::OemFake& f, const std::string& leaf, int) {
      if (leaf.find(".mgp-swap-") != std::string::npos) f.files.erase(leaf);
    };
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedIndeterminate && !r.Success());
  }
}

TEST(oem_commit_cleanup_failure_is_only_a_harmless_leftover_and_never_a_failure) {
  using namespace oemtest;
  StoreRig t;
  t.fake.failRemove = true;
  const CommitResult r = t.store.Commit(150, "");
  CHECK(r.kind == CommitKind::Committed && r.Success() && r.jsonVerified);
  CHECK(r.leftovers.size() == 1 && r.leftovers[0].find(".mgp-swap-") != std::string::npos);
  CHECK(mgp::oem::ParseProfile(t.fake.Active()).target == 150);
}

TEST(oem_commit_only_ever_touches_the_profile_file_and_its_own_artifacts) {
  using namespace oemtest;
  using namespace mgp::oem;
  StoreRig t;
  t.fake.Put("Fan.json", mock::Bytes("{\"fan\":1}"));  // alakasız bir OEM dosyası (platform onu zaten tanımaz)
  const auto fanBefore = t.fake.files["Fan.json"].bytes;
  const auto mainBefore = t.fake.files[kMainOptionLeaf].bytes;
  CHECK(t.store.Commit(145, "").Success());
  CHECK(t.fake.files["Fan.json"].bytes == fanBefore && t.fake.files[kMainOptionLeaf].bytes == mainBefore);
  // 230 W adaptör profili, BIOS, EC, kayıt defteri gibi hiçbir arayüz bu modülde YOKTUR: yalnız IOemPlatform'un düz-ad işlemleri çağrıldı.
  CHECK(t.fake.creates == 2 && t.fake.replaces == 1);
}

// ------------------------------------------------------------------ İŞLEM AKIŞI (engine -> OEM)
TEST(persist_txn_applies_ec_nvml_first_then_writes_the_oem_json_and_reports_both_layers) {
  using namespace oemtest;
  TxnRig t;
  int mutationsAtFirstEcWrite = -1, ecWritesAtReplace = -1;
  t.r.ec.afterWrite = [&](mock::Ec&) { if (mutationsAtFirstEcWrite < 0) mutationsAtFirstEcWrite = t.fake.Mutations(); };
  t.fake.beforeReplace = [&](mock::OemFake&) { ecWritesAtReplace = t.r.ec.writes; };
  const auto res = t.Run(150);
  CHECK(res.live.outcome == Outcome::Ok && res.live.ecVerified && res.live.nvVerified && res.live.Succeeded());
  CHECK(res.persistAttempted && res.oem.kind == CommitKind::Committed && BothLayersOk(res) && LiveOk(res) && !res.aborted);
  CHECK(res.pre.ok && res.pre.target == 140);
  CHECK(mutationsAtFirstEcWrite == 0);  // EC'ye yazılırken OEM'e HİÇBİR yazma yapılmamıştı
  CHECK(ecWritesAtReplace >= 1);        // OEM değiştirme, EC yazımı doğrulandıktan SONRA
  CHECK(t.r.ec.mem[0x743] == 0x05 && t.r.ec.mem[0x744] == 25);  // gerçek EC durumu 150 W
  CHECK(mgp::oem::ParseProfile(t.fake.Active()).target == 150);
  CHECK(!t.r.eng.FaultLatched());
}

TEST(persist_txn_failed_live_apply_never_touches_the_oem_file) {
  using namespace oemtest;
  {
    TxnRig t;
    t.r.ec.failWriteNumber = 1;
    const auto res = t.Run(150);
    CHECK(!res.live.Succeeded() && !res.persistAttempted && !BothLayersOk(res));
    CHECK(t.fake.Mutations() == 0 && mgp::oem::ParseProfile(t.fake.Active()).target == 140);
  }
  {  // kimlik/izin ön koşulu: EC'ye hiç dokunulmaz, OEM'e de
    TxnRig t;
    t.r.smb.raw = mock::BuildSmbios("DELL", "XPS", "A01", "x");
    const auto res = t.Run(150);
    CHECK(res.live.outcome == Outcome::BlockedNotReady && !res.persistAttempted && t.r.ec.writes == 0 && t.fake.Mutations() == 0);
  }
  {  // onay yok: 150 W için consentOverOem gerekir
    TxnRig t;
    mgp::PersistTxnInput in;
    in.watts = 150;
    // AllConsent verilmiyor
    const auto res = mgp::RunPersistTransaction(t.r.eng, t.store, t.r.log, in);
    CHECK(!res.live.Succeeded() && !res.persistAttempted && t.r.ec.writes == 0 && t.fake.Mutations() == 0);
  }
  {  // sıcaklık sınırı
    TxnRig t;
    t.r.nv.temp = 90;
    const auto res = t.Run(150);
    CHECK(res.live.outcome == Outcome::BlockedTemp && !res.persistAttempted && t.fake.Mutations() == 0);
  }
}

TEST(persist_txn_locked_oem_module_applies_nothing_at_all_there_is_no_temporary_apply) {
  using namespace oemtest;
  {  // OEM güncellenmiş (GCUService sürümü farklı): modül kilitlenir -> ne EC ne OEM dosyası
    TxnRig t;
    t.fake.svc.version = "1.0.2.71";
    const auto res = t.Run(150);
    CHECK(res.oemUnavailable && !res.persistAttempted && !res.live.Succeeded() && res.live.outcome == Outcome::BlockedNotReady);
    CHECK(res.pre.lock == Lock::ServiceVersion && ClassifyTxn(res) == TxnClass::OemUnavailable);
    CHECK(t.r.ec.writes == 0 && t.r.ec.mem[0x743] == 0x05 && t.r.ec.mem[0x744] == 15 && t.fake.Mutations() == 0);  // canlı değer de dosya da AYNEN
    CHECK(t.r.log.Has("persist txn refused"));
    CHECK(!t.r.eng.FaultLatched());
  }
  {  // her kilit nedeni için aynı: hiçbir şey uygulanmaz (yalnız anlık "geçici" uygulama yoktur)
    for (int k = 0; k < 4; ++k) {
      TxnRig t;
      if (k == 0) t.fake.svc.running = false;
      if (k == 1) t.fake.svc.pathMatches = false;
      if (k == 2) t.fake.svc.dirSafe = false;
      if (k == 3) t.fake.svc.elevated = false;
      const auto res = t.Run(125);
      CHECK(res.oemUnavailable && ClassifyTxn(res) == TxnClass::OemUnavailable && t.r.ec.writes == 0 && t.fake.Mutations() == 0);
    }
  }
  {  // OEM dosyası eksik/bozuk: yine hiçbir şey uygulanmaz
    TxnRig t;
    t.fake.Put(mgp::oem::kProfileLeaf, std::vector<uint8_t>{'{', '"', 'x'});
    const auto res = t.Run(145);
    CHECK(res.oemUnavailable && t.r.ec.writes == 0 && t.fake.Active().size() == 3);
  }
}

TEST(persist_txn_oem_write_failure_returns_the_live_state_to_what_it_was_using_the_existing_rollback) {
  using namespace oemtest;
  TxnRig t;
  t.fake.failReplace = true;
  const auto res = t.Run(150);
  CHECK(res.live.outcome == Outcome::Ok);  // katman (a) gerçekten uygulanmıştı
  CHECK(res.persistAttempted && res.oem.kind == CommitKind::FailedNoChange && !res.oem.Success() && !BothLayersOk(res) && !LiveOk(res));
  CHECK(res.liveRestoreAttempted && res.liveRestore.outcome == Outcome::FailedRolledBack && res.liveRestore.rollbackVerified);
  CHECK(t.r.ec.mem[0x743] == 0x05 && t.r.ec.mem[0x744] == 15 && t.r.ec.mem[0x745] == 0 && t.r.ec.mem[0x746] == 0);  // 140 W'a dönüldü
  CHECK(!t.r.eng.FaultLatched());
  CHECK(t.r.eng.ReadLive(false).activeProfile == 140);
  CHECK(mgp::oem::ParseProfile(t.fake.Active()).target == 140);  // OEM dosyası DEĞİŞMEDİ (ve sahte ReplaceFile reddetti)
  for (const auto& w : t.r.ec.writeLog) CHECK(IsAllowedEcWrite(w.first, w.second));  // geri dönüş de izin listesinin içinde kaldı
}

TEST(persist_txn_if_the_live_restore_cannot_be_verified_the_existing_critical_write_lock_applies) {
  using namespace oemtest;
  TxnRig t;
  t.fake.failReplace = true;
  t.r.ec.failAllWritesAfterFirst = true;  // ilk EC yazımı (uygulama) başarılı, geri dönüş yazımları başarısız
  const auto res = t.Run(150);
  CHECK(res.live.outcome == Outcome::Ok && res.liveRestoreAttempted);
  CHECK(res.liveRestore.outcome == Outcome::FailedRollbackUnverified && !res.liveRestore.rollbackVerified);
  CHECK(t.r.eng.FaultLatched());
  // kilit sürerken yeni yazma hiç yapılmaz
  const int w0 = t.r.ec.writes;
  const auto next = t.Run(140);
  CHECK(!next.live.Succeeded() && t.r.ec.writes == w0 && !next.persistAttempted);
}

TEST(persist_txn_live_already_at_target_still_writes_the_json_and_a_json_failure_does_not_touch_the_unchanged_live_state) {
  using namespace oemtest;
  {
    TxnRig t;
    t.r.ec.SetOemOn(25);  // canlı zaten 150 W; OEM dosyası hâlâ 140
    const auto res = t.Run(150);
    CHECK(res.live.outcome == Outcome::AlreadyActive && t.r.ec.writes == 0);
    CHECK(res.persistAttempted && res.oem.kind == CommitKind::Committed && BothLayersOk(res));
    CHECK(mgp::oem::ParseProfile(t.fake.Active()).target == 150);
  }
  {
    TxnRig t;
    t.r.ec.SetOemOn(25);
    t.fake.failReplace = true;
    const auto res = t.Run(150);
    CHECK(res.live.outcome == Outcome::AlreadyActive && res.persistAttempted && !res.oem.Success());
    CHECK(!res.liveRestoreAttempted && t.r.ec.writes == 0 && t.r.ec.mem[0x744] == 25);  // canlı durum hiç değişmediği için geri dönüş YOK
  }
}

TEST(persist_txn_existing_matching_oem_record_needs_no_write_and_no_new_backup) {
  using namespace oemtest;
  TxnRig t;
  t.r.ec.SetOemOn(25);                                       // canlı 150 W
  t.fake.Put(mgp::oem::kProfileLeaf, mock::Profile(150));    // OEM kaydı da 150 W (kullanıcının mevcut durumu)
  t.fake.Put(mgp::oem::MakeBackupName("20260101-120000", "1a2b3c48"), mock::Profile(140));
  const auto before = t.fake.Active();
  const auto res = t.Run(150);
  CHECK(res.live.outcome == Outcome::AlreadyActive && res.oem.kind == CommitKind::AlreadyCurrent && BothLayersOk(res));
  CHECK(t.fake.Mutations() == 0 && t.fake.Active() == before && t.r.ec.writes == 0 && t.fake.CountWithInfix(".mgp-original-") == 1);
}

TEST(persist_txn_approval_for_a_different_file_version_aborts_before_anything_is_applied) {
  using namespace oemtest;
  TxnRig t;
  const auto res = t.Run(150, std::string(64, '0'));
  CHECK(res.aborted && !res.live.Succeeded() && !res.persistAttempted);
  CHECK(t.r.ec.writes == 0 && t.fake.Mutations() == 0);
  // doğru sürüm onaylanmışsa devam eder
  const Inspection i = t.store.Inspect();
  const auto ok = t.Run(150, i.sha);
  CHECK(!ok.aborted && BothLayersOk(ok));
}

TEST(persist_txn_sequence_of_profile_clicks_keeps_json_and_ec_in_step_and_never_creates_stray_files) {
  using namespace oemtest;
  TxnRig t;
  for (int w : {150, 145, 130, 125, 135, 140, 150}) {
    const auto res = t.Run(w);
    CHECK(BothLayersOk(res) || res.live.outcome == Outcome::AlreadyActive);
    const auto lv = t.r.eng.ReadLive(false);
    CHECK(lv.activeProfile == w);
    CHECK(mgp::oem::ParseProfile(t.fake.Active()).target == w);
  }
  CHECK(t.fake.CountWithInfix(".mgp-tmp-") == 0 && t.fake.CountWithInfix(".mgp-swap-") == 0 && t.fake.CountWithInfix(".mgp-rejected-") == 0);
  CHECK(t.fake.CountWithInfix(".mgp-original-") == 7);  // 7 gerçek değişiklik = 7 benzersiz yedek; hiçbiri ezilmedi
  CHECK(t.fake.files.size() == 2 + 7);
}

// ------------------------------------------------------------------ Engine::RestoreLive
TEST(engine_restore_live_returns_to_the_exact_pre_apply_state_inside_the_ec_allow_list) {
  Rig r;
  const ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::Ok && a.before.ec.ok && a.before.ec.ctrl == 0x05 && a.before.ec.offset == 15);
  const size_t w0 = r.ec.writeLog.size();
  const ApplyResult b = r.eng.RestoreLive(a.before, "unit test");
  CHECK(b.outcome == Outcome::FailedRolledBack && b.rolledBack && b.rollbackVerified && !r.eng.FaultLatched());
  CHECK(r.ec.mem[0x743] == 0x05 && r.ec.mem[0x744] == 15 && r.ec.mem[0x745] == 0 && r.ec.mem[0x746] == 0);
  CHECK(r.eng.ReadLive(false).activeProfile == 140);
  CHECK(r.ec.writeLog.size() > w0);
  for (size_t k = w0; k < r.ec.writeLog.size(); ++k) CHECK(IsAllowedEcWrite(r.ec.writeLog[k].first, r.ec.writeLog[k].second));
  CHECK(r.log.Has("RestoreLive: returning to the pre-apply state"));
  // zaten önceki durumdaysa yazma yok
  const size_t w1 = r.ec.writeLog.size();
  const ApplyResult c = r.eng.RestoreLive(a.before, "again");
  CHECK(c.outcome == Outcome::FailedNoChange && c.rollbackVerified && r.ec.writeLog.size() == w1);
}

TEST(engine_restore_live_with_unknown_previous_state_writes_nothing) {
  Rig r;
  (void)r.eng.Apply(150, AllConsent());
  const size_t w0 = r.ec.writeLog.size();
  LiveState unknown;  // ec.ok == false
  const ApplyResult b = r.eng.RestoreLive(unknown, "x");
  CHECK(b.outcome == Outcome::BlockedNotReady && r.ec.writeLog.size() == w0 && r.ec.mem[0x744] == 25 && !r.eng.FaultLatched());
}

TEST(engine_restore_live_failure_latches_the_fault_like_any_unverified_rollback) {
  Rig r;
  const ApplyResult a = r.eng.Apply(150, AllConsent());
  r.ec.failAllWritesAfterFirst = true;
  const ApplyResult b = r.eng.RestoreLive(a.before, "x");
  CHECK(b.outcome == Outcome::FailedRollbackUnverified && !b.rollbackVerified && r.eng.FaultLatched());
  CHECK(r.ec.mem[0x744] == 25);  // yazma başarısız olduğu için EC olduğu gibi: gerçek durum raporlanır
}

// ------------------------------------------------------------------ Ek güvenlik denetimleri
TEST(oem_replace_failure_after_a_foreign_write_is_reported_as_changed_by_another_program_not_as_unchanged) {
  using namespace oemtest;
  using namespace mgp::oem;
  {  // ReplaceFile başarısız + araya giren yabancı yazıcı aktif dosyayı başka bir değere çevirdi: "değişmedi" DENMEZ
    StoreRig t(140);
    t.fake.failReplace = true;
    t.fake.beforeReplace = [](mock::OemFake& f) { f.Foreign(mock::Profile(145)); };
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::Refused && r.lock == Lock::Changed && !r.Success());
    CHECK(mgp::oem::ParseProfile(t.fake.Active()).target == 145);  // yabancının yazdığı AYNEN duruyor
    CHECK(r.detail.find("another program") != std::string::npos);
    CHECK(!r.backupName.empty() && t.fake.files.count(r.backupName) == 1);  // özgün baytların doğrulanmış yedeği korunur
  }
  {  // ReplaceFile hata bildirdi ama aktif dosya yazmak istediğimiz baytları zaten içeriyor: kanıtlanamaz -> belirsiz
    StoreRig t(140);
    t.fake.failReplace = true;
    t.fake.beforeReplace = [](mock::OemFake& f) { f.Foreign(mock::Profile(150)); };
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedIndeterminate && !r.Success());
    CHECK(!r.backupName.empty());
  }
  {  // klasik durum: ReplaceFile reddetti, dosya olduğu gibi: gerçekten "değişmedi"
    StoreRig t(140);
    t.fake.failReplace = true;
    const CommitResult r = t.store.Commit(150, "");
    CHECK(r.kind == CommitKind::FailedNoChange && t.fake.Active() == t.original);
  }
}

TEST(persist_txn_store_exceptions_become_honest_results_instead_of_escaping) {
  using namespace oemtest;
  {  // ön denetim sırasında istisna: modül kilitli sayılır; HİÇBİR ŞEY uygulanmaz (geçici uygulama yok)
    TxnRig t;
    t.fake.beforeRead = [](mock::OemFake&, const std::string& leaf, int n) {
      if (leaf == mgp::oem::kMainOptionLeaf && n == 1) throw std::runtime_error("boom");
    };
    const auto res = t.Run(150);
    CHECK(!res.live.Succeeded() && !res.pre.ok && res.oemUnavailable && !res.persistAttempted && !BothLayersOk(res));
    CHECK(t.fake.Mutations() == 0 && t.r.ec.writes == 0 && t.r.ec.mem[0x744] == 15);
  }
  {  // yazma sırasında istisna: belirsiz sonuç + canlı durum geri alınır (varsa), istisna dışarı çıkmaz
    TxnRig t;
    t.fake.beforeReplace = [](mock::OemFake&) { throw std::runtime_error("boom"); };
    mgp::PersistTxnResult res;
    bool threw = false;
    try { res = t.Run(150); } catch (...) { threw = true; }
    CHECK(!threw);
    CHECK(res.persistAttempted && res.oem.kind == CommitKind::FailedIndeterminate && !res.oem.Success());
    CHECK(res.liveRestoreAttempted && res.liveRestore.rollbackVerified && t.r.ec.mem[0x744] == 15);
  }
}
