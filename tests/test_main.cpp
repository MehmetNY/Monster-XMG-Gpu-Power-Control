// MONSTER GPU POWER - çekirdek mantık otomatik testleri (SAHTE EC/NVML/SMBIOS ile).
// Bu testler algoritmayı/ön koşulları/geri almayı doğrular; GERÇEK donanım doğrulaması DEĞİLDİR.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "../src/core/hardware_identity_guard.h"
#include "../src/core/power_profile_engine.h"
#include "../src/core/settings.h"
#include "../src/core/smbios.h"
#include "mocks.h"
#include "test_helpers.h"

using namespace mgp;

static int g_pass = 0, g_fail = 0;
#define CHECK(c)                                                         \
  do {                                                                   \
    if (!(c)) { ++g_fail; std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
    else ++g_pass;                                                       \
  } while (0)

struct TestCase { const char* name; void (*fn)(); };
static std::vector<TestCase>& Tests() { static std::vector<TestCase> t; return t; }
struct Reg { Reg(const char* n, void (*f)()) { Tests().push_back({n, f}); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, &name); static void name()

// Her testte taze bir "donanım" kurulumu.
struct Rig {
  mock::Clock clock; mock::MemLog log; mock::Ec ec; mock::Nv nv{ec}; mock::Smbios smb;
  PowerProfileEngine eng;
  Rig() : eng(ec, nv, smb, clock, log) { smb.raw = mock::GoodSmbios(); }
};
static ApplyOptions AllConsent() { ApplyOptions o; o.consentOverOem = true; o.consentUnverified = true; return o; }

// ------------------------------------------------------------------ SMBIOS & kimlik
TEST(smbios_parses_expected_fields_and_never_exposes_serials) {
  SmbiosInfo i;
  auto raw = mock::GoodSmbios();
  CHECK(ParseRawSmbios(raw.data(), raw.size(), i));
  CHECK(i.sysManufacturer == "MONSTER");
  CHECK(i.sysProduct == "TULPAR T5 V21.7");
  CHECK(i.biosVersion == "N.1.07Mon08");
  CHECK(i.boardProduct == "GM5TG7Y");
  CHECK(i.ecMajor == 1 && i.ecMinor == 7);
  std::string all = i.sysManufacturer + i.sysProduct + i.sysVersion + i.sysSku + i.sysFamily + i.biosVendor +
                    i.biosVersion + i.boardManufacturer + i.boardProduct + i.boardVersion;
  CHECK(all.find("SERIAL") == std::string::npos);
}
TEST(smbios_garbage_does_not_crash) {
  SmbiosInfo i;
  uint8_t junk[64]; for (int k = 0; k < 64; ++k) junk[k] = (uint8_t)(k * 37);
  ParseRawSmbios(junk, sizeof junk, i);
  CHECK(!ParseRawSmbios(nullptr, 0, i));
  uint8_t trunc[12] = {0, 3, 3, 0, 4, 0, 0, 0, 1, 0xFF, 0, 0};
  CHECK(!ParseRawSmbios(trunc, sizeof trunc, i));
}
TEST(identity_exact_and_case_insensitive) {
  SmbiosInfo i;
  auto raw = mock::BuildSmbios("monster", "tulpar t5 v21.7 extra", "n.1.07mon08", "x");
  ParseRawSmbios(raw.data(), raw.size(), i);
  IdentityResult r = EvaluateIdentity(i, true);
  CHECK(r.verdict == IdentityVerdict::Exact);
  CHECK(!r.boardConfirmed);
  auto raw2 = mock::GoodSmbios(); ParseRawSmbios(raw2.data(), raw2.size(), i);
  CHECK(EvaluateIdentity(i, true).boardConfirmed);
  CHECK(EvaluateIdentity(i, true).summary.find("SERIAL") == std::string::npos);
}
TEST(identity_mismatches_are_detected) {
  SmbiosInfo i;
  auto a = mock::BuildSmbios("ASUS", "TULPAR T5 V21.7", "N.1.07Mon08", "x"); ParseRawSmbios(a.data(), a.size(), i);
  CHECK(EvaluateIdentity(i, true).verdict == IdentityVerdict::ManufacturerMismatch);
  auto b = mock::BuildSmbios("MONSTER", "TULPAR T7 V20.3", "N.1.07Mon08", "x"); ParseRawSmbios(b.data(), b.size(), i);
  CHECK(EvaluateIdentity(i, true).verdict == IdentityVerdict::ModelMismatch);
  auto c = mock::BuildSmbios("MONSTER", "TULPAR T5 V21.7", "N.1.08Mon01", "x"); ParseRawSmbios(c.data(), c.size(), i);
  CHECK(EvaluateIdentity(i, true).verdict == IdentityVerdict::BiosMismatch);
  CHECK(EvaluateIdentity(SmbiosInfo(), false).verdict == IdentityVerdict::Unreadable);
}

// ------------------------------------------------------------------ sabitler / allow-list
TEST(profile_table_matches_evidence) {
  CHECK(FindProfile(125)->ctrl == 0x01 && FindProfile(125)->offset == 0);
  CHECK(FindProfile(130)->ctrl == 0x05 && FindProfile(130)->offset == 5);
  CHECK(FindProfile(135)->ctrl == 0x05 && FindProfile(135)->offset == 10);
  CHECK(FindProfile(140)->ctrl == 0x05 && FindProfile(140)->offset == 15);
  CHECK(FindProfile(145)->ctrl == 0x05 && FindProfile(145)->offset == 20);
  CHECK(FindProfile(150)->ctrl == 0x05 && FindProfile(150)->offset == 25);
  CHECK(FindProfile(155) == nullptr && FindProfile(120) == nullptr && FindProfile(0) == nullptr);
  CHECK(ProfileFromEc(0x05, 25) == 150 && ProfileFromEc(0x01, 0) == 125 && ProfileFromEc(0x05, 0) == 125);
  CHECK(ProfileFromEc(0x01, 5) == 0 && ProfileFromEc(0x07, 5) == 0 && ProfileFromEc(0x05, 30) == 0 && ProfileFromEc(0x05, 7) == 0);
}
TEST(write_allow_list_rejects_everything_else) {
  CHECK(IsAllowedEcWrite(0x0743, 0x01) && IsAllowedEcWrite(0x0743, 0x05));
  CHECK(!IsAllowedEcWrite(0x0743, 0x07) && !IsAllowedEcWrite(0x0743, 0x00) && !IsAllowedEcWrite(0x0743, 0x03));
  CHECK(IsAllowedEcWrite(0x0744, 0) && IsAllowedEcWrite(0x0744, 25));
  CHECK(!IsAllowedEcWrite(0x0744, 26) && !IsAllowedEcWrite(0x0744, 30) && !IsAllowedEcWrite(0x0744, 3));
  CHECK(!IsAllowedEcWrite(0x0745, 0) && !IsAllowedEcWrite(0x0746, 0) && !IsAllowedEcWrite(0x0730, 0x3C));
  CHECK(!IsAllowedEcWrite(0x0783, 120) && !IsAllowedEcWrite(0x0000, 0));
  CHECK(IsReadableEcAddress(0x0743) && IsReadableEcAddress(0x0746) && !IsReadableEcAddress(0x0730));
}
TEST(plan_orders_writes_safely) {
  auto up = PowerProfileEngine::PlanRaw(0x01, 0, 0x05, 25);  // 125 -> 150: önce bit (125 W'ta), sonra ofset
  CHECK(up.size() == 2 && up[0].addr == 0x743 && up[0].value == 0x05 && up[1].addr == 0x744 && up[1].value == 25);
  auto down = PowerProfileEngine::PlanRaw(0x05, 25, 0x01, 0);  // 150 -> 125: önce ofset 0, sonra bit
  CHECK(down.size() == 2 && down[0].addr == 0x744 && down[0].value == 0 && down[1].addr == 0x743 && down[1].value == 0x01);
  auto same = PowerProfileEngine::PlanRaw(0x05, 15, 0x05, 15);
  CHECK(same.empty());
  auto only = PowerProfileEngine::PlanRaw(0x05, 15, 0x05, 25);
  CHECK(only.size() == 1 && only[0].addr == 0x744 && only[0].value == 25);
  auto z = PowerProfileEngine::PlanRaw(0x05, 0, 0x01, 0);
  CHECK(z.size() == 1 && z[0].addr == 0x743 && z[0].value == 0x01);
}

// ------------------------------------------------------------------ Aşama 3: engelleme testleri
TEST(model_or_bios_mismatch_blocks_everything_and_never_touches_ec) {
  Rig r;
  r.smb.raw = mock::BuildSmbios("MONSTER", "TULPAR T5 V21.7", "N.1.09Mon99", "GM5TG7Y");
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::BlockedNotReady);
  CHECK(r.ec.writes == 0);
  CHECK(r.ec.reads == 0 && r.ec.probes == 0);  // EC'ye hiç dokunulmaz (okuma dahil)
  CHECK(a.before.HasBlocker(Blocker::IdentityMismatch));
  Rig r2;
  r2.smb.raw = mock::BuildSmbios("DELL", "XPS", "A01", "x");
  CHECK(r2.eng.Apply(140, AllConsent()).outcome == Outcome::BlockedNotReady);
  CHECK(r2.ec.writes == 0 && r2.ec.reads == 0);
}
TEST(driver_is_not_even_probed_when_identity_is_not_exact) {
  Rig r; r.smb.raw = mock::BuildSmbios("DELL", "XPS", "A01", "x");
  LiveState s = r.eng.ReadLive(false);
  CHECK(!s.driverProbed && !s.ecAttempted && r.ec.reads == 0);
  Rig g; LiveState t = g.eng.ReadLive(false);
  CHECK(t.driverProbed && t.driver == DriverState::Connected);
}

TEST(smbios_unreadable_blocks) {
  Rig r; r.smb.ok = false;
  ApplyResult a = r.eng.Apply(140, AllConsent());
  CHECK(a.outcome == Outcome::BlockedNotReady && a.before.HasBlocker(Blocker::IdentityUnreadable));
  CHECK(r.ec.writes == 0 && r.ec.reads == 0);
}
TEST(driver_absent_gives_clear_error_and_no_write) {
  Rig r; r.ec.state = DriverState::Absent; r.ec.winErr = 2;
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::BlockedNotReady);
  CHECK(a.before.HasBlocker(Blocker::DriverAbsent));
  CHECK(a.detail.find("ACPIDriver") != std::string::npos);
  CHECK(r.ec.writes == 0 && r.ec.reads == 0);
  Rig r2; r2.ec.state = DriverState::AccessDenied; r2.ec.winErr = 5;
  CHECK(r2.eng.Apply(140, AllConsent()).before.HasBlocker(Blocker::DriverAccessDenied));
  CHECK(r2.ec.writes == 0);
}
TEST(ec_read_failure_blocks) {
  Rig r; r.ec.failAllReads = true;
  ApplyResult a = r.eng.Apply(145, AllConsent());
  CHECK(a.outcome == Outcome::BlockedNotReady && a.before.HasBlocker(Blocker::EcReadInvalid));
  CHECK(r.ec.writes == 0);
}
TEST(stale_echo_of_address_is_not_mistaken_for_a_valid_state) {
  // IOCTL başarısız olursa OEM DLL adresin düşük baytını döndürür: 0x43 / 0x44 geçerli durum değildir.
  Rig r; r.ec.echoAddress = true;
  ApplyResult a = r.eng.Apply(125, AllConsent());
  CHECK(a.outcome == Outcome::BlockedNotReady && a.before.HasBlocker(Blocker::EcStateUnexpected));
  CHECK(r.ec.writes == 0);
}
TEST(unexpected_ec_states_block) {
  { Rig r; r.ec.mem[0x743] = 0x07; CHECK(r.eng.Apply(140, AllConsent()).outcome == Outcome::BlockedNotReady); CHECK(r.ec.writes == 0); }
  { Rig r; r.ec.mem[0x743] = 0x00; CHECK(r.eng.Apply(140, AllConsent()).outcome == Outcome::BlockedNotReady); CHECK(r.ec.writes == 0); }
  { Rig r; r.ec.mem[0x744] = 12; CHECK(r.eng.Apply(140, AllConsent()).outcome == Outcome::BlockedNotReady); CHECK(r.ec.writes == 0); }
  { Rig r; r.ec.SetOemOff(); r.ec.mem[0x744] = 10; CHECK(r.eng.Apply(140, AllConsent()).outcome == Outcome::BlockedNotReady); CHECK(r.ec.writes == 0); }
  { Rig r; r.ec.mem[0x745] = 3; CHECK(r.eng.Apply(140, AllConsent()).outcome == Outcome::BlockedNotReady); CHECK(r.ec.writes == 0); }
  { Rig r; r.ec.mem[0x746] = 9; CHECK(r.eng.Apply(140, AllConsent()).outcome == Outcome::BlockedNotReady); CHECK(r.ec.writes == 0); }
}
TEST(nvml_missing_or_wrong_gpu_blocks_writes) {
  { Rig r; r.nv.initOk = false; ApplyResult a = r.eng.Apply(150, AllConsent());
    CHECK(a.outcome == Outcome::BlockedNotReady && a.before.HasBlocker(Blocker::NvmlUnavailable)); CHECK(r.ec.writes == 0); }
  { Rig r; r.nv.dynOk = false; CHECK(r.eng.Apply(150, AllConsent()).outcome == Outcome::BlockedNotReady); CHECK(r.ec.writes == 0); }
  { Rig r; r.nv.name = "NVIDIA GeForce RTX 4090 Laptop GPU"; ApplyResult a = r.eng.Apply(150, AllConsent());
    CHECK(a.before.HasBlocker(Blocker::GpuMismatch)); CHECK(r.ec.writes == 0); }
  { Rig r; r.nv.name = "NVIDIA GeForce RTX 3070"; CHECK(r.eng.Apply(150, AllConsent()).before.HasBlocker(Blocker::GpuMismatch)); }
  { Rig r; r.nv.maxW = 140; ApplyResult a = r.eng.Apply(145, AllConsent());
    CHECK(a.before.HasBlocker(Blocker::MaxLimitMismatch)); CHECK(r.ec.writes == 0); }
}
TEST(consent_temperature_and_vbios_max_gates) {
  { Rig r; ApplyOptions o; ApplyResult a = r.eng.Apply(150, o); CHECK(a.outcome == Outcome::BlockedConsent); CHECK(r.ec.writes == 0); }
  { Rig r; ApplyOptions o; o.consentOverOem = true; CHECK(r.eng.Apply(145, o).outcome == Outcome::Ok); }
  { Rig r; ApplyOptions o; ApplyResult a = r.eng.Apply(130, o); CHECK(a.outcome == Outcome::BlockedConsent); CHECK(r.ec.writes == 0); }
  { Rig r; ApplyOptions o; o.consentUnverified = true; CHECK(r.eng.Apply(135, o).outcome == Outcome::Ok); }
  { Rig r; ApplyOptions o; CHECK(r.eng.Apply(140, o).outcome == Outcome::AlreadyActive); }   // OEM profili onay istemez
  { Rig r; ApplyOptions o; CHECK(r.eng.Apply(125, o).outcome == Outcome::Ok); }               // 125 W (OEM kapalı) onay istemez
  { Rig r; r.nv.temp = 81; ApplyResult a = r.eng.Apply(150, AllConsent()); CHECK(a.outcome == Outcome::BlockedTemp); CHECK(r.ec.writes == 0); }
  { Rig r; r.nv.temp = 90; ApplyResult a = r.eng.Apply(125, AllConsent()); CHECK(a.outcome == Outcome::Ok); }  // düşürmek sıcakta serbest
  { Rig r; ApplyResult a = r.eng.Apply(155, AllConsent()); CHECK(a.outcome == Outcome::BlockedUnknownProfile); CHECK(r.ec.writes == 0); }
}

