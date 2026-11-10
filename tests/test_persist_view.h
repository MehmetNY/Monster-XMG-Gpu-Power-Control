// KALICI UYGULAMA ARAYÜZ MANTIĞI testleri (src/core/persist_view.*, persist_txn.*): kart durumu, sahiplik kaydı, yabancı değer onayı ve
// iki katmanlı sonuç sınıflandırması. Sahte EC/NVML + bellek içi sahte OEM platformu.
// (test_main.cpp içinden, test_oem_store.h'tan SONRA dahil edilir.)
#pragma once
#include <fstream>
#include <sstream>

#include "../src/core/persist_view.h"
#include "../src/core/task_xml.h"

namespace pvtest {

using namespace mgp;

inline oem::Inspection OkInsp(int target, const std::string& sha = std::string(64, 'a')) {
  oem::Inspection i;
  i.ok = true;
  i.lock = oem::Lock::None;
  i.haveFile = true;
  i.target = target;
  i.sha = sha;
  return i;
}
inline OemOwnership Own(const std::string& sha, int watts) {
  OemOwnership o;
  o.ownedSha = sha;
  o.ownedWatts = watts;
  return o;
}
inline PersistView View(const oem::Inspection& i, int live, const OemOwnership& own = OemOwnership()) {
  PersistViewInput in;
  in.haveInsp = true;
  in.insp = i;
  in.liveWatts = live;
  in.own = own;
  return DerivePersistView(in);
}

}  // namespace pvtest

TEST(persist_view_is_derived_from_the_real_oem_file_and_the_real_live_state) {
  using namespace pvtest;
  {  // henüz denetim yok
    PersistViewInput in;
    CHECK(DerivePersistView(in).kind == PersistKind::Checking);
  }
  {  // OEM 150, anlık 150
    const PersistView v = View(OkInsp(150), 150);
    CHECK(v.kind == PersistKind::Matches && v.oemWatts == 150 && v.liveWatts == 150);
  }
  {  // OEM 150 kayıtlı ama anlık 140: dürüst "farklı" durumu
    const PersistView v = View(OkInsp(150), 140);
    CHECK(v.kind == PersistKind::Differs && v.oemWatts == 150 && v.liveWatts == 140);
  }
  {  // anlık profil okunamadı/tutarsız: OEM kaydı yine de gösterilir, anlık "bilinmiyor"
    const PersistView v = View(OkInsp(150), 0);
    CHECK(v.kind == PersistKind::LiveUnknown && v.oemWatts == 150 && v.liveWatts == 0);
  }
  {  // OEM dosyasında desteklenmeyen bir sayı: oemWatts 0 (uydurma değer gösterilmez)
    const PersistView v = View(OkInsp(133), 133);
    CHECK(v.oemWatts == 0);
  }
}

TEST(persist_view_locked_module_keeps_the_real_reason) {
  using namespace pvtest;
  const oem::Lock locks[] = {oem::Lock::NotAdmin,       oem::Lock::ServiceNotRunning, oem::Lock::ServicePath,    oem::Lock::ServiceVersion,
                             oem::Lock::DirUnsafe,      oem::Lock::MainOptionIo,      oem::Lock::MainOptionFormat, oem::Lock::ProfileNotActive,
                             oem::Lock::ProfileIo,      oem::Lock::ProfileEncoding,   oem::Lock::ProfileFormat,  oem::Lock::ProfileNotActivated,
                             oem::Lock::SwitchOff,      oem::Lock::DynamicBoostOn,    oem::Lock::TargetUnsupported, oem::Lock::Changed};
  for (oem::Lock lk : locks) {
    oem::Inspection i = OkInsp(150);
    i.ok = false;
    i.lock = lk;
    const PersistView v = View(i, 150, Own(i.sha, 150));
    CHECK(v.kind == PersistKind::Locked && v.lock == lk);
  }
  {  // sürüm kilidinde bulunan sürüm kullanıcıya gösterilebilsin
    oem::Inspection i;
    i.ok = false;
    i.lock = oem::Lock::ServiceVersion;
    i.svc.versionRead = true;
    i.svc.version = "1.0.2.71";
    const PersistView v = View(i, 140);
    CHECK(v.serviceVersion == "1.0.2.71" && v.oemWatts == 0);
  }
  {  // servis hiç yokken dosya okunmadı: OEM değeri uydurulmaz
    oem::Inspection i;
    i.ok = false;
    i.lock = oem::Lock::ServiceNotRunning;
    const PersistView v = View(i, 150);
    CHECK(v.oemWatts == 0 && v.owner == OwnerKind::None);
  }
}

