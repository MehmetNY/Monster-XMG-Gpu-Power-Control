// Görev Zamanlayıcı ile ilgili ana pencere tarafı: "Uygulamayı açılışta başlat" anahtarı
// (görev: MonsterGpuPower_Autostart; komut: "<exe>" /minimized).
//
//  * Anahtarın durumu ayar dosyasından DEĞİL, GERÇEK görevden türetilir (kayıtlı + etkin + bu kopyanın yolu + /minimized).
//    Böylece kullanıcı görevi Görev Zamanlayıcı'dan silerse / kapatırsa ya da EXE taşınırsa anahtar yanlış "açık" göstermez.
//  * COM çağrıları arayüzü bloklamasın diye Worker iş parçacığında yapılır; sonuç WM_TASKDONE ile döner (yük: unique_ptr / PostMessage başarısızsa silinir).
//  * Bu görev hiçbir GPU profili UYGULAMAZ ve OEM profil dosyasına dokunmaz.
#include <memory>

#include "main_window.h"
#include "main_window_ids.h"
#include "settings_store.h"
#include "startup_task.h"

namespace mgp {
namespace win {

using namespace ids;
using txt::Level;
using txt::Notice;

struct MainWindow::TaskMsg {
  TaskOp op = TaskOp::Probe;
  bool opOk = true;            // Enable/Disable: kayıt / silme çağrısı başarılı mı
  std::string err;
  TaskObservation autostart;   // işlem SONRASI gözlem (Probe'da ilk gözlem)
};

void MainWindow::RequestTaskProbe() { PostTaskOp(TaskOp::Probe); }

void MainWindow::PostTaskOp(TaskOp op) {
  if (!hwnd_ || exiting_ || taskOpInFlight_) return;
  taskOpInFlight_ = true;
  HWND hw = hwnd_;
  const std::wstring self = SelfPath();
  worker_.Post([hw, op, self]() {
    std::unique_ptr<TaskMsg> m(new TaskMsg());
    m->op = op;
    try {
      switch (op) {
        case TaskOp::Probe:
          m->autostart = QueryTask(TaskKind::Autostart);
          break;
        case TaskOp::EnableAutostart:
          m->opOk = RegisterAutostartTask(self, m->err);
          m->autostart = QueryTask(TaskKind::Autostart);  // doğrulama: gerçekten kayıtlı ve bu kopya için etkin mi
          break;
        case TaskOp::DisableAutostart:
          m->opOk = RemoveTask(TaskKind::Autostart, m->err);
          m->autostart = QueryTask(TaskKind::Autostart);  // doğrulama: gerçekten silindi mi
          break;
      }
    } catch (...) {
      m->opOk = false;
      m->err = "internal exception during the task scheduler operation";
      m->autostart = TaskObservation();  // known=false: "bilinmiyor"
    }
    TaskMsg* raw = m.release();
    if (!PostMessageW(hw, WM_TASKDONE, 0, reinterpret_cast<LPARAM>(raw))) delete raw;
  });
  Refresh();
}

void MainWindow::OnTaskDone(TaskMsg* mp) {
  std::unique_ptr<TaskMsg> m(mp);
  taskOpInFlight_ = false;
  autostartPending_ = -1;
  if (exiting_ || !m) return;

  const bool active = AutostartIsActive(m->autostart, SelfPath());
  autostartOn_ = active;  // (görev sorgusu kesin değilse "etkin" sayılmaz: anahtar kapalı görünür; bilinmezlik günlükteki known=0 satırında kalır)
  const char* opName = m->op == TaskOp::Probe ? "probe" : (m->op == TaskOp::EnableAutostart ? "enable" : "disable");
  log_.Write(LogLevel::Info, std::string("tasks ") + opName + ": autostart known=" + (m->autostart.known ? "1" : "0") +
                                 " exists=" + (m->autostart.exists ? "1" : "0") + " enabled=" + (m->autostart.enabled ? "1" : "0") +
                                 " active=" + (active ? "1" : "0") + (m->opOk ? "" : " opError=" + m->err));

  switch (m->op) {
    case TaskOp::Probe:
      break;
    case TaskOp::EnableAutostart:
      if (!m->opOk) {
        {
          const std::wstring e = Widen(m->err);
          SetNotice([e]() { return txt::AutostartEnableFailedNotice(e); });
        }
        log_.Write(LogLevel::Error, "autostart task create failed: " + m->err);
      } else if (!active) {
        SetNotice([]() { return txt::AutostartNotVerifiedNotice(); });
        log_.Write(LogLevel::Warn, "autostart task registered but not verified as active for this exe");
      } else {
        SetNotice([]() { return txt::AutostartOnNotice(); });
        log_.Write(LogLevel::Info, "UI: autostart task created and verified");
      }
      break;
    case TaskOp::DisableAutostart:
      if (TaskRemovalConfirmed(m->opOk, m->autostart.known, m->autostart.exists)) {
        autostartOn_ = false;
        SetNotice([]() { return txt::AutostartOffNotice(); });
        log_.Write(LogLevel::Info, "UI: autostart task removed and verified gone");
      } else {
        const std::string why = !m->err.empty() ? m->err : std::string(m->autostart.known ? "task still registered" : "task scheduler unreachable");
        {
          const std::wstring e = Widen(why);
          SetNotice([e]() { return txt::AutostartDisableFailedNotice(e); });
        }
        log_.Write(LogLevel::Warn, "autostart task removal not confirmed: " + why);
      }
      break;
  }
  Refresh();
}

void MainWindow::OnAutostartToggle(bool want) {
  if (exiting_) return;
  if (taskOpInFlight_) {
    SetNotice([]() { return txt::AutostartBusyNotice(); });
    Refresh();  // anahtar gerçek duruma geri döner
    return;
  }
  if (want == autostartOn_) {
    Refresh();
    return;
  }
#ifndef MGP_DEMO
  if (want && !IsTrustedInstallPath(SelfPath())) {
    // Yükseltilmiş bir oturum-açılışı görevi, kullanıcının yazabildiği klasördeki programı çalıştırmamalı (RegisterAutostartTask da reddeder).
    SetNotice([]() { return txt::AutostartUntrustedPathNotice(); });
    Refresh();
    return;
  }
#endif
  autostartPending_ = want ? 1 : 0;
  log_.Write(LogLevel::Info, std::string("UI: autostart ") + (want ? "ON" : "OFF") + " requested");
  PostTaskOp(want ? TaskOp::EnableAutostart : TaskOp::DisableAutostart);
}

}  // namespace win
}  // namespace mgp
