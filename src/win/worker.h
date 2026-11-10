// Tek arka plan iş parçacığı (Win32 CRITICAL_SECTION/Event; derleyicinin std::thread desteğine bağlı değil).
// Motor (PowerProfileEngine) YALNIZ bu iş parçacığından çağrılır: tüm EC/NVML işlemleri sıraya girer, böylece
// iki yazma asla iç içe geçmez ve arayüz asla bloklanmaz.
#pragma once
#include <deque>
#include <functional>

#include "win_util.h"

namespace mgp {
namespace win {

class Worker {
 public:
  Worker() {
    InitializeCriticalSection(&cs_);
    ev_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    th_ = CreateThread(nullptr, 0, &Worker::Run, this, 0, nullptr);
  }
  ~Worker() {
    Stop();
    if (ev_) CloseHandle(ev_);
    DeleteCriticalSection(&cs_);
  }
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  void Post(std::function<void()> job) {
    EnterCriticalSection(&cs_);
    q_.push_back(std::move(job));
    LeaveCriticalSection(&cs_);
    SetEvent(ev_);
  }

  // Sıradaki ve çalışan işler bitene kadar en çok timeoutMs bekler. Süre dolarsa (ör. yanıt vermeyen bir sürücü
  // çağrısı) false döner ve iş parçacığına DOKUNULMAZ: çağıran süreci sonlandırmaktan sorumludur.
  bool Stop(DWORD timeoutMs = INFINITE) {
    if (!th_) return true;
    EnterCriticalSection(&cs_);
    stop_ = true;
    LeaveCriticalSection(&cs_);
    SetEvent(ev_);
    if (WaitForSingleObject(th_, timeoutMs) != WAIT_OBJECT_0) return false;
    CloseHandle(th_);
    th_ = nullptr;
    return true;
  }

 private:
  static DWORD WINAPI Run(LPVOID p) {
    Worker* self = static_cast<Worker*>(p);
    for (;;) {
      WaitForSingleObject(self->ev_, INFINITE);
      for (;;) {
        std::function<void()> job;
        EnterCriticalSection(&self->cs_);
        if (!self->q_.empty()) {
          job = std::move(self->q_.front());
          self->q_.pop_front();
        }
        const bool stop = self->stop_;
        const bool empty = self->q_.empty() && !job;
        LeaveCriticalSection(&self->cs_);
        if (job) {
          try {
            job();
          } catch (...) {
          }
          continue;
        }
        if (empty) {
          if (stop) return 0;
          break;
        }
      }
    }
  }

  CRITICAL_SECTION cs_;
  HANDLE ev_ = nullptr;
  HANDLE th_ = nullptr;
  std::deque<std::function<void()>> q_;
  bool stop_ = false;
};

}  // namespace win
}  // namespace mgp
