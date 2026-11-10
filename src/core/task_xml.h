// Görev Zamanlayıcı görevleri için SAF mantık (OS çağrısı yok; ana makinede birim testlenir):
//   * görev adları ve bu uygulamanın kendini oturum açılışında başlatan görevinin XML'i,
//   * gözlenen görevin "bu kopya için gerçekten etkin mi" kararı,
//   * silme sonrası doğrulama kuralı.
// COM / schtasks / demo görev deposu src/win/startup_task.cpp içindedir.
#pragma once
#include <string>

namespace mgp {

enum class TaskKind {
  // v1.0-v1.3 "Windows açılışında uygula": oturum açılışında son profili EC'ye yazıp çıkan görev. v1.4.0'da ARTIK YOKTUR
  // (yerini OEM profil dosyası kalıcılığı aldı); yalnız eski kurulumlardan kalmış olanı silinir (kurulum/kaldırıcı).
  LegacyApply,
  // "Uygulamayı açılışta başlat": uygulamanın kendisini oturum açılışında tepside başlatan görev. Profil UYGULAMAZ.
  Autostart
};

inline constexpr wchar_t kLegacyTaskName[] = L"MonsterGpuPower_ApplyAtLogon";
inline constexpr wchar_t kAutostartTaskName[] = L"MonsterGpuPower_Autostart";
inline constexpr wchar_t kAutostartArgs[] = L"/minimized";

const wchar_t* TaskName(TaskKind k);

// Görev Zamanlayıcı'dan okunan görevin gözlemi.
struct TaskObservation {
  bool exists = false;
  // Sorgu KESİN bir sonuca ulaştı mı? (görev bulundu ya da "görev yok" yanıtı alındı). false = Görev Zamanlayıcı'ya ulaşılamadı /
  // beklenmeyen hata: bu durumda exists=false "görev yok" anlamına GELMEZ.
  bool known = false;
  bool enabled = false;      // görev etkin mi (Görev Zamanlayıcı'dan kapatılmış olabilir)
  std::wstring actionPath;   // görevin çalıştıracağı EXE yolu
  std::wstring actionArgs;   // komut satırı bağımsız değişkenleri
};

// Otomatik başlatma görevinin XML'i (UTF-16 başlıklı; RegisterTask'a ya da schtasks /XML'e verilir).
//   * oturum açılışı tetikleyicisi (yalnız bu kullanıcı, 10 sn gecikme: görev çubuğu hazır olsun),
//   * InteractiveToken + HighestAvailable: EXE requireAdministrator olduğundan oturum açılışında UAC istemi çıkmaz,
//   * pil kısıtı yok, süre sınırı yok (uzun yaşayan tepsi uygulaması), çoklu örnek yok, GÖRÜNÜR (Hidden=false),
//   * komut: "<exe>" /minimized  (tepside başlar; hiçbir profil uygulamaz).
std::wstring BuildAutostartTaskXml(const std::wstring& exePath, const std::wstring& userId);

// Gözlenen görev, BU kopya için "açılışta başlat" olarak GERÇEKTEN etkin mi? Evet demek için: sorgu kesin, görev var, etkin, eylem yolu
// bu çalışan EXE ile (büyük/küçük harf duyarsız, tırnaksız) aynı ve bağımsız değişken tam olarak /minimized.
bool AutostartIsActive(const TaskObservation& o, const std::wstring& selfPath);

// Bir görevin silindiği ancak şu üç koşul birlikte sağlanırsa KESİN sayılır: silme çağrısı başarılı, sorgu kesin sonuç verdi, görev artık yok.
// Görev Zamanlayıcı'ya ulaşılamıyorsa (queryKnown=false) "görev yok" varsayılmaz.
inline bool TaskRemovalConfirmed(bool removeApiOk, bool queryKnown, bool stillExists) { return removeApiOk && queryKnown && !stillExists; }

}  // namespace mgp