TEST(persist_view_owner_relation_is_judged_only_from_the_real_file_hash) {
  using namespace pvtest;
  const std::string mine(64, 'a'), other(64, 'b');
  CHECK(View(OkInsp(150, mine), 150, Own(mine, 150)).owner == OwnerKind::Mine);
  CHECK(View(OkInsp(145, other), 145, Own(mine, 150)).owner == OwnerKind::Changed);  // başka bir program değiştirmiş olabilir
  CHECK(View(OkInsp(150, mine), 150).owner == OwnerKind::None);                        // kayıt yok
  oem::Inspection nofile;
  nofile.ok = true;
  CHECK(View(nofile, 150, Own(mine, 150)).owner == OwnerKind::None);                   // dosya okunamadıysa hüküm verilmez
}

TEST(persist_classify_txn_reports_each_layer_separately_and_never_calls_a_half_result_full_success) {
  using namespace oemtest;
  {  // tam başarı
    TxnRig t;
    const auto r = t.Run(150);
    CHECK(ClassifyTxn(r) == TxnClass::BothOk && !OemFileKnownUnchanged(r));
  }
  {  // OEM kaydı zaten bu değerde
    TxnRig t;
    t.r.ec.SetOemOn(25);
    t.fake.Put(mgp::oem::kProfileLeaf, mock::Profile(150));
    const auto r = t.Run(150);
    CHECK(ClassifyTxn(r) == TxnClass::BothOkAlready && OemFileKnownUnchanged(r));
  }
  {  // anlık katman başarısız: OEM'e dokunulmadı
    TxnRig t;
    t.r.ec.failWriteNumber = 1;
    const auto r = t.Run(150);
    CHECK(ClassifyTxn(r) == TxnClass::LiveFailed && OemFileKnownUnchanged(r));
  }
  {  // OEM modülü kilitli: HİÇBİR ŞEY uygulanmadı
    TxnRig t;
    t.fake.svc.running = false;
    const auto r = t.Run(150);
    CHECK(ClassifyTxn(r) == TxnClass::OemUnavailable && OemFileKnownUnchanged(r) && !r.live.Succeeded());
  }
  {  // OEM yazımı başarısız -> anlık değişiklik doğrulanarak geri alındı
    TxnRig t;
    t.fake.failReplace = true;
    const auto r = t.Run(150);
    CHECK(ClassifyTxn(r) == TxnClass::OemFailedLiveRestored && OemFileKnownUnchanged(r));
  }
  {  // OEM yazımı başarısız + geri alma DOĞRULANAMADI: kritik
    TxnRig t;
    t.fake.failReplace = true;
    t.r.ec.failAllWritesAfterFirst = true;
    const auto r = t.Run(150);
    CHECK(ClassifyTxn(r) == TxnClass::OemFailedRestoreUnverified);
  }
  {  // canlı zaten hedefte + OEM yazımı başarısız: geri alma gerekmedi
    TxnRig t;
    t.r.ec.SetOemOn(25);
    t.fake.failReplace = true;
    const auto r = t.Run(150);
    CHECK(ClassifyTxn(r) == TxnClass::OemFailedLiveUnchanged);
  }
  {  // onaylanan sürümden farklı dosya: hiçbir şey uygulanmadı
    TxnRig t;
    const auto r = t.Run(150, std::string(64, '0'));
    CHECK(ClassifyTxn(r) == TxnClass::Aborted && OemFileKnownUnchanged(r));
  }
  {  // dosyanın son durumu bilinmiyor (ReplaceFile 1177 penceresi): "değişmedi" DENMEZ
    TxnRig t;
    t.fake.replaceDropsActive = true;
    const auto r = t.Run(150);
    CHECK(!r.oem.Success() && (r.oem.kind == oem::CommitKind::FailedIndeterminate || r.oem.kind == oem::CommitKind::FailedNoChange));
    if (r.oem.kind == oem::CommitKind::FailedIndeterminate) CHECK(!OemFileKnownUnchanged(r));
  }
}

