// Görev Zamanlayıcı erişimi (COM; yedek: %SystemRoot%\System32\schtasks.exe).
//   * TaskKind::Autostart  — "Uygulamayı açılışta başlat": bu uygulamanın tepside başlamasını sağlayan görev. Oluşturulur, sorgulanır, silinir.
//     Oturum açılışı tetikleyicisi, en yüksek ayrıcalık (UAC istemi YOK: EXE requireAdministrator ve görev InteractiveToken+HighestAvailable),
//     komut: "<exe>" /minimized. Bu görev hiçbir GPU profili UYGULAMAZ; arka plan servisi/daemon da DEĞİLDİR (normal bir kullanıcı oturumu uygulaması).
//   * TaskKind::LegacyApply — v1.0-v1.3'ten kalan "oturum açılışında profili uygula ve çık" görevi. v1.4.0'da bu özellik YOKTUR: görev ARTIK
//     OLUŞTURULMAZ; yalnız silinir (kurulum/kaldırıcı ve görevin kendi /apply-startup çağrısı, bkz. startup_cli.h).
// Demo/Wine derlemesinde (-DMGP_DEMO) Görev Zamanlayıcı yerine dosya tabanlı bir sahte depo kullanılır (üretim EXE'sinde yoktur).
#pragma once
#include <string>

#include "../core/task_xml.h"

namespace mgp {
namespace win {

// Otomatik başlatma görevini (yeniden) oluşturur. Üretim derlemesinde EXE Program Files altında değilse reddeder
// (yükseltilmiş bir görev, kullanıcının yazabildiği bir klasördeki programı çalıştırmamalıdır).
bool RegisterAutostartTask(const std::wstring& exePath, std::string& err);
// Görevi siler; görev yoksa da başarılı sayılır. Silindiğinin KESİN doğrulanması için çağıran QueryTask ile TaskRemovalConfirmed kullanır.
bool RemoveTask(TaskKind kind, std::string& err);
TaskObservation QueryTask(TaskKind kind);

}  // namespace win
}  // namespace mgp
