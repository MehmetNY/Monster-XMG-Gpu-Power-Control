// MONSTER GPU POWER - giriş noktası. Kipler:
//   (argümansız)        GUI
//   /minimized          GUI, doğrudan tepside başlar ("Uygulamayı açılışta başlat" görevinin kullandığı kip)
//   /apply-startup      eski (v1.0-v1.2) açılış görevinin çağırdığı kip: HİÇBİR ŞEY uygulamaz, yalnız eski görevi siler
//   /remove-startup     otomatik başlatma görevini ve eski açılış görevini siler (kurulum/kaldırıcı kullanır)
#include "win_util.h"  // windows.h (shellapi.h'dan ÖNCE gelmeli)

#include <shellapi.h>

#include <cwchar>

#include "backends.h"
#include "file_logger.h"
#include "main_window.h"
#include "settings_store.h"
#include "startup_cli.h"
#include "strings_tr.h"

using namespace mgp;
using namespace mgp::win;

namespace {
bool Is(const wchar_t* a, const wchar_t* b) { return _wcsicmp(a, b) == 0; }
}  // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
  bool applyStartup = false, removeStartup = false, minimized = false;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; argv && i < argc; ++i) {
    if (Is(argv[i], L"/apply-startup") || Is(argv[i], L"--apply-startup")) applyStartup = true;
    else if (Is(argv[i], L"/remove-startup") || Is(argv[i], L"--remove-startup")) removeStartup = true;
    else if (Is(argv[i], L"/minimized") || Is(argv[i], L"--minimized")) minimized = true;
  }
  if (argv) LocalFree(argv);

  if (removeStartup) return RunRemoveStartup();
  if (applyStartup) return RunApplyStartup();

  // Tek örnek: ikinci başlatma var olan pencereyi öne getirir. İSTİSNA: "/minimized" (oturum açılışı görevi) çalışan bir örnek varken sessizce
  // çıkar; kullanıcı uygulamayı görev tetiklenmeden önce kendisi açtıysa o pencereyi ya da tepsiye indirdiği pencereyi aniden öne getirmemelidir.
  HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\MonsterGpuPower.SingleInstance");
  if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
    if (!minimized) {
      HWND w = FindWindowW(MainWindow::ClassName(), nullptr);
      if (w) PostMessageW(w, MainWindow::ShowMeMessage(), 0, 0);
    }
    CloseHandle(single);
    return 0;
  }

  int rc = 0;
  {
    FileLogger log;
    log.Write(LogLevel::Info, std::string("start v") + Narrow(tr::kVersion) + (Backends::IsDemoBuild() ? " DEMO(simulated hardware)" : "") +
                                  (IsElevated() ? " elevated" : " NOT elevated"));
    const Settings settings = LoadSettings();
    Backends be(log);         // MainWindow'dan ÖNCE tanımlı: pencere (ve işçi iş parçacığı) önce yok edilir
    MainWindow win(hi, be, log, settings);
    if (!win.Create(minimized)) {
      MessageBoxW(nullptr, L"Pencere oluşturulamadı.", tr::kAppTitle, MB_OK | MB_ICONERROR);
      log.Write(LogLevel::Error, "window creation failed");
      rc = 1;
    } else {
      MSG msg;
      while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (win.PreTranslate(&msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      rc = static_cast<int>(msg.wParam);
    }
  }
  if (single) CloseHandle(single);
  return rc;
}