TEST(persist_classify_foreign_flags_only_unowned_or_externally_changed_non_default_values_that_the_target_would_overwrite) {
  using namespace pvtest;
  const std::string mine(64, 'a'), other(64, 'b');
  const OemOwnership own = Own(mine, 150);
  // sahiplik kaydı YOK
  CHECK(ClassifyForeign(OkInsp(150, other), OemOwnership(), 145) == ForeignKind::UnownedValue);   // kullanıcının mevcut 150 kaydı, 145 seçildi
  CHECK(ClassifyForeign(OkInsp(150, other), OemOwnership(), 140) == ForeignKind::UnownedValue);   // 140'a dönmek de başkasının değerini ezer
  CHECK(ClassifyForeign(OkInsp(125, other), OemOwnership(), 150) == ForeignKind::UnownedValue);
  CHECK(ClassifyForeign(OkInsp(150, other), OemOwnership(), 150) == ForeignKind::None);           // hedef zaten aynı: yazma yok
  CHECK(ClassifyForeign(OkInsp(140, other), OemOwnership(), 150) == ForeignKind::None);           // OEM varsayılanı: ilk değişiklik, sorulmaz
  // sahiplik kaydı VAR
  CHECK(ClassifyForeign(OkInsp(150, mine), own, 145) == ForeignKind::None);                       // bu uygulamanın bıraktığı sürüm
  CHECK(ClassifyForeign(OkInsp(145, other), own, 150) == ForeignKind::ChangedAfterOurs);          // dosyayı başkası değiştirmiş
  CHECK(ClassifyForeign(OkInsp(140, other), own, 150) == ForeignKind::None);                      // başkası 140'a sıfırlamış: varsayılan
  CHECK(ClassifyForeign(OkInsp(145, other), own, 145) == ForeignKind::None);
  // modül kilitli / dosya okunamadı: zaten yazılmaz
  oem::Inspection locked = OkInsp(145, other);
  locked.ok = false;
  locked.lock = oem::Lock::ServiceNotRunning;
  CHECK(ClassifyForeign(locked, OemOwnership(), 150) == ForeignKind::None);
  oem::Inspection nofile;
  CHECK(ClassifyForeign(nofile, OemOwnership(), 150) == ForeignKind::None);
}

