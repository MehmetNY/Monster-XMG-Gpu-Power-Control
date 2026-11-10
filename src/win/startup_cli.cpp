#include "startup_cli.h"

#include <string>

#include "file_logger.h"
#include "startup_task.h"

namespace mgp {
namespace win {
namespace {

// Görevi siler ve silindiğini SORGUYLA doğrular (TaskRemovalConfirmed: silme çağrısı başarılı + sorgu kesin + görev yok).
bool RemoveVerified(FileLogger& log, TaskKind kind, const char* what) {
  std::string err;
  const bool removed = RemoveTask(kind, err);
  const TaskObservation after = QueryTask(kind);
  const bool ok = TaskRemovalConfirmed(removed, after.known, after.exists);
  log.Write(ok ? LogLevel::Info : LogLevel::Warn,
            ok ? std::string("task cleanup: ") + what + " removed (or absent)"
               : std::string("task cleanup failed for ") + what + ": " + err + " (removed=" + (removed ? "1" : "0") + " known=" +
                     (after.known ? "1" : "0") + " stillExists=" + (after.exists ? "1" : "0") + ")");
  return ok;
}

}  // namespace

int RunApplyStartup() {
  FileLogger log;
  log.Write(LogLevel::Info, "apply-startup: the logon-apply feature was removed in v1.4.0; nothing is applied, the old task is deleted");
  return RemoveVerified(log, TaskKind::LegacyApply, "legacy apply task") ? 0 : 1;
}

int RunRemoveStartup() {
  FileLogger log;
  bool allOk = RemoveVerified(log, TaskKind::Autostart, "autostart task");
  allOk = RemoveVerified(log, TaskKind::LegacyApply, "legacy apply task") && allOk;
  return allOk ? 0 : 1;
}

}  // namespace win
}  // namespace mgp