// ------------------------------------------------------------------ Aşama 3: profil eşleştirme (125..150)
TEST(missing_board_tag_requires_explicit_model_confirmation_before_any_write) {
  Rig r;
  r.smb.raw = mock::BuildSmbios("MONSTER", "TULPAR T5 V21.7", "N.1.07Mon08", "x");   // kart etiketi (GM5TG7Y) YOK
  LiveState s = r.eng.ReadLive(false);
  CHECK(s.identity.verdict == IdentityVerdict::Exact && !s.identity.boardConfirmed);
  CHECK(s.CanWrite());                                       // okuma/durum gösterimi engellenmez
  ApplyOptions o = AllConsent();                              // diğer tüm onaylar var, model teyidi yok
  ApplyResult a = r.eng.Apply(145, o);
  CHECK(a.outcome == Outcome::BlockedConsent);
  CHECK(r.ec.writes == 0);
  o.consentModel = true;
  CHECK(r.eng.Apply(145, o).outcome == Outcome::Ok);
  Rig r2;                                                     // etiket var -> teyit gerekmez
  ApplyOptions o2; o2.consentOverOem = true;
  CHECK(r2.eng.Apply(145, o2).outcome == Outcome::Ok);
}

TEST(all_six_profiles_apply_and_verify_from_oem_on_baseline) {
  const int ws[] = {125, 130, 135, 140, 145, 150};
  for (int w : ws) {
    Rig r;  // taban: OEM ON 140 W
    ApplyResult a = r.eng.Apply(w, AllConsent());
    CHECK(w == 140 ? a.outcome == Outcome::AlreadyActive : a.outcome == Outcome::Ok);
    const ProfileDef* d = FindProfile(w);
    CHECK(r.ec.mem[0x743] == d->ctrl && r.ec.mem[0x744] == d->offset);
    CHECK(std::fabs(r.nv.limit - w) < 0.01);
    CHECK(a.ecVerified && a.nvVerified);
    CHECK(r.ec.mem[0x745] == 0 && r.ec.mem[0x746] == 0);  // dokunulmaz
    for (auto& wl : r.ec.writeLog) CHECK(wl.first == 0x743 || wl.first == 0x744);
    CHECK(a.after.activeProfile == w);
  }
}
TEST(all_six_profiles_apply_from_oem_off_baseline) {
  const int ws[] = {125, 130, 135, 140, 145, 150};
  for (int w : ws) {
    Rig r; r.ec.SetOemOff(); r.nv.limit = 125;
    ApplyResult a = r.eng.Apply(w, AllConsent());
    CHECK(w == 125 ? a.outcome == Outcome::AlreadyActive : a.outcome == Outcome::Ok);
    const ProfileDef* d = FindProfile(w);
    CHECK(r.ec.mem[0x743] == d->ctrl && r.ec.mem[0x744] == d->offset);
    CHECK(std::fabs(r.nv.limit - w) < 0.01);
    if (w > 125) {  // yükselirken: önce 0x0743, sonra 0x0744
      CHECK(r.ec.writeLog.size() == 2 && r.ec.writeLog[0].first == 0x743 && r.ec.writeLog[1].first == 0x744);
    }
  }
}
TEST(descending_to_125_writes_offset_then_control_and_restores_oem_off_state) {
  Rig r; r.ec.SetOemOn(25); r.nv.limit = 150;
  ApplyResult a = r.eng.Apply(125, AllConsent());
  CHECK(a.outcome == Outcome::Ok);
  CHECK(r.ec.mem[0x743] == 0x01 && r.ec.mem[0x744] == 0);
  CHECK(r.ec.writeLog.size() == 2 && r.ec.writeLog[0].first == 0x744 && r.ec.writeLog[0].second == 0 &&
        r.ec.writeLog[1].first == 0x743 && r.ec.writeLog[1].second == 0x01);
}
TEST(already_active_does_not_write) {
  Rig r; r.ec.SetOemOn(25); r.nv.limit = 150;
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::AlreadyActive && r.ec.writes == 0);
}
TEST(nvml_lag_is_tolerated_within_timeout) {
  Rig r; r.nv.lagSamples = 5;  // NVML yeni değeri 5 örnek sonra gösterir (~2 sn)
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::Ok);
}

