// OEM kalıcılık modülünün SAF MANTIK testleri: SHA-256, katı UTF-8, katı JSON, profil şeması, yama, ad kalıpları, ayarlar.
// (test_main.cpp içinden dahil edilir; TEST()/CHECK() makroları orada tanımlıdır.) Gerçek donanım/Windows doğrulaması DEĞİLDİR.
#pragma once
#include "../src/core/oem_json.h"
#include "../src/core/oem_names.h"
#include "../src/core/sha256.h"
#include "oem_mock.h"

namespace oemtest {

inline std::string Rep(std::string s, const std::string& a, const std::string& b) {
  const size_t p = s.find(a);
  if (p != std::string::npos) s.replace(p, a.size(), b);
  return s;
}
inline std::vector<uint8_t> V(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

struct Variant {
  const char* name;
  std::vector<uint8_t> bytes;
};
// Kabul edilmesi gereken biçim çeşitleri (hepsi hedef = 140).
inline std::vector<Variant> GoodVariants() {
  std::vector<Variant> v;
  v.push_back({"lf", mock::Profile(140, "\n")});
  v.push_back({"crlf", mock::Profile(140, "\r\n")});
  v.push_back({"compact", mock::Profile(140, "")});
  v.push_back({"bom+lf", mock::Profile(140, "\n", true)});
  v.push_back({"bom+crlf", mock::Profile(140, "\r\n", true)});
  v.push_back({"tabs", V(Rep(mock::ProfileText(140, "\n"), "  ", "\t"))});
  v.push_back({"wide-spaces", V(Rep(Rep(mock::ProfileText(140, "\r\n"), "\"ConfigurableTGPTarget\": 140", "\"ConfigurableTGPTarget\"   :    140   "), "\"Activated\": true", "\"Activated\"\t:\ttrue"))});
  v.push_back({"unicode-escape", V(Rep(mock::ProfileText(140, "\n"), "Turbo 1", "Turbo \\u00c7 \\ud83d\\ude00"))});
  v.push_back({"int-flags", V(Rep(Rep(mock::ProfileText(140, "\n"), "\"Activated\": true", "\"Activated\": 1"), "\"ConfigurableTGPSwitch\": 1", "\"ConfigurableTGPSwitch\": true"))});
  v.push_back({"mixed-eol", V(Rep(mock::ProfileText(140, "\n"), "\"CPU\"", "\r\n\"CPU\""))});
  return v;
}

}  // namespace oemtest

// ------------------------------------------------------------------ SHA-256
TEST(sha256_known_answer_vectors_and_block_boundaries) {
  auto H = [](const std::string& s) { return Sha256Hex(s); };
  CHECK(H("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(H("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(H("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(H("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
        "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
  struct { size_t n; const char* h; } edge[] = {
      {1, "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"},
      {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
      {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
      {57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6"},
      {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
      {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
      {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
      {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
      {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
      {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
      {1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"},
      {1000000, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
  };
  for (auto& e : edge) CHECK(H(std::string(e.n, 'a')) == e.h);
}
TEST(sha256_incremental_updates_equal_one_shot_for_every_split) {
  std::string data;
  for (int i = 0; i < 300; ++i) data.push_back(static_cast<char>(i * 7 + 3));
  const std::string want = Sha256Hex(data);
  for (size_t a = 0; a <= data.size(); a += 13) {
    for (size_t b = a; b <= data.size(); b += 29) {
      Sha256 s;
      s.Update(reinterpret_cast<const uint8_t*>(data.data()), a);
      s.Update(reinterpret_cast<const uint8_t*>(data.data()) + a, b - a);
      s.Update(reinterpret_cast<const uint8_t*>(data.data()) + b, data.size() - b);
      uint8_t out[32];
      s.Final(out);
      static const char* hx = "0123456789abcdef";
      std::string h;
      for (int k = 0; k < 32; ++k) { h.push_back(hx[out[k] >> 4]); h.push_back(hx[out[k] & 15]); }
      CHECK(h == want);
    }
  }
}
TEST(sha256_hex_validator_is_strict) {
  CHECK(IsSha256Hex(std::string(64, 'a')) && IsSha256Hex(Sha256Hex(std::string("x"))));
  CHECK(!IsSha256Hex("") && !IsSha256Hex(std::string(63, 'a')) && !IsSha256Hex(std::string(65, 'a')));
  CHECK(!IsSha256Hex(std::string(64, 'A')) && !IsSha256Hex(std::string(63, 'a') + "g") && !IsSha256Hex(std::string(63, 'a') + " "));
}

// ------------------------------------------------------------------ UTF-8 / zarf
TEST(utf8_strict_accepts_valid_and_rejects_every_malformed_class) {
  using namespace mgp::oem;
  auto U = [](std::initializer_list<int> b) { std::vector<uint8_t> v; for (int x : b) v.push_back(static_cast<uint8_t>(x)); return IsStrictUtf8(v.data(), v.size()); };
  CHECK(U({}) && U({0x41}) && U({0xC3, 0xA7}) && U({0xE2, 0x82, 0xAC}) && U({0xF0, 0x9F, 0x98, 0x80}) && U({0x7F}) && U({0xC2, 0x80}) && U({0xDF, 0xBF}));
  CHECK(U({0xE0, 0xA0, 0x80}) && U({0xED, 0x9F, 0xBF}) && U({0xEE, 0x80, 0x80}) && U({0xEF, 0xBF, 0xBF}) && U({0xF0, 0x90, 0x80, 0x80}) && U({0xF4, 0x8F, 0xBF, 0xBF}));
  CHECK(!U({0xC0, 0x80}) && !U({0xC1, 0xBF}) && !U({0xE0, 0x80, 0x80}) && !U({0xE0, 0x9F, 0xBF}) && !U({0xF0, 0x80, 0x80, 0x80}) && !U({0xF0, 0x8F, 0xBF, 0xBF}));
  CHECK(!U({0xED, 0xA0, 0x80}) && !U({0xED, 0xBF, 0xBF}));                        // vekil çiftler
  CHECK(!U({0xF4, 0x90, 0x80, 0x80}) && !U({0xF5, 0x80, 0x80, 0x80}) && !U({0xF8, 0x88, 0x80, 0x80, 0x80}));  // > U+10FFFF
  CHECK(!U({0x80}) && !U({0xBF}) && !U({0xFE}) && !U({0xFF}));                    // yalnız devam baytı / yasak baytlar
  CHECK(!U({0xC3}) && !U({0xE2, 0x82}) && !U({0xF0, 0x9F, 0x98}) && !U({0x41, 0xC3}));  // kesik diziler (dizi sonu)
  CHECK(!U({0xC3, 0x28}) && !U({0xE2, 0x28, 0xA1}) && !U({0xE2, 0x82, 0x28}) && !U({0xF0, 0x28, 0x8C, 0xBC}));  // bozuk devam baytı
}
TEST(envelope_rules_empty_oversize_utf16_utf32_bom_and_invalid_utf8) {
  using namespace mgp::oem;
  Envelope e;
  CHECK(CheckEnvelope({}, e) == EnvelopeError::Empty);
  CHECK(CheckEnvelope({0xEF, 0xBB, 0xBF}, e) == EnvelopeError::Empty);  // yalnız BOM
  CHECK(CheckEnvelope(std::vector<uint8_t>(kMaxJsonBytes + 1, ' '), e) == EnvelopeError::TooLarge);
  CHECK(CheckEnvelope({0xFF, 0xFE, '{', 0}, e) == EnvelopeError::Utf16OrUtf32);
  CHECK(CheckEnvelope({0xFE, 0xFF, 0, '{'}, e) == EnvelopeError::Utf16OrUtf32);
  CHECK(CheckEnvelope({0xFF, 0xFE, 0, 0, '{', 0, 0, 0}, e) == EnvelopeError::Utf16OrUtf32);
  CHECK(CheckEnvelope({'{', 0xFF, '}'}, e) == EnvelopeError::InvalidUtf8);
  CHECK(CheckEnvelope({0xEF, 0xBB, 0xBF, '{', '}'}, e) == EnvelopeError::None && e.bom && e.bodyOffset == 3);
  CHECK(CheckEnvelope({'{', '}'}, e) == EnvelopeError::None && !e.bom && e.bodyOffset == 0);
}

// ------------------------------------------------------------------ profil ayrıştırma
TEST(profile_parse_accepts_every_realistic_layout_and_locates_the_three_digits) {
  using namespace oemtest;
  for (const Variant& v : GoodVariants()) {
    const mgp::oem::ProfileFacts f = mgp::oem::ParseProfile(v.bytes);
    if (!f.ok) std::printf("    variant %s: %s\n", v.name, f.detail.c_str());
    CHECK(f.ok && f.target == 140 && f.activated == 1 && f.tgpSwitch == 1 && f.dynBoostSwitch == 0);
    if (f.ok) {
      CHECK(f.targetFileOffset + 3 <= v.bytes.size());
      CHECK(std::string(v.bytes.begin() + static_cast<long>(f.targetFileOffset), v.bytes.begin() + static_cast<long>(f.targetFileOffset) + 3) == "140");
      CHECK(f.bom == (v.bytes.size() > 2 && v.bytes[0] == 0xEF));
    }
  }
}
TEST(profile_parse_rejects_malformed_ambiguous_and_wrongly_typed_files) {
  using namespace oemtest;
  using mgp::oem::Defect;
  const std::string base = mock::ProfileText(140, "\n");
  struct Bad { const char* name; std::vector<uint8_t> bytes; };
  std::vector<Bad> bad;
  bad.push_back({"truncated", V(base.substr(0, base.size() / 2))});
  bad.push_back({"trailing-garbage", V(base + "x")});
  bad.push_back({"trailing-second-value", V(base + "{}")});
  bad.push_back({"duplicate-Activated", V(Rep(base, "\"Activated\": true,", "\"Activated\": true, \"Activated\": false,"))});
  bad.push_back({"duplicate-target-same-object", V(Rep(base, "\"DynamicBoost\": 15,", "\"DynamicBoost\": 15, \"ConfigurableTGPTarget\": 150,"))});
  bad.push_back({"second-target-in-other-object", V(Rep(base, "\"PL1\": 55", "\"PL1\": 55, \"ConfigurableTGPTarget\": 150"))});
  bad.push_back({"target-as-string", V(Rep(base, "\"ConfigurableTGPTarget\": 140", "\"ConfigurableTGPTarget\": \"140\""))});
  bad.push_back({"target-float", V(Rep(base, "\"ConfigurableTGPTarget\": 140", "\"ConfigurableTGPTarget\": 140.0"))});
  bad.push_back({"target-exponent", V(Rep(base, "\"ConfigurableTGPTarget\": 140", "\"ConfigurableTGPTarget\": 1.4e2"))});
  bad.push_back({"target-two-digits", V(Rep(base, "\"ConfigurableTGPTarget\": 140", "\"ConfigurableTGPTarget\": 99"))});
  bad.push_back({"target-four-digits", V(Rep(base, "\"ConfigurableTGPTarget\": 140", "\"ConfigurableTGPTarget\": 1400"))});
  bad.push_back({"target-negative", V(Rep(base, "\"ConfigurableTGPTarget\": 140", "\"ConfigurableTGPTarget\": -140"))});
  bad.push_back({"target-leading-zero", V(Rep(base, "\"ConfigurableTGPTarget\": 140", "\"ConfigurableTGPTarget\": 014"))});
  bad.push_back({"target-null", V(Rep(base, "\"ConfigurableTGPTarget\": 140", "\"ConfigurableTGPTarget\": null"))});
  bad.push_back({"target-bool", V(Rep(base, "\"ConfigurableTGPTarget\": 140", "\"ConfigurableTGPTarget\": true"))});
  bad.push_back({"missing-target", V(Rep(base, ",\n    \"ConfigurableTGPTarget\": 140", ""))});
  bad.push_back({"missing-GPU", V(Rep(base, "\"GPU\"", "\"GPX\""))});
  bad.push_back({"GPU-is-array", V(Rep(base, "\"GPU\": {", "\"GPU\": [ {"))});
  bad.push_back({"Activated-string", V(Rep(base, "\"Activated\": true", "\"Activated\": \"true\""))});
  bad.push_back({"Activated-missing", V(Rep(base, "\"Activated\": true,", ""))});
  bad.push_back({"switch-two", V(Rep(base, "\"ConfigurableTGPSwitch\": 1", "\"ConfigurableTGPSwitch\": 2"))});
  bad.push_back({"trailing-comma", V(Rep(base, "\"DynamicBoost\": 15,", "\"DynamicBoost\": 15,,"))});
  bad.push_back({"single-quotes", V(Rep(base, "\"Name\"", "'Name'"))});
  bad.push_back({"comment", V(Rep(base, "\"CPU\"", "/* x */ \"CPU\""))});
  bad.push_back({"control-char-in-string", V(Rep(base, "Turbo 1", std::string("Turbo\x01 1")))});
  bad.push_back({"nul-byte", V(Rep(base, "Turbo 1", std::string("Turbo\0 1", 8)))});
  bad.push_back({"bad-escape", V(Rep(base, "Turbo 1", "Turbo \\x 1"))});
  bad.push_back({"lone-surrogate-escape", V(Rep(base, "Turbo 1", "Turbo \\ud800 1"))});
  bad.push_back({"invalid-utf8", V(Rep(base, "Turbo 1", std::string("Tur\xFF 1")))});
  bad.push_back({"overlong-utf8", V(Rep(base, "Turbo 1", std::string("Tur\xC0\x80 1")))});
  bad.push_back({"double-bom", [&] { auto b = mock::Profile(140, "\n", true); b.insert(b.begin(), {0xEF, 0xBB, 0xBF}); return b; }()});
  bad.push_back({"utf16le-bom", {0xFF, 0xFE, '{', 0, '}', 0}});
  bad.push_back({"utf16-no-bom", {'{', 0, '"', 0, 'a', 0, '"', 0, ':', 0, '1', 0, '}', 0}});
  bad.push_back({"empty", {}});
  bad.push_back({"whitespace-only", V("  \n\t ")});
  bad.push_back({"root-array", V("[]")});
  bad.push_back({"root-null", V("null")});
  bad.push_back({"root-string", V("\"x\"")});
  bad.push_back({"oversize", std::vector<uint8_t>(mgp::oem::kMaxJsonBytes + 1, ' ')});
  {
    std::string deep = std::string(100, '[') + std::string(100, ']');
    bad.push_back({"depth-bomb-array", V(deep)});
    std::string nest = Rep(base, "\"Fan\":", "\"Deep\": " + std::string(60, '[') + std::string(60, ']') + ", \"Fan\":");
    bad.push_back({"depth-bomb-in-profile", V(nest)});
  }
  for (const Bad& b : bad) {
    const mgp::oem::ProfileFacts f = mgp::oem::ParseProfile(b.bytes);
    if (f.ok) std::printf("    UNEXPECTEDLY ACCEPTED: %s\n", b.name);
    CHECK(!f.ok);
    CHECK(f.defect != Defect::None && !f.detail.empty());
    std::vector<uint8_t> out;
    std::string err;
    CHECK(!mgp::oem::PatchTarget(b.bytes, 150, out, err) && out.empty() && !err.empty());  // reddedilen dosyadan ASLA bayt üretilmez
  }
  // Hata sınıfları doğru ayrışıyor
  CHECK(mgp::oem::ParseProfile(V(base + "x")).defect == Defect::Syntax);
  CHECK(mgp::oem::ParseProfile(V(Rep(base, "\"PL1\": 55", "\"PL1\": 55, \"ConfigurableTGPTarget\": 150"))).defect == Defect::TargetAmbiguous);
  CHECK(mgp::oem::ParseProfile(V(Rep(base, "\"ConfigurableTGPTarget\": 140", "\"ConfigurableTGPTarget\": \"140\""))).defect == Defect::WrongType);
  CHECK(mgp::oem::ParseProfile(V(Rep(base, "\"ConfigurableTGPTarget\": 140", "\"ConfigurableTGPTarget\": 99"))).defect == Defect::TargetFormat);
  CHECK(mgp::oem::ParseProfile(V("[]")).defect == Defect::RootNotObject);
  CHECK(mgp::oem::ParseProfile({0xFF, 0xFE, '{', 0}).defect == Defect::Envelope);
  CHECK(mgp::oem::ParseProfile(V(Rep(base, "\"GPU\"", "\"GPX\""))).defect == Defect::Missing);
}
TEST(profile_parse_flags_follow_the_powershell_oracle_semantics) {
  using namespace oemtest;
  const std::string base = mock::ProfileText(140, "\n");
  auto F = [&](const std::string& a, const std::string& b) { return mgp::oem::ParseProfile(V(Rep(base, a, b))); };
  auto f = F("\"Activated\": true", "\"Activated\": false");
  CHECK(f.ok && f.activated == 0);
  f = F("\"Activated\": true", "\"Activated\": 0");
  CHECK(f.ok && f.activated == 0);
  f = F("\"ConfigurableTGPSwitch\": 1", "\"ConfigurableTGPSwitch\": 0");
  CHECK(f.ok && f.tgpSwitch == 0);
  f = F("\"DynamicBoostSwitch\": 0", "\"DynamicBoostSwitch\": 1");
  CHECK(f.ok && f.dynBoostSwitch == 1);
  f = F("\"DynamicBoostSwitch\": 0", "\"DynamicBoostSwitch\": false");
  CHECK(f.ok && f.dynBoostSwitch == 0);
}
TEST(main_option_parse_requires_integer_mode_and_index) {
  using namespace oemtest;
  using namespace mgp::oem;
  auto M = [&](const std::string& s) { return ParseMainOption(V(s)); };
  auto g = M(mock::MainOptionText(2, 0));
  CHECK(g.ok && g.operatingMode == 2 && g.turboProfileIndex == 0);
  g = M(mock::MainOptionText(1, 3));
  CHECK(g.ok && g.operatingMode == 1 && g.turboProfileIndex == 3);
  CHECK(!M("{\"OperatingMode\": 2}").ok && !M("{\"TurboProfileIndex\": 0}").ok && !M("{}").ok && !M("[]").ok && !M("").ok);
  CHECK(!M("{\"OperatingMode\": \"2\", \"TurboProfileIndex\": 0}").ok && !M("{\"OperatingMode\": 2.0, \"TurboProfileIndex\": 0}").ok);
  CHECK(!M("{\"OperatingMode\": 2, \"TurboProfileIndex\": 0, \"OperatingMode\": 1}").ok);  // kopya anahtar
  CHECK(!ParseMainOption(std::vector<uint8_t>{0xFF, 0xFE, '{', 0}).ok);
  CHECK(M("\xEF\xBB\xBF{\"OperatingMode\": 2, \"TurboProfileIndex\": 0}").ok);
}

// ------------------------------------------------------------------ yama
TEST(patch_changes_exactly_the_three_digits_for_every_layout_and_every_profile) {
  using namespace oemtest;
  for (const Variant& v : GoodVariants()) {
    for (int w : {125, 130, 135, 140, 145, 150}) {
      std::vector<uint8_t> out;
      std::string err;
      const bool ok = mgp::oem::PatchTarget(v.bytes, w, out, err);
      CHECK(ok && err.empty());
      if (!ok) continue;
      CHECK(out.size() == v.bytes.size());
      int diff = 0;
      size_t first = 0;
      for (size_t k = 0; k < out.size(); ++k)
        if (out[k] != v.bytes[k]) { if (!diff) first = k; ++diff; }
      CHECK(diff <= 3);
      if (w != 140) CHECK(diff >= 1);
      if (w == 140) CHECK(diff == 0 && out == v.bytes);
      const mgp::oem::ProfileFacts f = mgp::oem::ParseProfile(out);
      CHECK(f.ok && f.target == w && f.activated == 1 && f.tgpSwitch == 1 && f.dynBoostSwitch == 0);
      CHECK(mgp::oem::SameModuloTarget(v.bytes, out));
      (void)first;
    }
  }
}
TEST(patch_refuses_unsupported_values_and_never_returns_partial_output) {
  using namespace mgp::oem;
  const auto good = mock::Profile(140);
  for (int w : {0, 1, 99, 100, 120, 124, 126, 129, 141, 149, 151, 160, 999, 1000, -125, -1}) {
    std::vector<uint8_t> out{1, 2, 3};
    std::string err;
    CHECK(!PatchTarget(good, w, out, err) && out.empty() && !err.empty());
  }
}
TEST(same_modulo_target_is_the_only_equivalence_used_for_backups) {
  using namespace mgp::oem;
  const auto a = mock::Profile(140), b = mock::Profile(150), c = mock::Profile(150, "\r\n");
  CHECK(SameModuloTarget(a, b) && SameModuloTarget(a, a) && SameModuloTarget(b, a));
  CHECK(!SameModuloTarget(a, c));                                   // satır sonu farkı
  CHECK(!SameModuloTarget(a, mock::Profile(150, "\n", true)));      // BOM farkı
  auto d = b;
  d[d.size() / 2] ^= 1;
  CHECK(!SameModuloTarget(a, d));                                    // başka bir bayt farklı
  {  // aynı hedef konumu ama farklı uzunluk (sonda fazladan boşluk / eksik kuyruk): aynı SAYILMAZ, taşma/okuma dışı erişim yok
    auto longer = b;
    longer.push_back(' ');
    CHECK(!SameModuloTarget(a, longer) && !SameModuloTarget(longer, a));
    auto shorter = b;
    shorter.pop_back();  // artık geçerli JSON da değil
    CHECK(!SameModuloTarget(a, shorter) && !SameModuloTarget(shorter, a));
  }
  CHECK(!SameModuloTarget(a, {}) && !SameModuloTarget({}, {}));
  CHECK(!SameModuloTarget(a, std::vector<uint8_t>(mock::Bytes("{\"x\":1}"))));
}

// ------------------------------------------------------------------ ad kalıpları
TEST(oem_names_backup_pattern_is_strict_and_roundtrips) {
  using namespace mgp::oem;
  const std::string n = MakeBackupName("20260101-120000", "1a2b3c48");
  CHECK(n == "Mode3_Profile1.json.mgp-original-20260101-120000-1a2b3c48.bak");  // gerçek bir yedek adı biçimiyle aynı biçim
  std::string st, id;
  CHECK(ParseBackupName(n, &st, &id) && st == "20260101-120000" && id == "1a2b3c48");
  CHECK(ParseBackupName(n, nullptr, nullptr));
  const char* bad[] = {
      "", "Mode3_Profile1.json", "Mode3_Profile1.json.bak", "Mode3_Profile1.json.mgp-original-20260101-120000-1a2b3c48",
      "Mode3_Profile1.json.mgp-original-20260101-120000-1A2B3C48.bak", "Mode3_Profile1.json.mgp-original-20260101-120000-1a2b3c4.bak",
      "Mode3_Profile1.json.mgp-original-20260101-120000-1a2b3c489.bak", "Mode3_Profile1.json.mgp-original-2026010-120000-1a2b3c48.bak",
      "Mode3_Profile1.json.mgp-original-20260101_120000-1a2b3c48.bak", "Mode3_Profile1.json.mgp-original-20260101-120000-1a2b3c48.bak.bak",
      "..\\Mode3_Profile1.json.mgp-original-20260101-120000-1a2b3c48.bak", "x\\Mode3_Profile1.json.mgp-original-20260101-120000-1a2b3c48.bak",
      "C:\\Mode3_Profile1.json.mgp-original-20260101-120000-1a2b3c48.bak", "Mode3_Profile1.json.mgp-original-20260101-120000-1a2b3c48.bak ",
      "Mode3_Profile1.json.mgp-original-2026110a-005221-1a2b3c48.bak", "Mode3_Profile2.json.mgp-original-20260101-120000-1a2b3c48.bak",
      "mode3_profile1.json.mgp-original-20260101-120000-1a2b3c48.bak"};
  for (const char* b : bad) CHECK(!ParseBackupName(b, nullptr, nullptr));
  CHECK(IsStamp("20260101-120000") && !IsStamp("20261108005221") && !IsStamp("20261108-00522") && !IsStamp("2026110a-005221"));
  CHECK(IsHex8("1a2b3c48") && IsHex8("00000000") && !IsHex8("1A2B3C48") && !IsHex8("1a2b3c4") && !IsHex8("4cd24cg8"));
}
TEST(oem_names_temp_names_and_known_leaf_allow_list) {
  using namespace mgp::oem;
  CHECK(MakeTempName(kTmpInfix, "1a2b3c48") == "Mode3_Profile1.json.mgp-tmp-1a2b3c48");
  CHECK(IsOwnTempName("Mode3_Profile1.json.mgp-tmp-1a2b3c48") && IsOwnTempName("Mode3_Profile1.json.mgp-swap-1a2b3c48") &&
        IsOwnTempName("Mode3_Profile1.json.mgp-rejected-1a2b3c48"));
  CHECK(!IsOwnTempName("Mode3_Profile1.json") && !IsOwnTempName("MainOption.json") && !IsOwnTempName("Mode3_Profile1.json.mgp-tmp-1a2b3c4") &&
        !IsOwnTempName("Mode3_Profile1.json.mgp-tmp-ZZZZZZZZ") && !IsOwnTempName("Mode3_Profile1.json.mgp-tmp-1a2b3c48.bak") &&
        !IsOwnTempName("Mode3_Profile1.json.mgp-original-20260101-120000-1a2b3c48.bak") && !IsOwnTempName("..\\Mode3_Profile1.json.mgp-tmp-1a2b3c48"));
  // Yedek (.bak) dosyaları ASLA "kendi geçici dosyası" sayılmaz: kod onları silemez.
  CHECK(!IsOwnTempName(MakeBackupName("20260101-120000", "1a2b3c48")));
  CHECK(IsKnownLeaf("MainOption.json") && IsKnownLeaf("Mode3_Profile1.json") && IsKnownLeaf(MakeBackupName("20260101-120000", "1a2b3c48")) &&
        IsKnownLeaf("Mode3_Profile1.json.mgp-swap-00000000"));
  for (const char* b : {"", "..", ".", "..\\MainOption.json", "sub\\MainOption.json", "sub/MainOption.json", "C:MainOption.json", "MainOption.json:stream",
                        "MainOption.json ", "Mode3_Profile2.json", "Fan.json", "MainOption.JSON", "NUL", "CON", "MainOption.json\\..\\x"})
    CHECK(!IsKnownLeaf(b));
}

// ------------------------------------------------------------------ ayarlar (sahiplik kanıtı)
TEST(settings_ownership_roundtrip_and_serialization_shape) {
  Settings s;
  s.restoreOnExit = true;
  s.oem.ownedSha = std::string(64, 'a');
  s.oem.ownedWatts = 150;
  const std::string t = SerializeSettings(s);
  const Settings r = ParseSettings(t);
  CHECK(r.restoreOnExit && r.oem.Present() && r.oem.ownedSha == s.oem.ownedSha && r.oem.ownedWatts == 150);
  // Varsayılan kurulumda ayar dosyası SAHİPLİK satırı içermez.
  const std::string d = SerializeSettings(Settings());
  CHECK(d.find("oem_owned") == std::string::npos);
  // Yol/kullanıcı adı gibi veriler ayar dosyasına yazılmaz.
  CHECK(t.find("C:\\") == std::string::npos && t.find("Users") == std::string::npos && t.find('\\') == std::string::npos);
  // Eski sürümlerin anahtarları (persist_oem, oem_orig_*, last_profile...) bir sonraki kayıtta dosyadan düşer.
  const Settings old = ParseSettings("persist_oem=1\nstartup_apply=1\nlast_profile=150\noem_orig_watts=140\noem_orig_backup=x\n");
  const std::string again = SerializeSettings(old);
  for (const char* k : {"persist_oem", "startup_apply", "last_profile", "oem_orig", "monitor", "auto_reapply", "wake_reapply"})
    CHECK(again.find(k) == std::string::npos);
}
TEST(settings_oem_ownership_is_all_or_nothing_and_rejects_unsafe_values) {
  const std::string okSha(64, 'c');
  auto P = [&](const std::string& body) { return ParseSettings(body); };
  // eksik parça -> sahiplik kanıt SAYILMAZ
  CHECK(!P("oem_owned_sha=" + okSha + "\n").oem.Present());
  CHECK(!P("oem_owned_watts=150\n").oem.Present());
  CHECK(P("oem_owned_sha=" + okSha + "\noem_owned_watts=150\n").oem.Present());
  // güvensiz değerler
  CHECK(!P("oem_owned_sha=" + std::string(63, 'c') + "\noem_owned_watts=150\n").oem.Present());
  CHECK(!P("oem_owned_sha=" + std::string(64, 'C') + "\noem_owned_watts=150\n").oem.Present());
  CHECK(!P("oem_owned_sha=" + okSha + "\noem_owned_watts=151\n").oem.Present());   // bilinmeyen watt
  CHECK(!P("oem_owned_sha=" + okSha + "\noem_owned_watts=0\n").oem.Present());
  CHECK(!P("oem_owned_sha=" + okSha + "\noem_owned_watts=abc\n").oem.Present());
  // kaldırılan eski anahtarlar hiçbir davranışı değiştirmez
  const Settings a = P("persist_oem=1\nstartup_apply=1\nmonitor=1\nauto_reapply=1\nwake_reapply=1\nlast_profile=150\n");
  CHECK(!a.restoreOnExit && !a.consentOverOem && !a.consentUnverified && !a.consentModel && !a.oem.Present());
}
