#include "startup_task.h"

#ifndef MGP_DEMO
#include <initguid.h>  // taskschd.h içindeki DEFINE_GUID'ler bu çeviri biriminde tanımlansın
#endif

#include <windows.h>
#ifndef MGP_DEMO
#include <oleauto.h>
#include <taskschd.h>
#endif

#include <cstdio>
#include <cstring>
#include <cwchar>

#include "win_util.h"

namespace mgp {
namespace win {
namespace {

std::wstring CurrentUserId() {
  wchar_t dom[256] = {0}, usr[256] = {0};
  DWORD a = GetEnvironmentVariableW(L"USERDOMAIN", dom, 256);
  DWORD b = GetEnvironmentVariableW(L"USERNAME", usr, 256);
  if (b == 0 || b >= 256) return std::wstring();
  if (a == 0 || a >= 256) return std::wstring(usr);
  return std::wstring(dom) + L"\\" + usr;
}

}  // namespace

#ifdef MGP_DEMO
// =============================================================================================================================
// YALNIZ demo/Wine derlemesi: Wine 9'un Görev Zamanlayıcı emülasyonu kısmidir (tetikleyici/eylem boş saklanır). Görevler <klasör>\<ad>.xml
// dosyaları olarak saklanır; böylece arayüz akışları (açma/kapama, doğrulama, hata yolları) Wine'da uçtan uca denenebilir. ÜRETİM EXE'sinde
// bu blok derlenmez; gerçek COM kodu orada çalışır ve gerçek Windows'ta bu sürümde DENENMEDİ.
//   MGP_DEMO_TASKDIR : depo klasörü (yoksa %LOCALAPPDATA%\MonsterGpuPower\demo_tasks)
//   MGP_DEMO_TASKFAIL: virgülle ayrılmış hata enjeksiyonu: register | remove | nodelete (silme "başarılı" döner ama görev kalır) | query
// =============================================================================================================================
namespace {

std::wstring EnvW(const wchar_t* name) {
  wchar_t b[1024];
  const DWORD n = GetEnvironmentVariableW(name, b, 1024);
  return (n > 0 && n < 1024) ? std::wstring(b, n) : std::wstring();
}

bool FailMode(const wchar_t* what) {
  const std::wstring f = L"," + EnvW(L"MGP_DEMO_TASKFAIL") + L",";
  return f.find(std::wstring(L",") + what + L",") != std::wstring::npos;
}

std::wstring TaskFile(const wchar_t* taskName) {
  std::wstring d = EnvW(L"MGP_DEMO_TASKDIR");
  if (d.empty()) d = DataDir() + L"\\demo_tasks";
  CreateDirectoryW(d.c_str(), nullptr);
  return d + L"\\" + taskName + L".xml";
}

std::wstring XmlUnescape(std::wstring s) {
  const struct { const wchar_t* from; const wchar_t* to; } map[] = {
      {L"&lt;", L"<"}, {L"&gt;", L">"}, {L"&quot;", L"\""}, {L"&apos;", L"'"}, {L"&amp;", L"&"}};  // &amp; en sonda
  for (const auto& m : map) {
    size_t p = 0;
    const std::wstring from = m.from;
    while ((p = s.find(from, p)) != std::wstring::npos) {
      s.replace(p, from.size(), m.to);
      p += std::wcslen(m.to);
    }
  }
  return s;
}

std::wstring TagText(const std::wstring& xml, const wchar_t* tag, bool last) {
  const std::wstring open = std::wstring(L"<") + tag + L">", close = std::wstring(L"</") + tag + L">";
  size_t b = last ? xml.rfind(open) : xml.find(open);
  if (b == std::wstring::npos) return std::wstring();
  b += open.size();
  const size_t e = xml.find(close, b);
  return e == std::wstring::npos ? std::wstring() : XmlUnescape(xml.substr(b, e - b));
}

bool StoreRegister(const wchar_t* taskName, const std::wstring& xml, std::string& err) {
  if (FailMode(L"register")) {
    err = "simulated RegisterTask failure (demo)";
    return false;
  }
  std::string bytes;
  bytes.push_back(static_cast<char>(0xFF));
  bytes.push_back(static_cast<char>(0xFE));  // UTF-16LE BOM (gerçek schtasks yedeğindeki biçimle aynı)
  bytes.append(reinterpret_cast<const char*>(xml.data()), xml.size() * sizeof(wchar_t));
  if (!WriteFileAtomic(TaskFile(taskName), bytes)) {
    err = "demo task store not writable";
    return false;
  }
  return true;
}

bool StoreRemove(const wchar_t* taskName, std::string& err) {
  if (FailMode(L"remove")) {
    err = "simulated DeleteTask failure (demo)";
    return false;
  }
  if (FailMode(L"nodelete")) return true;  // "başarılı" döner ama görev kalır: silme doğrulamasının yakalaması gerekir
  const std::wstring f = TaskFile(taskName);
  if (!DeleteFileW(f.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
    err = "demo task store: delete failed";
    return false;
  }
  return true;
}

TaskObservation StoreQuery(const wchar_t* taskName) {
  TaskObservation st;
  if (FailMode(L"query")) return st;  // known=false: "bilinmiyor"
  std::string bytes;
  const std::wstring f = TaskFile(taskName);
  if (!FileExists(f)) {
    st.known = true;  // kesin: görev yok
    return st;
  }
  if (!ReadFileAll(f, bytes) || bytes.size() < 2) return st;  // okunamadı: bilinmiyor
  const size_t n = (bytes.size() - 2) / sizeof(wchar_t);
  std::wstring xml(n, L'\0');
  if (n) std::memcpy(&xml[0], bytes.data() + 2, n * sizeof(wchar_t));
  st.known = true;
  st.exists = true;
  st.enabled = TagText(xml, L"Enabled", true) == L"true";  // Settings bloğundaki (dosyadaki SON) Enabled
  st.actionPath = TagText(xml, L"Command", false);
  st.actionArgs = TagText(xml, L"Arguments", false);
  return st;
}

}  // namespace

#else  // ------------------------------------------------------------------------------------------------------- üretim: COM

namespace {

std::string HrText(const char* what, HRESULT hr) {
  char b[96];
  std::snprintf(b, sizeof b, "%s failed (HRESULT 0x%08lX)", what, static_cast<unsigned long>(hr));
  return b;
}

// COM yaşam döngüsü
struct ComInit {
  HRESULT hr;
  bool ownsInit;
  ComInit() {
    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ownsInit = SUCCEEDED(hr);                 // S_OK / S_FALSE: eşleşen CoUninitialize gerekir
    if (hr == RPC_E_CHANGED_MODE) hr = S_OK;  // iş parçacığı zaten başka modda; COM kullanılabilir
  }
  ~ComInit() { if (ownsInit) CoUninitialize(); }
  ComInit(const ComInit&) = delete;
  ComInit& operator=(const ComInit&) = delete;
};

template <typename T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

HRESULT ConnectService(ITaskService** out, ITaskFolder** folder) {
  ITaskService* svc = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService,
                                reinterpret_cast<void**>(&svc));
  if (FAILED(hr)) return hr;
  VARIANT e;
  VariantInit(&e);
  hr = svc->Connect(e, e, e, e);
  if (FAILED(hr)) { svc->Release(); return hr; }
  BSTR root = SysAllocString(L"\\");
  hr = svc->GetFolder(root, folder);
  SysFreeString(root);
  if (FAILED(hr)) { svc->Release(); return hr; }
  *out = svc;
  return S_OK;
}

// schtasks.exe yedeği (COM başarısız olursa): UTF-16 XML dosyası + gizli pencere.
// Yürütülebilir dosya HER ZAMAN %SystemRoot%\System32'deki mutlak yoldur (PATH/çalışma dizini araması YOK: yükseltilmiş süreçte
// kullanıcının yazabildiği bir klasördeki sahte schtasks.exe çalıştırılamasın). Yol belirlenemezse yedek yöntem kullanılmaz.
std::wstring SchtasksPath() {
  wchar_t sys[MAX_PATH] = {0};
  const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return std::wstring();
  return std::wstring(sys) + L"\\schtasks.exe";
}

bool RunHidden(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs, DWORD* exitCode) {
  if (exe.empty()) return false;
  STARTUPINFOW si = {};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi = {};
  std::wstring c = L"\"" + exe + L"\" " + args;
  if (!CreateProcessW(exe.c_str(), &c[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
  DWORD w = WaitForSingleObject(pi.hProcess, timeoutMs);
  DWORD code = 1;
  if (w == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
  else TerminateProcess(pi.hProcess, 1);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  if (exitCode) *exitCode = code;
  return w == WAIT_OBJECT_0;
}

bool RegisterViaSchtasks(const wchar_t* taskName, const std::wstring& xml, std::string& err) {
  // XML, kullanıcının yazabildiği %TEMP%'e DEĞİL, yalnız yöneticinin yazabildiği kurulum klasörüne yazılır
  // (aksi hâlde orta bütünlükteki bir süreç dosyayı değiştirip yükseltilmiş görev oluşturtabilirdi).
  const std::wstring tmpFile = ExeDir() + L"\\startup_task.tmp.xml";
  std::string bytes;
  bytes.push_back(static_cast<char>(0xFF));
  bytes.push_back(static_cast<char>(0xFE));  // UTF-16LE BOM
  bytes.append(reinterpret_cast<const char*>(xml.data()), xml.size() * sizeof(wchar_t));
  bool ok = WriteFileAtomic(tmpFile, bytes);
  DWORD code = 1;
  if (ok) {
    const std::wstring args = L"/Create /TN \"" + std::wstring(taskName) + L"\" /XML \"" + tmpFile + L"\" /F";
    ok = RunHidden(SchtasksPath(), args, 20000, &code) && code == 0;
    if (!ok) err = "schtasks /Create failed (exit " + std::to_string(code) + ")";
  } else {
    err = "could not write task XML";
  }
  DeleteFileW(tmpFile.c_str());
  return ok;
}

TaskObservation StoreQuery(const wchar_t* taskName) {
  TaskObservation st;
  ComInit com;
  if (FAILED(com.hr)) return st;
  ITaskService* svc = nullptr;
  ITaskFolder* folder = nullptr;
  if (FAILED(ConnectService(&svc, &folder))) return st;
  BSTR name = SysAllocString(taskName);
  IRegisteredTask* task = nullptr;
  HRESULT hr = folder->GetTask(name, &task);
  SysFreeString(name);
  // Kesin sonuç: görev bulundu ya da "dosya/yol yok" yanıtı alındı. Başka her hata (erişim, RPC ...) "bilinmiyor"dur.
  if (SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) st.known = true;
  if (SUCCEEDED(hr) && task) {
    st.exists = true;
    VARIANT_BOOL en = VARIANT_FALSE;
    if (SUCCEEDED(task->get_Enabled(&en))) st.enabled = (en != VARIANT_FALSE);
    ITaskDefinition* def = nullptr;
    if (SUCCEEDED(task->get_Definition(&def)) && def) {
      IActionCollection* acts = nullptr;
      if (SUCCEEDED(def->get_Actions(&acts)) && acts) {
        IAction* act = nullptr;
        if (SUCCEEDED(acts->get_Item(1, &act)) && act) {
          IExecAction* ex = nullptr;
          if (SUCCEEDED(act->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&ex))) && ex) {
            BSTR p = nullptr, a = nullptr;
            if (SUCCEEDED(ex->get_Path(&p)) && p) { st.actionPath = p; SysFreeString(p); }
            if (SUCCEEDED(ex->get_Arguments(&a)) && a) { st.actionArgs = a; SysFreeString(a); }
            Release(ex);
          }
          Release(act);
        }
        Release(acts);
      }
      Release(def);
    }
  }
  Release(task);
  Release(folder);
  Release(svc);
  return st;
}

bool StoreRegister(const wchar_t* taskName, const std::wstring& xml, std::string& err) {
  const std::wstring user = CurrentUserId();
  ComInit com;
  if (FAILED(com.hr)) {
    err = HrText("CoInitializeEx", com.hr);
    std::string e2;
    if (RegisterViaSchtasks(taskName, xml, e2)) return true;
    err += "; fallback: " + e2;  // COM hatası, yedek yolun hata metniyle ezilmesin
    return false;
  }
  ITaskService* svc = nullptr;
  ITaskFolder* folder = nullptr;
  HRESULT hr = ConnectService(&svc, &folder);
  if (FAILED(hr)) {
    err = HrText("Task Scheduler connect", hr);
    std::string e2;
    if (RegisterViaSchtasks(taskName, xml, e2)) return true;
    err += "; fallback: " + e2;
    return false;
  }
  BSTR name = SysAllocString(taskName);
  BSTR bxml = SysAllocString(xml.c_str());
  VARIANT vUser, vPwd, vSddl;
  VariantInit(&vUser);
  VariantInit(&vPwd);
  VariantInit(&vSddl);
  if (!user.empty()) {
    vUser.vt = VT_BSTR;
    vUser.bstrVal = SysAllocString(user.c_str());
  }
  IRegisteredTask* reg = nullptr;
  hr = folder->RegisterTask(name, bxml, TASK_CREATE_OR_UPDATE, vUser, vPwd, TASK_LOGON_INTERACTIVE_TOKEN, vSddl, &reg);
  VariantClear(&vUser);
  SysFreeString(name);
  SysFreeString(bxml);
  Release(reg);
  Release(folder);
  Release(svc);
  if (FAILED(hr)) {
    err = HrText("RegisterTask", hr);
    std::string e2;
    if (RegisterViaSchtasks(taskName, xml, e2)) return true;
    err += "; fallback: " + e2;
    return false;
  }
  return true;
}

bool StoreRemove(const wchar_t* taskName, std::string& err) {
  {
    ComInit com;
    if (SUCCEEDED(com.hr)) {
      ITaskService* svc = nullptr;
      ITaskFolder* folder = nullptr;
      if (SUCCEEDED(ConnectService(&svc, &folder))) {
        BSTR name = SysAllocString(taskName);
        HRESULT hr = folder->DeleteTask(name, 0);
        SysFreeString(name);
        Release(folder);
        Release(svc);
        if (SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) return true;
        err = HrText("DeleteTask", hr);
      }
    }
  }
  DWORD code = 1;
  const std::wstring args = L"/Delete /TN \"" + std::wstring(taskName) + L"\" /F";
  if (RunHidden(SchtasksPath(), args, 20000, &code) && code == 0) return true;
  // Görev zaten yoksa schtasks hata verir; yokluk KESİN olarak doğrulanabiliyorsa başarılı sayılır. Görev Zamanlayıcı'ya
  // ulaşılamıyorsa (known=false) "görev yok" varsayılmaz: silindiği kanıtlanamadığı için başarısız döner.
  const TaskObservation ts = StoreQuery(taskName);
  if (ts.known && !ts.exists) return true;
  if (err.empty()) err = ts.known ? "schtasks /Delete failed (exit " + std::to_string(code) + ")"
                                  : "the task could not be removed and its absence could not be verified (Task Scheduler unreachable)";
  return false;
}

}  // namespace
#endif  // MGP_DEMO

bool RegisterAutostartTask(const std::wstring& exePath, std::string& err) {
  if (exePath.empty() || !FileExists(exePath)) {
    err = "executable path invalid";
    return false;
  }
#ifndef MGP_DEMO
  if (!IsTrustedInstallPath(exePath)) {
    // Yükseltilmiş bir oturum-açılışı görevi, kullanıcının yazabildiği bir klasördeki EXE'yi çalıştırmamalıdır.
    err = "acilista baslatma yalniz kurulum sihirbaziyla Program Files altina kurulan surumde etkinlestirilebilir";
    return false;
  }
#endif
  return StoreRegister(kAutostartTaskName, BuildAutostartTaskXml(exePath, CurrentUserId()), err);
}

bool RemoveTask(TaskKind kind, std::string& err) { return StoreRemove(TaskName(kind), err); }

TaskObservation QueryTask(TaskKind kind) { return StoreQuery(TaskName(kind)); }

}  // namespace win
}  // namespace mgp
