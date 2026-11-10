// Görev Zamanlayıcı saf mantığı testleri (src/core/task_xml.*): otomatik başlatma görevinin XML'i, "bu kopya için etkin mi" kararı,
// silme doğrulaması ve ayar dosyası uyumluluğu (eski startup_apply anahtarı; açılışta başlat için ayar anahtarı YOK).
// (test_main.cpp içinden, test_persist_view.h'tan SONRA dahil edilir.)
#pragma once
#include "../src/core/task_xml.h"

namespace taskxmltest {

using namespace mgp;

inline bool Has(const std::wstring& x, const wchar_t* needle) { return x.find(needle) != std::wstring::npos; }

// Asgari XML iyi-biçimlilik denetimi: etiketler doğru iç içe/kapalı, & yalnız bilinen varlıklarla, başlık geçerli.
inline bool XmlWellFormed(const std::wstring& x, std::string* why) {
  std::vector<std::wstring> stack;
  size_t i = 0;
  if (x.compare(0, 5, L"<?xml") == 0) {
    const size_t e = x.find(L"?>");
    if (e == std::wstring::npos) { *why = "unterminated xml declaration"; return false; }
    i = e + 2;
  }
  bool sawRoot = false;
  while (i < x.size()) {
    const wchar_t c = x[i];
    if (c == L'<') {
      const size_t e = x.find(L'>', i);
      if (e == std::wstring::npos) { *why = "unterminated tag"; return false; }
      std::wstring tag = x.substr(i + 1, e - i - 1);
      if (tag.empty()) { *why = "empty tag"; return false; }
      if (tag[0] == L'/') {
        const std::wstring name = tag.substr(1);
        if (stack.empty() || stack.back() != name) { *why = "mismatched closing tag"; return false; }
        stack.pop_back();
      } else {
        const bool selfClose = tag.back() == L'/';
        size_t n = 0;
        while (n < tag.size() && tag[n] != L' ' && tag[n] != L'/') ++n;
        if (stack.empty() && sawRoot) { *why = "second root element"; return false; }
        sawRoot = true;
        if (!selfClose) stack.push_back(tag.substr(0, n));
      }
      i = e + 1;
    } else if (c == L'&') {
      const size_t e = x.find(L';', i);
      if (e == std::wstring::npos) { *why = "bare ampersand"; return false; }
      const std::wstring ent = x.substr(i + 1, e - i - 1);
      if (ent != L"amp" && ent != L"lt" && ent != L"gt" && ent != L"quot" && ent != L"apos") { *why = "unknown entity"; return false; }
      i = e + 1;
    } else if (c == L'>') {
      *why = "stray >";
      return false;
    } else {
      ++i;
    }
  }
  if (!stack.empty()) { *why = "unclosed element"; return false; }
  return sawRoot;
}

inline TaskObservation GoodObs(const std::wstring& exe) {
  TaskObservation o;
  o.known = o.exists = o.enabled = true;
  o.actionPath = exe;
  o.actionArgs = L"/minimized";
  return o;
}

}  // namespace taskxmltest

TEST(task_names_are_exact_and_distinct) {
  using namespace taskxmltest;
  CHECK(std::wstring(TaskName(TaskKind::Autostart)) == L"MonsterGpuPower_Autostart");
  CHECK(std::wstring(TaskName(TaskKind::LegacyApply)) == L"MonsterGpuPower_ApplyAtLogon");
  CHECK(std::wstring(TaskName(TaskKind::Autostart)) != std::wstring(TaskName(TaskKind::LegacyApply)));
  CHECK(std::wstring(kAutostartArgs) == L"/minimized");
}