// ------------------------------------------------------------------ Aşama 3: geri alma
TEST(readback_mismatch_rolls_back) {
  Rig r; r.ec.dropWrites = true;  // yazma "başarılı" döner ama EC değişmez
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::FailedNoChange);  // EC zaten eski değerde: geri alınacak bir şey yok
  CHECK(a.rollbackVerified);
  CHECK(r.ec.mem[0x743] == 0x05 && r.ec.mem[0x744] == 15);
  CHECK(r.log.Has("read-back mismatch"));
}
TEST(partial_write_failure_rolls_back_to_previous_values) {
  Rig r; r.ec.SetOemOff(); r.nv.limit = 125;
  r.ec.failWriteNumber = 2;  // ilk yazma (0x0743=0x05) başarılı, ikinci (0x0744) hata
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::FailedRolledBack);
  CHECK(a.rollbackVerified && a.rolledBack);
  CHECK(r.ec.mem[0x743] == 0x01 && r.ec.mem[0x744] == 0);
  CHECK(std::fabs(r.nv.limit - 125) < 0.01);
}
TEST(nvml_does_not_follow_so_apply_fails_and_restores) {
  Rig r; r.nv.followEc = false; r.nv.limit = 140;  // NVIDIA hiç değişmiyor
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::FailedRolledBack);
  CHECK(r.ec.mem[0x744] == 15);  // EC eski değere döndü
  CHECK(a.ecVerified && !a.nvVerified);
  CHECK(a.detail.find("NVIDIA power limit did not reach") != std::string::npos);
}
TEST(external_override_during_final_confirmation_is_caught) {
  // NVML'nin 3. okumasında (= son teyit aşaması) "başka yazılım" EC'yi eski değere (15) geri alır.
  struct Hook : mock::Nv {
    using Nv::Nv;
    int polls = 0;
    int triggerAt = 3;
    bool ReadDynamic(NvDynamic& o) override {
      if (++polls == triggerAt) ec.mem[0x744] = 15;
      return Nv::ReadDynamic(o);
    }
  };
  mock::Clock clk; mock::MemLog log; mock::Ec ec; Hook nv(ec); mock::Smbios smb; smb.raw = mock::GoodSmbios();
  PowerProfileEngine eng(ec, nv, smb, clk, log);
  ApplyResult a = eng.Apply(150, AllConsent());
  CHECK(a.outcome != Outcome::Ok);
  CHECK(!a.Succeeded());
  CHECK(log.Has("final confirmation"));
  CHECK(a.ecVerified && a.nvVerified);   // ilk iki doğrulama geçti; sorun son teyitte yakalandı
  CHECK(ec.mem[0x743] == 0x05 && ec.mem[0x744] == 15);  // sonuçta bilinen önceki duruma dönüldü
}
TEST(rollback_failure_is_reported_as_critical_not_success) {
  Rig r; r.ec.failAllReadsAfterWrites = true;  // ilk yazmadan sonra EC okunamaz, geri alma doğrulanamaz
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::FailedRollbackUnverified);
  CHECK(!a.rollbackVerified && !a.Succeeded());
}
TEST(rapid_sequence_of_profile_changes_stays_consistent) {
  Rig r;
  const int seq[] = {150, 125, 145, 130, 140, 135, 150, 125, 140};
  for (int w : seq) {
    ApplyResult a = r.eng.Apply(w, AllConsent());
    CHECK(a.Succeeded());
    CHECK(std::fabs(r.nv.limit - w) < 0.01);
  }
  for (auto& wl : r.ec.writeLog) CHECK(IsAllowedEcWrite(wl.first, wl.second));
}

