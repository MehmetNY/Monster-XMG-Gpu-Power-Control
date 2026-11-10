#include "task_xml.h"

namespace mgp {
namespace {

std::wstring XmlEscape(const std::wstring& s) {
  std::wstring r;
  r.reserve(s.size() + 8);
  for (wchar_t c : s) {
    switch (c) {
      case L'&': r += L"&amp;"; break;
      case L'<': r += L"&lt;"; break;
      case L'>': r += L"&gt;"; break;
      case L'"': r += L"&quot;"; break;
      case L'\'': r += L"&apos;"; break;
      default: r += c;
    }
  }
  return r;
}

wchar_t AsciiLower(wchar_t c) { return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c; }

bool EqualsNoCase(const std::wstring& a, const std::wstring& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (AsciiLower(a[i]) != AsciiLower(b[i])) return false;
  return true;
}

// Başındaki/sonundaki boşluk ve çift tırnakları atar (Görev Zamanlayıcı yolu tırnaklı döndürebilir).
std::wstring StripQuotesAndSpace(const std::wstring& s) {
  size_t b = 0, e = s.size();
  auto junk = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'"'; };
  while (b < e && junk(s[b])) ++b;
  while (e > b && junk(s[e - 1])) --e;
  return s.substr(b, e - b);
}

}  // namespace

const wchar_t* TaskName(TaskKind k) { return k == TaskKind::Autostart ? kAutostartTaskName : kLegacyTaskName; }

std::wstring BuildAutostartTaskXml(const std::wstring& exePath, const std::wstring& userId) {
  const std::wstring u = XmlEscape(userId);
  std::wstring x;
  x += L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n";
  x += L"<Task version=\"1.4\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n";
  x += L"  <RegistrationInfo><Description>MONSTER GPU POWER: oturum acilisinda uygulamayi tepside (kucultulmus) baslatir. "
       L"GPU profili uygulamaz.</Description></RegistrationInfo>\n";
  x += L"  <Triggers><LogonTrigger><Enabled>true</Enabled>";
  if (!u.empty()) x += L"<UserId>" + u + L"</UserId>";
  x += L"<Delay>PT10S</Delay></LogonTrigger></Triggers>\n";
  x += L"  <Principals><Principal id=\"Author\">";
  if (!u.empty()) x += L"<UserId>" + u + L"</UserId>";
  x += L"<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>\n";
  x += L"  <Settings>\n";
  x += L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n";
  x += L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n";
  x += L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n";
  x += L"    <AllowHardTerminate>true</AllowHardTerminate>\n";
  x += L"    <StartWhenAvailable>false</StartWhenAvailable>\n";
  x += L"    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\n";
  x += L"    <AllowStartOnDemand>true</AllowStartOnDemand>\n";
  x += L"    <Enabled>true</Enabled>\n";
  x += L"    <Hidden>false</Hidden>\n";
  x += L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\n";
  x += L"    <Priority>5</Priority>\n";
  x += L"  </Settings>\n";
  x += L"  <Actions Context=\"Author\"><Exec><Command>\"" + XmlEscape(exePath) + L"\"</Command><Arguments>";
  x += kAutostartArgs;
  x += L"</Arguments></Exec></Actions>\n";
  x += L"</Task>\n";
  return x;
}

bool AutostartIsActive(const TaskObservation& o, const std::wstring& selfPath) {
  if (!o.known || !o.exists || !o.enabled) return false;
  const std::wstring self = StripQuotesAndSpace(selfPath);
  if (self.empty()) return false;
  if (!EqualsNoCase(StripQuotesAndSpace(o.actionPath), self)) return false;
  return EqualsNoCase(StripQuotesAndSpace(o.actionArgs), kAutostartArgs);
}

}  // namespace mgp
