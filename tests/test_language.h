// Arayüz dili: ayrıştırma, çözümleme sırası ve ayar dosyası gidiş-dönüşü (saf mantık; Windows gerekmez).
#pragma once
#include "../src/core/language.h"

TEST(language_codes_parse_strictly) {
  Lang l = Lang::Tr;
  CHECK(ParseLangCode("en", &l) && l == Lang::En);
  CHECK(ParseLangCode(" TR ", &l) && l == Lang::Tr);
  CHECK(!ParseLangCode("", &l) && !ParseLangCode("de", &l) && !ParseLangCode("english", &l) && !ParseLangCode("1033", &l));
  CHECK(l == Lang::Tr);  // başarısız ayrıştırma çıktıyı değiştirmez
  CHECK(ParseInstallerLanguage("1055", &l) && l == Lang::Tr);
  CHECK(ParseInstallerLanguage("1033", &l) && l == Lang::En);
  CHECK(ParseInstallerLanguage("tr", &l) && l == Lang::Tr);
  CHECK(!ParseInstallerLanguage("", &l) && !ParseInstallerLanguage("1031", &l) && !ParseInstallerLanguage("0", &l));
  CHECK(std::string(LangCode(Lang::Tr)) == "tr" && std::string(LangCode(Lang::En)) == "en");
}

TEST(language_defaults_to_english_unless_windows_ui_is_turkish) {
  LangInputs in;
  CHECK(ResolveLanguage(in) == Lang::En);  // ana dil İngilizce
  in.systemIsTurkish = true;
  CHECK(ResolveLanguage(in) == Lang::Tr);
}

TEST(language_installer_choice_beats_system_language) {
  LangInputs in;
  in.systemIsTurkish = true;
  in.installerLang = "en";
  CHECK(ResolveLanguage(in) == Lang::En);
  in.systemIsTurkish = false;
  in.installerLang = "tr";
  CHECK(ResolveLanguage(in) == Lang::Tr);
}

TEST(language_user_toggle_beats_installer_while_installer_choice_is_unchanged) {
  LangInputs in;
  in.installerLang = "tr";
  in.settingsLang = "en";
  in.settingsInstaller = "tr";  // kullanıcı, kurulum dili Türkçe iken İngilizceyi seçti
  CHECK(ResolveLanguage(in) == Lang::En);
  in.installerLang = "en";  // yeniden kurulumda İngilizce seçildi: yeni kurulum seçimi kazanır
  in.settingsLang = "tr";
  in.settingsInstaller = "en";
  CHECK(ResolveLanguage(in) == Lang::Tr);  // (kurulum dili değişmedi: kullanıcı seçimi sürer)
  in.installerLang = "tr";                 // kurulum dili sonradan değişti
  CHECK(ResolveLanguage(in) == Lang::Tr);  // yeni kurulum dili Türkçe
  in.settingsLang = "en";
  in.settingsInstaller = "en";             // kullanıcı EN seçmişti (kurulum dili o zaman EN idi); şimdi kurulum TR
  CHECK(ResolveLanguage(in) == Lang::Tr);  // yeniden kurulumun seçimi kullanıcı seçimini geçer
}

TEST(language_superseded_user_choice_is_detected_only_when_the_installer_language_differs_or_appeared) {
  LangInputs in;
  CHECK(!UserChoiceSuperseded(in));                 // seçim yok
  in.installerLang = "tr";
  CHECK(!UserChoiceSuperseded(in));                 // seçim yok (yalnız kurulum dili)
  in.settingsLang = "en";
  in.settingsInstaller = "tr";
  CHECK(!UserChoiceSuperseded(in));                 // kurulum dili seçimden bu yana değişmedi
  in.installerLang = "en";
  CHECK(UserChoiceSuperseded(in));                  // yeniden kurulumda başka dil seçildi
  in.settingsInstaller = "";
  in.installerLang = "tr";
  CHECK(UserChoiceSuperseded(in));                  // taşınabilir kopyada seçilmişti, sonra kurulum yapıldı
  in.installerLang = "";
  in.settingsInstaller = "tr";
  CHECK(!UserChoiceSuperseded(in));                 // kurulum dili okunamıyor: kullanıcı seçimine dokunulmaz
  in.installerLang = "garbage";
  CHECK(!UserChoiceSuperseded(in));
  // Diriltme yok: eski seçim silindikten sonra aynı kurulum diline dönülürse kurulumun seçimi geçerlidir
  LangInputs after;
  after.installerLang = "tr";
  after.systemIsTurkish = false;
  CHECK(ResolveLanguage(after) == Lang::Tr);
}

TEST(language_portable_user_choice_sticks_without_any_installer_value) {
  LangInputs in;  // kurulumsuz (standalone) kopya: kayıt defteri değeri yok
  in.settingsLang = "tr";
  CHECK(ResolveLanguage(in) == Lang::Tr);
  in.settingsLang = "en";
  in.systemIsTurkish = true;
  CHECK(ResolveLanguage(in) == Lang::En);  // seçim sistem dilini geçer
  // kullanıcı seçmişken kurulum dili sonradan ortaya çıktıysa (taşınabilir kopya, sonra kurulum): kurulum seçimi kazanır
  in.installerLang = "tr";
  CHECK(ResolveLanguage(in) == Lang::Tr);
}

TEST(language_garbage_inputs_fall_back_safely) {
  LangInputs in;
  in.settingsLang = "xx";
  in.installerLang = "zz";
  CHECK(ResolveLanguage(in) == Lang::En);
  in.systemIsTurkish = true;
  CHECK(ResolveLanguage(in) == Lang::Tr);
}

TEST(language_setting_roundtrips_and_is_written_only_when_chosen) {
  CHECK(SerializeSettings(Settings()).find("language") == std::string::npos);
  Settings s;
  s.language = "en";
  s.languageInstaller = "tr";
  const std::string out = SerializeSettings(s);
  CHECK(out.find("language=en") != std::string::npos && out.find("language_installer=tr") != std::string::npos);
  const Settings t = ParseSettings(out);
  CHECK(t.language == "en" && t.languageInstaller == "tr");
  const Settings u = ParseSettings("language=de\nlanguage_installer=1055\n");  // tanınmayan değerler yok sayılır
  CHECK(u.language.empty() && u.languageInstaller.empty());
  CHECK(ParseSettings("language=TR\n").language == "tr");
}