// ------------------------------------------------------------------ gözden geçirme düzeltmeleri
TEST(plan_from_inconsistent_state_clears_offset_before_enabling_ctgp) {
  // (0x01,25) tutarsız durumundan 140 W'a: bit açılmadan ÖNCE ofset 0'lanmalı; (0x05,25)=150 W geçişi olmamalı.
  auto p = PowerProfileEngine::PlanRaw(0x01, 25, 0x05, 15);
  CHECK(p.size() == 3);
  CHECK(p[0].addr == 0x744 && p[0].value == 0);
  CHECK(p[1].addr == 0x743 && p[1].value == 0x05);
  CHECK(p[2].addr == 0x744 && p[2].value == 15);
  auto q = PowerProfileEngine::PlanRaw(0x01, 25, 0x01, 0);
  CHECK(q.size() == 1 && q[0].addr == 0x744 && q[0].value == 0);
}
TEST(ec_changed_between_precheck_and_first_write_aborts_without_any_write) {
  Rig r;  // (0x05,15) = 140 W
  // ReadLive 4 okuma yapar (0x743..0x746); 5. okuma yazmadan hemen önceki yeniden okumadır: o anda dış yazılım EC'yi değiştirir.
  r.ec.beforeRead = [](mock::Ec& e) { if (e.reads == 5) e.SetOemOff(); };
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::FailedNoChange);
  CHECK(r.ec.writes == 0);
  CHECK(a.detail.find("changed between") != std::string::npos);
  CHECK(!a.Succeeded());
}
TEST(nvml_disagreeing_with_ec_before_write_blocks_and_never_verifies_against_stale_value) {
  // EC = 150 W ama NVIDIA 140 W gösterip hiç izlemiyor -> hiçbir şey yazılmaz, geri alma hedefi uydurulmaz.
  Rig r; r.ec.SetOemOn(25); r.nv.followEc = false; r.nv.limit = 140;
  ApplyResult a = r.eng.Apply(130, AllConsent());
  CHECK(a.outcome == Outcome::BlockedNotReady);
  CHECK(r.ec.writes == 0);
  CHECK(a.detail.find("disagree") != std::string::npos);
  // EC = 145 W, NVIDIA bayat 150 W: eskiden Apply(150) ilk örnekte "doğrulanmış" sayılırdı.
  Rig q; q.ec.SetOemOn(20); q.nv.followEc = false; q.nv.limit = 150;
  ApplyResult b = q.eng.Apply(150, AllConsent());
  CHECK(b.outcome == Outcome::BlockedNotReady && q.ec.writes == 0 && !b.nvVerified);
}
TEST(unreadable_temperature_blocks_raising_above_oem_but_not_oem_or_lower) {
  Rig r; r.nv.haveTempSensor = false;
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::BlockedTemp && r.ec.writes == 0);
  ApplyResult b = r.eng.Apply(125, AllConsent());  // düşürmek her zaman serbest
  CHECK(b.Succeeded());
}
TEST(unverified_rollback_latches_writes_off_until_manual_refresh_sees_clean_state) {
  Rig r; r.ec.failAllReadsAfterWrites = true;
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::FailedRollbackUnverified);
  CHECK(r.eng.FaultLatched());
  // EC okunabilir hâle gelse bile, kilit otomatik açılmaz: sıradan okuma ve hafif yoklama engeli korur.
  r.ec.failAllReadsAfterWrites = false;
  r.ec.SetOemOn(15); r.nv.limit = 140;
  LiveState s = r.eng.ReadLive(false);
  CHECK(s.HasBlocker(Blocker::FaultLatched) && !s.CanWrite());
  LiveState l = r.eng.ReadLive(true, true);  // hafif okuma kilidi AÇAMAZ
  CHECK(l.HasBlocker(Blocker::FaultLatched));
  const int w0 = r.ec.writes;
  ApplyResult b = r.eng.Apply(125, AllConsent());
  CHECK(b.outcome == Outcome::BlockedNotReady && r.ec.writes == w0);
  // Kullanıcı "Yenile" der ve durum temiz+tutarlı: kilit açılır.
  LiveState c = r.eng.ReadLive(false, true);
  CHECK(!c.HasBlocker(Blocker::FaultLatched) && c.CanWrite() && !r.eng.FaultLatched());
  CHECK(r.eng.Apply(125, AllConsent()).Succeeded());
}
TEST(fault_latch_stays_if_manual_refresh_still_sees_inconsistent_state) {
  Rig r; r.ec.failAllReadsAfterWrites = true;
  CHECK(r.eng.Apply(150, AllConsent()).outcome == Outcome::FailedRollbackUnverified);
  r.ec.failAllReadsAfterWrites = false;
  r.nv.followEc = false; r.nv.limit = 125;  // EC 140 W, NVIDIA 125 W: tutarsız
  r.ec.SetOemOn(15);
  LiveState c = r.eng.ReadLive(false, true);
  CHECK(c.HasBlocker(Blocker::FaultLatched) && r.eng.FaultLatched());
}
TEST(exception_during_apply_after_first_write_triggers_rollback_not_a_stuck_half_write) {
  Rig r;
  // ReadLive(1) + WaitNvml/First(2..): yazmadan sonraki NVML okumasında bad_alloc; sonra kurtarma okumaları çalışır.
  r.nv.throwAtDynCall = 3;
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(!a.Succeeded());
  CHECK(a.outcome == Outcome::FailedRolledBack);
  CHECK(r.ec.mem[0x743] == 0x05 && r.ec.mem[0x744] == 15);   // EC önceki duruma döndü
  CHECK(a.detail.find("internal exception") != std::string::npos);
  CHECK(!r.eng.FaultLatched());
}
TEST(persistent_exception_after_write_is_reported_as_unverified_and_latches) {
  Rig r; r.nv.throwAlwaysAfterFirst = true;
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::FailedRollbackUnverified && !a.Succeeded());
  CHECK(r.eng.FaultLatched());
  CHECK(r.ec.mem[0x743] == 0x05 && r.ec.mem[0x744] == 15);   // EC yine de geri yazıldı (NVML doğrulanamadı)
}
TEST(sibling_gpu_models_are_rejected_and_nvml_recovers_after_a_transient_failure) {
  Rig r; r.nv.name = "NVIDIA GeForce RTX 3070 Ti Laptop GPU";
  LiveState s = r.eng.ReadLive(false);
  CHECK(s.HasBlocker(Blocker::GpuMismatch) && !s.CanWrite());
  Rig q; q.nv.dynOk = false;
  CHECK(q.eng.ReadLive(false).HasBlocker(Blocker::NvmlUnavailable));
  q.nv.dynOk = true;  // GPU sıfırlandı/yeniden göründü: yeniden başlatma olmadan toparlanır
  CHECK(q.eng.ReadLive(false).CanWrite());
}
TEST(rollback_never_writes_dependent_step_after_a_failed_step) {
  // 125 -> 150 yükseliyor: 0x743=0x05 yazılır, 0x744 yazımı hata verir; geri alma sırasında da 0x744 hata verir.
  // Geri alma (0x05,0)->(0x01,0): yalnız bit yazımı gerekir ve olur; 0x744'e anlamsız ikinci yazma gitmez.
  Rig r; r.ec.SetOemOff(); r.nv.limit = 125;
  r.ec.failWriteNumber = 2;
  ApplyResult a = r.eng.Apply(150, AllConsent());
  CHECK(a.outcome == Outcome::FailedRolledBack);
  int ctrlWrites = 0, offWrites = 0;
  for (auto& w : r.ec.writeLog) { if (w.first == 0x743) ++ctrlWrites; else if (w.first == 0x744) ++offWrites; }
  CHECK(ctrlWrites == 2);  // 0x05 sonra geri 0x01
  CHECK(offWrites == 1);   // yalnız hata veren tek deneme; sıfırlama gereksizdi
  // Hiçbir yazma sonrası ara durum (ctrl kapalı + sıfırdan farklı ofset) oluşmadı.
  uint8_t c = 0x01, o = 0;
  int n = 0;
  for (auto& w : r.ec.writeLog) {
    ++n;
    if (n == 2) continue;  // 2. yazma hata verdi: EC'de etkisizdi
    if (w.first == 0x743) c = w.second; else o = w.second;
    CHECK(!(c == 0x01 && o != 0));
  }
}