TEST(autostart_task_xml_is_well_formed_and_launches_minimized_without_applying_a_profile) {
  using namespace taskxmltest;
  const std::wstring exe = L"C:\\Program Files\\MONSTER GPU POWER\\MonsterGpuPower.exe";
  const std::wstring x = BuildAutostartTaskXml(exe, L"DESKTOP-ABC\\mehmet");
  std::string why;
  CHECK(XmlWellFormed(x, &why));
  // eylem: tırnaklı yol + /minimized; ESKİ görevin /apply-startup kipi ASLA yok (bu görev profil uygulamaz)
  CHECK(Has(x, L"<Command>\"C:\\Program Files\\MONSTER GPU POWER\\MonsterGpuPower.exe\"</Command><Arguments>/minimized</Arguments>"));
  CHECK(!Has(x, L"apply-startup"));
  // oturum açılışı tetikleyicisi: yalnız bu kullanıcı, 10 sn gecikme
  CHECK(Has(x, L"<LogonTrigger><Enabled>true</Enabled><UserId>DESKTOP-ABC\\mehmet</UserId><Delay>PT10S</Delay></LogonTrigger>"));
  // UAC istemi olmadan yükseltilmiş çalışma: InteractiveToken + HighestAvailable
  CHECK(Has(x, L"<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel>"));
  // uzun yaşayan tepsi uygulaması: süre sınırı yok, pilde de çalışır/durmaz, çoklu örnek yok, görünür
  CHECK(Has(x, L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>"));
  CHECK(Has(x, L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"));
  CHECK(Has(x, L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"));
  CHECK(Has(x, L"<MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"));
  CHECK(Has(x, L"<Hidden>false</Hidden>"));
  CHECK(Has(x, L"<Enabled>true</Enabled>"));
  CHECK(!Has(x, L"PT3M"));  // eski görevin 3 dakikalık sınırı bu uzun yaşayan görevde olmamalı
  CHECK(x.rfind(L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>", 0) == 0);
}

TEST(autostart_task_xml_escapes_special_characters_in_path_and_user_and_handles_unknown_user) {
  using namespace taskxmltest;
  const std::wstring weird = L"C:\\Program Files\\A&B <x> 'q'\\MonsterGpuPower.exe";
  const std::wstring x = BuildAutostartTaskXml(weird, L"AD\\o'brien&co");
  std::string why;
  CHECK(XmlWellFormed(x, &why));
  CHECK(Has(x, L"A&amp;B &lt;x&gt; &apos;q&apos;"));
  CHECK(Has(x, L"<UserId>AD\\o&apos;brien&amp;co</UserId>"));
  // Türkçe harfli kullanıcı/yol da bozulmadan geçer
  const std::wstring tr = L"C:\\Program Files\\\u0130\u015E\\MonsterGpuPower.exe";
  const std::wstring y = BuildAutostartTaskXml(tr, L"\u0130\u015E\\\u00C7elik");
  CHECK(XmlWellFormed(y, &why));
  CHECK(Has(y, L"\u0130\u015E\\\u00C7elik") && Has(y, tr.c_str()));
  // kullanıcı bilinmiyorsa UserId öğeleri hiç yazılmaz (geçerli XML kalır)
  const std::wstring z = BuildAutostartTaskXml(L"C:\\Program Files\\X\\MonsterGpuPower.exe", L"");
  CHECK(XmlWellFormed(z, &why));
  CHECK(!Has(z, L"<UserId>"));
}

TEST(autostart_is_active_only_for_an_enabled_task_that_launches_this_exe_minimized) {
  using namespace taskxmltest;
  const std::wstring self = L"C:\\Program Files\\MONSTER GPU POWER\\MonsterGpuPower.exe";
  CHECK(AutostartIsActive(GoodObs(self), self));
  // sorgu kesin değil / görev yok / kapalı: asla etkin değil
  TaskObservation o = GoodObs(self); o.known = false;
  CHECK(!AutostartIsActive(o, self));
  o = GoodObs(self); o.exists = false;
  CHECK(!AutostartIsActive(o, self));
  o = GoodObs(self); o.enabled = false;  // kullanıcı Görev Zamanlayıcı'dan kapatmış
  CHECK(!AutostartIsActive(o, self));
  // başka bir kopyanın yolu (EXE taşınmış / başka kurulum): bu kopya için etkin DEĞİL
  o = GoodObs(L"D:\\Portable\\MonsterGpuPower.exe");
  CHECK(!AutostartIsActive(o, self));
  o = GoodObs(self + L"x");
  CHECK(!AutostartIsActive(o, self));
  // yol büyük/küçük harf duyarsız ve tırnak/boşluk toleranslı (Görev Zamanlayıcı yolu tırnaklı döndürebilir)
  CHECK(AutostartIsActive(GoodObs(L"c:\\program files\\monster gpu power\\monstergpupower.exe"), self));
  CHECK(AutostartIsActive(GoodObs(L"\"" + self + L"\""), self));
  CHECK(AutostartIsActive(GoodObs(L"  " + self + L" "), self));
  // bağımsız değişken tam olarak /minimized olmalı: eski görevin /apply-startup'ı ya da fazladan bağımsız değişken etkin SAYILMAZ
  for (const wchar_t* a : {L"", L"/apply-startup", L"/minimized /x", L"minimized", L"/minimised"}) {
    o = GoodObs(self); o.actionArgs = a;
    CHECK(!AutostartIsActive(o, self));
  }
  o = GoodObs(self); o.actionArgs = L" /MINIMIZED ";
  CHECK(AutostartIsActive(o, self));
  // çalışan EXE yolu bilinmiyorsa hiçbir görev "bu kopyaya ait" sayılamaz
  CHECK(!AutostartIsActive(GoodObs(L""), L""));
  CHECK(!AutostartIsActive(GoodObs(self), L""));
}

TEST(task_removal_is_confirmed_only_when_the_call_succeeded_the_query_was_definite_and_the_task_is_gone) {
  CHECK(TaskRemovalConfirmed(true, true, false));
  CHECK(!TaskRemovalConfirmed(true, true, true));
  CHECK(!TaskRemovalConfirmed(true, false, false));  // Görev Zamanlayıcı'ya ulaşılamadı: "yok" varsayılmaz
  CHECK(!TaskRemovalConfirmed(true, false, true));
  CHECK(!TaskRemovalConfirmed(false, true, false));
  CHECK(!TaskRemovalConfirmed(false, false, false));
  CHECK(!TaskRemovalConfirmed(false, true, true));
  CHECK(!TaskRemovalConfirmed(false, false, true));
}

TEST(settings_file_has_no_key_for_autostart_it_is_derived_from_the_real_task) {
  const std::string out = SerializeSettings(Settings());
  // "Uygulamayı açılışta başlat" durumu GERÇEK görevden türetilir: ayar dosyasında ona ait hiçbir anahtar yoktur
  for (const char* k : {"autostart", "auto_start", "run_at_logon", "start_with_windows", "startup_minimized", "startup_apply"}) CHECK(out.find(k) == std::string::npos);
  // dosyadaki böyle bir anahtar yok sayılır (etkisiz): kullanıcı dosyaya yazarak "açılışta başlat"ı AÇAMAZ
  const Settings t = ParseSettings("autostart=1\nstart_with_windows=1\nstartup_apply=1\n");
  CHECK(!t.restoreOnExit && !t.consentOverOem && !t.consentUnverified && !t.consentModel && !t.oem.Present());
}
