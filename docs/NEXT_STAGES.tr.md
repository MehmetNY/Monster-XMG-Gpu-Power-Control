# Sonraki aşamalar (araştırma notları — uygulamada YOK)

[English](NEXT_STAGES.md) · **Türkçe**

> Bu dosya yalnızca **ileride** düşünülebilecek işleri kaydeder. Bunların **hiçbiri** MONSTER GPU POWER'da yoktur, hiçbirine dokunulmamıştır ve vaat edilmemektedir:
> kodda CPU/GPU ortak güç bütçesi, Control Center alternatifi veya Advanced BIOS ile ilgili bir değişiklik yoktur; 230 W adaptör politikası değiştirilmemiştir; 330 W profiline dönülmemiştir.
> Önkoşul: [cihaz kabul testi](DEVICE_ACCEPTANCE_TEST.tr.md)nin kendi cihazınızda geçilmiş olması.

## Aşama 2 — CPU/GPU ortak güç bütçesi

* **Gözlem:** RTX 3070 Laptop GPU 150 W iken i7-11800H'nin yaklaşık **60 W**'a düştüğü; GPU 140 W iken CPU'nun daha yüksek güç aldığı görülüyor. Nedeni bilinmiyor; varsayım yapılmayacak.
* **İncelenecek (yalnız-okuma ile başlanacak):** EC CPU limitleri `0x0730` / `0x0731` ve `0x0783` / `0x0784` / `0x0785`; ortak **AC / PSYS / şarj aleti / VRM** bütçesi; payı hangi bileşenin (EC, BIOS, GCUService, Intel DTT/PSYS) belirlediği.
* **230 W profilinde** gerçek güç paylaşımı kaynağı **kanıtla** araştırılacak; **fiziksel adaptör kapasitesi aşılmayacak**.
* **330 W profiline dönüş yok:** daha önce 330 W profilinde ACPI batarya metriği bozulduğu için (bilinmeyen 330 W sınırlaması) yeniden denenmeyecek. Bu proje ACPI batarya sorunlarını çözdüğünü iddia etmez.
* Kabul ölçütü (öneri): her EC adresi için önce salt-okuma günlüğü + kanıt; yazma yalnız kullanıcı onayı ve geri alma planıyla.

## Aşama 3 — Hafif, Türkçe "Control Center alternatifi"

* Doğrulanmış **tüm** GPU / CPU / fan / EC işlevlerini birleştiren, hafif bir uygulama. **Ayrı bir geliştirme projesi** olurdu; bu uygulamanın yerine geçmez.
* Bu uygulamanın güvenlik ilkeleri taşınırdı: yalnız izin listesi, geri okuma, doğrulama, geri alma, kilitlenme, dürüst durum metinleri.

## Aşama 4 — BIOS `N.1.07Mon08` gizli "Advanced" menü

* AMITSE form filtresi ve IFR koşullarının incelenmesi — **ayrı, yüksek riskli** araştırma.
* **OEM BIOS/EC dosyalarına bu uygulamada müdahale edilmez.** Böyle bir çalışma başlamadan önce yedek ve kurtarma planı (programlayıcı, kurtarma yöntemi) hazır olmalıdır.