TEST(persist_txn_never_overwrites_a_foreign_oem_value_without_explicit_approval_of_that_exact_file_version) {
  using namespace oemtest;
  using namespace mgp::oem;
  {  // kullanıcının gerçek durumu: dosyada 145 var, bu uygulama yazmadı, kullanıcı 150 tıklıyor
    TxnRig t;
    t.fake.Put(kProfileLeaf, mock::Profile(145));
    const auto before = t.fake.Active();
    const auto res = t.Run(150);
    CHECK(res.needsApproval && res.foreign == ForeignKind::UnownedValue && !res.aborted && !res.persistAttempted);
    CHECK(!res.live.Succeeded() && ClassifyTxn(res) == TxnClass::NeedsApproval);
    CHECK(t.r.ec.writes == 0 && t.r.ec.mem[0x744] == 15);              // canlı katmana DA dokunulmadı
    CHECK(t.fake.Mutations() == 0 && t.fake.Active() == before);      // OEM dosyasına hiçbir şey yazılmadı
    CHECK(t.r.log.Has("persist txn needs approval"));
    // yalnız "onaylandı" bayrağı yetmez: onaylanan sürümün özeti de gerekir
    const auto noSha = t.Run(150, "", true);
    CHECK(noSha.needsApproval && t.r.ec.writes == 0 && t.fake.Mutations() == 0);
    // başka bir sürüm için verilmiş onay geçersizdir (dosya o arada değişti)
    const auto stale = t.Run(150, std::string(64, '0'), true);
    CHECK(stale.aborted && !stale.needsApproval && t.r.ec.writes == 0 && t.fake.Mutations() == 0);
    // doğru sürüm için açık onay: işlem yürür; yabancı değerin tam yedeği alınır ve korunur
    const Inspection seen = t.store.Inspect();
    const auto ok = t.Run(150, seen.sha, true);
    CHECK(!ok.needsApproval && BothLayersOk(ok) && ok.foreign == ForeignKind::UnownedValue);
    CHECK(mgp::oem::ParseProfile(t.fake.Active()).target == 150 && t.r.ec.mem[0x744] == 25);
    CHECK(t.fake.files.count(ok.oem.backupName) == 1 && t.fake.files.at(ok.oem.backupName).bytes == before);  // 145 sürümü bayt bayt duruyor
  }
  {  // onay sonrası dosya yine değişirse (onay penceresi açıkken başka program yazdı) HİÇBİR ŞEY uygulanmaz
    TxnRig t;
    t.fake.Put(kProfileLeaf, mock::Profile(145));
    const Inspection seen = t.store.Inspect();
    t.fake.Foreign(mock::Profile(130));
    const auto res = t.Run(150, seen.sha, true);
    CHECK(res.aborted && t.r.ec.writes == 0 && t.fake.Mutations() == 0 && mgp::oem::ParseProfile(t.fake.Active()).target == 130);
  }
  {  // bu uygulamanın kendi bıraktığı sürüm: soru YOK (normal akış)
    TxnRig t;
    CHECK(BothLayersOk(t.Run(150)));
    const auto next = t.Run(145);
    CHECK(!next.needsApproval && BothLayersOk(next));
  }
  {  // bu uygulamanın bıraktığından sonra başka bir program 130'a çevirdi: ChangedAfterOurs -> soru
    TxnRig t;
    CHECK(BothLayersOk(t.Run(150)));
    t.fake.Foreign(mock::Profile(130));
    const int writes = t.r.ec.writes;
    const auto next = t.Run(145);
    CHECK(next.needsApproval && next.foreign == ForeignKind::ChangedAfterOurs && t.r.ec.writes == writes);
    CHECK(mgp::oem::ParseProfile(t.fake.Active()).target == 130);
    // ama başka program dosyayı 140'a (OEM varsayılanı) sıfırladıysa özelleştirme yoktur: soru yok
    t.fake.Foreign(mock::Profile(140));
    CHECK(!t.Run(145).needsApproval);
  }
  {  // hedef zaten dosyadaki değer: ezme yok, soru yok (mevcut 150 kaydı + 150 seçimi = yazmadan doğrulama)
    TxnRig t;
    t.r.ec.SetOemOn(25);
    t.fake.Put(kProfileLeaf, mock::Profile(150));
    const auto res = t.Run(150);
    CHECK(!res.needsApproval && res.oem.kind == CommitKind::AlreadyCurrent && t.fake.Mutations() == 0);
  }
  {  // modül kilitliyken (servis yok) soru da yok: HİÇBİR ŞEY uygulanmaz
    TxnRig t;
    t.fake.Put(kProfileLeaf, mock::Profile(145));
    t.fake.svc.running = false;
    const auto res = t.Run(150);
    CHECK(!res.needsApproval && res.oemUnavailable && !res.live.Succeeded() && t.r.ec.writes == 0 && t.fake.Mutations() == 0);
  }
}

TEST(persist_ownership_is_recorded_only_for_the_file_this_transaction_verified) {
  using namespace oemtest;
  using namespace mgp::oem;
  {  // normal: işlem sonrası dosya, işlemin doğruladığı dosya
    TxnRig t;
    t.trackOwnership = false;
    const auto res = t.Run(150);
    CHECK(BothLayersOk(res));
    const OemOwnership o = OwnershipFromTxn(t.store.Inspect(), res);
    CHECK(o.Present() && o.ownedSha == res.oem.shaAfter && o.ownedWatts == 150);
  }
  {  // işlemden hemen sonra başka bir program dosyayı değiştirdi: sahiplik KAYDEDİLMEZ
    TxnRig t;
    t.trackOwnership = false;
    const auto res = t.Run(150);
    CHECK(BothLayersOk(res));
    t.fake.Foreign(mock::Profile(125));
    const Inspection after = t.store.Inspect();
    CHECK(after.ok && !OwnershipFromTxn(after, res).Present());
  }
  {  // başarısız işlem: sahiplik kanıtı yok
    TxnRig t;
    t.trackOwnership = false;
    t.fake.failReplace = true;
    const auto failed = t.Run(150);
    CHECK(!failed.oem.Success() && !OwnershipFromTxn(t.store.Inspect(), failed).Present());
  }
  {  // denetim kilitli / SHA'sız
    TxnRig t;
    t.trackOwnership = false;
    const auto res = t.Run(150);
    Inspection locked = t.store.Inspect();
    locked.ok = false;
    CHECK(!OwnershipFromTxn(locked, res).Present());
    Inspection nosha = t.store.Inspect();
    nosha.sha.clear();
    CHECK(!OwnershipFromTxn(nosha, res).Present());
  }
}

