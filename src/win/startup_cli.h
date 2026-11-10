// Komut satırı kipleri: /apply-startup ve /remove-startup. Bunlar arka plan hizmeti DEĞİLDİR: işlem biter bitmez süreç kapanır.
//   /apply-startup  v1.0-v1.2'den kalan ESKİ "Windows açılışında uygula" görevinin çağırdığı kip. v1.4.0'da bu özellik yoktur: kip HİÇBİR ŞEY
//                   uygulamaz, yalnız eski görevi siler (profiller artık OEM profil dosyasında kalıcıdır).
//   /remove-startup kurulum/kaldırıcının kullandığı kip: otomatik başlatma görevini ve eski açılış görevini siler.
#pragma once

namespace mgp {
namespace win {

int RunApplyStartup();   // eski görevi siler; çıkış kodu 0: görev silindi (veya yoktu), 1: silindiği doğrulanamadı
int RunRemoveStartup();  // 0: iki görev de silindi (veya yoktu)

}  // namespace win
}  // namespace mgp