// ------------------------------------------------------------------ ReadLive
TEST(readlive_reports_actual_state_not_assumed) {
  Rig r; r.ec.SetOemOn(25); r.nv.limit = 150;
  LiveState s = r.eng.ReadLive(false);
  CHECK(s.CanWrite() && s.activeProfile == 150 && s.consistent && r.ec.writes == 0);
  Rig r2; r2.ec.SetOemOn(25); r2.nv.followEc = false; r2.nv.limit = 140;  // EC 150 diyor, NVIDIA 140
  LiveState s2 = r2.eng.ReadLive(false);
  CHECK(s2.ecProfile == 150 && !s2.consistent && s2.activeProfile == 0);
  Rig r3; r3.ec.SetOemOff();
  LiveState s3 = r3.eng.ReadLive(false);
  CHECK(s3.activeProfile == 125);
}
TEST(light_poll_reads_only_two_ec_registers) {
  Rig r;
  r.eng.ReadLive(false);
  r.ec.readLog.clear(); r.ec.reads = 0;
  r.eng.ReadLive(true);
  CHECK(r.ec.reads == 2);
}

// ------------------------------------------------------------------ Dış müdahale algılama
// ------------------------------------------------------------------ Ayarlar
TEST(settings_defaults_are_off_and_roundtrip) {
  Settings def;
  CHECK(!def.restoreOnExit && !def.consentOverOem && !def.consentUnverified && !def.consentModel && !def.oem.Present());
  Settings s;
  s.restoreOnExit = true; s.consentOverOem = true; s.consentModel = true;
  s.oem.ownedSha = std::string(64, 'a'); s.oem.ownedWatts = 150;
  Settings t = ParseSettings(SerializeSettings(s));
  CHECK(t.restoreOnExit && t.consentOverOem && t.consentModel && !t.consentUnverified);
  CHECK(t.oem.Present() && t.oem.ownedWatts == 150 && t.oem.ownedSha == s.oem.ownedSha);
  CHECK(SerializeSettings(Settings()).find("oem_owned") == std::string::npos);  // sahiplik kaydı yalnız varsa yazılır
}
TEST(settings_tolerate_garbage_reject_arbitrary_values_and_ignore_removed_keys) {
  Settings t = ParseSettings("\xEF\xBB\xBFrestore_on_exit=1\nrandom\n=\nconsent_model=abc\n");
  CHECK(t.restoreOnExit && !t.consentModel);  // BOM tolere edilir; bozuk satırlar yok sayılır
  // ownership: yarım kayıt ya da bozuk SHA/watt KANIT SAYILMAZ
  CHECK(!ParseSettings("oem_owned_sha=" + std::string(64, 'a') + "\n").oem.Present());
  CHECK(!ParseSettings("oem_owned_watts=150\n").oem.Present());
  CHECK(!ParseSettings("oem_owned_sha=zz\noem_owned_watts=150\n").oem.Present());
  CHECK(!ParseSettings("oem_owned_sha=" + std::string(64, 'a') + "\noem_owned_watts=999\n").oem.Present());
  CHECK(ParseSettings("oem_owned_sha=" + std::string(64, 'a') + "\noem_owned_watts=135\n").oem.Present());
  // önceki sürümlerin anahtarları (izleme, otomatik yeniden uygulama, ekran uyanışı, kalıcılık tercihi, son profil, açılışta uygula) okunmaz
  Settings v = ParseSettings("monitor=1\nauto_reapply=1\nwake_reapply=1\npersist_oem=1\nlast_profile=150\nstartup_apply=1\nec_write=0x0745\n");
  CHECK(!v.restoreOnExit && !v.consentOverOem && !v.oem.Present());
  CHECK(SerializeSettings(v).find("monitor") == std::string::npos && SerializeSettings(v).find("persist_oem") == std::string::npos);
}

// ------------------------------------------------------------------ OEM profil modülü: saf mantık + depo + kalıcı uygulama işlem akışı
#include "test_oem_json.h"
#include "test_oem_store.h"
#include "test_persist_view.h"
#include "test_task_xml.h"

// Kullanım: mgp_tests [süzgeç]   (süzgeç verilirse yalnız adı onu İÇEREN testler çalışır; verilmezse hepsi)
int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int failedTests = 0;
  size_t ran = 0;
  for (auto& t : Tests()) {
    if (filter && !std::strstr(t.name, filter)) continue;
    ++ran;
    const int before = g_fail;
    std::printf("[ RUN  ] %s\n", t.name);
    t.fn();
    if (g_fail != before) { ++failedTests; std::printf("[ FAIL ] %s\n", t.name); }
    else std::printf("[  OK  ] %s\n", t.name);
  }
  std::printf("\n%zu test, %d kontrol gecti, %d kontrol basarisiz, %d test basarisiz\n", ran, g_pass, g_fail, failedTests);
  return g_fail == 0 ? 0 : 1;
}