TEST(persist_ownership_survives_repeated_clicks_and_a_foreign_value_is_never_claimed) {
  using namespace oemtest;
  using namespace mgp::oem;
  {  // aynı profile tekrar tıklamak (OEM zaten bu değerde: yazma yok) sahiplik kaydını SİLMEZ; sonraki seçim soru sormaz
    TxnRig t;
    CHECK(ClassifyTxn(t.Run(150)) == TxnClass::BothOk && t.own.Present());
    const std::string sha = t.own.ownedSha;
    CHECK(ClassifyTxn(t.Run(150)) == TxnClass::BothOkAlready && t.own.Present() && t.own.ownedSha == sha);
    const auto next = t.Run(145);
    CHECK(!next.needsApproval && BothLayersOk(next) && t.own.ownedWatts == 145);
  }
  {  // başka bir programın yazdığı 150 değerine "150" tıklamak o değeri SAHİPLENMEZ: sonraki farklı seçim hâlâ onay ister
    TxnRig t;
    t.r.ec.SetOemOn(25);
    t.fake.Put(kProfileLeaf, mock::Profile(150));
    CHECK(ClassifyTxn(t.Run(150)) == TxnClass::BothOkAlready && !t.own.Present());
    CHECK(t.Run(145).needsApproval);
  }
  {  // 140'a (OEM varsayılanı) dönmek: soru yok, işlem tamam
    TxnRig t;
    CHECK(BothLayersOk(t.Run(150)));
    const auto back = t.Run(140);
    CHECK(!back.needsApproval && BothLayersOk(back) && mgp::oem::ParseProfile(t.fake.Active()).target == 140 && t.r.ec.mem[0x744] == 15);
  }
}

// Kullanıcıya görünen metinler: kalıcılığın ne olduğu DOĞRU anlatılmalı (OEM-profil tabanlı; firmware/BIOS/EC kalıcı yazımı DEĞİL).
// strings_tr.h Windows'a özgü başlık içerdiğinden burada DERLENMEZ; kaynak metni okunup yasak/zorunlu ifadeler denetlenir.
TEST(ui_texts_never_overclaim_what_persistence_is) {
  std::ifstream f("src/win/strings_tr.h", std::ios::binary);
  CHECK(f.good());  // testler depo kökünden çalıştırılır (make test)
  std::stringstream ss;
  ss << f.rdbuf();
  // Yalnız kullanıcıya görünen metinler denetlenir: satır yorumları (// ...) ayıklanır.
  std::string t;
  {
    std::istringstream is(ss.str());
    std::string line;
    while (std::getline(is, line)) {
      const size_t a = line.find_first_not_of(" \t");
      if (a != std::string::npos && line.compare(a, 2, "//") == 0) continue;
      t += line + "\n";
    }
  }
  CHECK(t.find("Seçili GPU güç profili, mevcut OEM profil dosyasına kaydedilir. MONSTER GPU POWER uygulamasının Windows açılışında "
               "\"\n    L\"çalışması gerekmez. OEM GCUService kurulu ve çalışır olmalıdır.") != std::string::npos);
  const char* forbidden[] = {"EC firmware", "firmware kalıcı", "BIOS modifikasyonu", "BIOS'a yaz", "BIOS'u değiştir", "işletim sistemlerinden bağımsız",
                             "Windows açılışında uygula", "Geçici", "geçici profil", "Tanılama", "Günlüğü aç", "Dış müdahale"};
  for (const char* w : forbidden) CHECK(t.find(w) == std::string::npos);
}
