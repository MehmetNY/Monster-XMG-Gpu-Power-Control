# Test ve doğrulama durumu (v1.4.0)

> **Özet: uygulama gerçek donanımda henüz çalıştırılmadı.** Aşağıdaki "geçti" sonuçlarının hepsi ya saf mantık birim testleridir ya da **simülasyondur**
> (Wine altında, sahte EC / NVIDIA / SMBIOS / OEM klasörü ile). Simülasyon, gerçek Windows'un, gerçek NTFS/ACL/`ReplaceFileW` davranışının,
> gerçek Görev Zamanlayıcı'nın, gerçek `GCUService`'in ve gerçek donanımın yerini tutmaz.

## 1. Çalıştırılan testler

| Katman | Sonuç | Ne kanıtlar | Ne kanıtlamaz |
|---|---|---|---|
| Birim testleri (`make test`, g++ + ASan + UBSan, ana makinede) | **101 test / 1818 denetim geçti** | JSON ayrıştırma/yama (bozuk ve saldırgan girdiler dahil), güvenli yazma algoritması ve TOCTOU yeniden denetimi, işlem sınıflandırması (`BothOk`, `OemFailedLiveRestored` …), sahiplik/yabancı değer kuralları, kimlik denetimi, görev XML'i, arayüz metinlerinin "EC firmware/BIOS kalıcı yazıldı" demediği | Gerçek Windows/donanım |
| Wine + sahte donanım, uçtan uca GUI (`MonsterGpuPower_DEMO.exe`) | **158 denetim geçti, 0 başarısız** | Gerçek Win32 pencere/mesaj/dosya çağrılarıyla: ilk açılışta sıfır yazma; 7 profil değişikliğinin her biri anında kalıcı (EC + NVML + OEM dosyası + yedekler); aynı profile tekrar tıklama; yabancı değer onayı (Hayır ve SHA'ya sabit Evet); kilitli OEM durumlarında hiçbir şey uygulanmaması; OEM yazımı başarısızken canlı katmanın geri alınması; canlı katman başarısızken OEM'e dokunulmaması; onay kutuları; *açılışta başlat*; *çıkışta OEM 140 W'a dön*; *Hakkında*; komut satırı kipleri; eski ayar anahtarlarının etkisizliği; bozuk OEM dosyaları; 96–192 DPI düzeni | Gerçek EC/NVIDIA/OEM dosya sistemi; gerçek Görev Zamanlayıcı (dosya tabanlı sahte depo kullanıldı) |
| Kurulum paketi, Wine (`sessiz kurulum + v1.3.0'dan yükseltme`) | **sessiz kurulum 22/22, v1.3.0'dan yükseltme 14/14 geçti** | Dosyalar, kısayollar, kayıt anahtarı, eski sürümün görevinin silinmesi, OEM klasörünün bayt bayt değişmemesi, kaldırma | Gerçek Windows'ta kurulum/SmartScreen davranışı |
| PE doğrulaması (`tools/verify_pe.py`) | geçti | x64, GUI alt sistemi, ASLR/DEP/yüksek entropili VA, yönetici manifesti, PerMonitorV2, sürüm 1.4.0.0, ağ DLL'si içe aktarımı yok, üretim EXE'sinde demo kodu/dizgesi yok | — |

Wine testlerinde bölüm başına sayılar: A 18 · B 39 · C 4 · D 9 · D2 3 · E 25 · F 5 · F2 8 · F3 2 · G 6 · H 6 · H2 1 · H2b 2 · H3 2 · I 4 · I2 1 · J 4 · K 6 · K2 1 · L 4 · M 2 · N 6 (toplam 158).
Test düzenekleri bu depoda **yoktur** (derleyici, Wine, Xvfb ve sahte klasör düzeneği geliştirme ortamına bağlıdır); birim testleri `tests/` altındadır.

## 2. DENENMEDİ (gerçek donanım ve gerçek Windows)

* Gerçek Windows 10/11 üzerinde ve gerçek Monster Tulpar T5 V21.7 cihazında **hiçbir deneme yapılmadı**: bu uygulamayla hiçbir gerçek EC yazması ve hiçbir gerçek OEM JSON yazması yapılmadı.
* Gerçek Windows Görev Zamanlayıcı davranışı (kayıt, 10 sn gecikmeli oturum açılışı tetikleyicisi, silme). Wine'ın kısmi `taskschd` emülasyonu tetikleyici/eylem alanlarını saklamaz; testler dosya tabanlı sahte depo kullandı.
* Oturum açılışında tepsi simgesinin görünmesi (`TaskbarCreated`, UIPI), `/minimized` kipinin gerçek Windows'ta davranışı.
* Gerçek NTFS/ACL/`ReplaceFileW` davranışı; `GCUService`'in profil dosyasını **ne zaman** okuduğu (yalnız açılışta mı, canlı mı).
* **125, 130, 135 ve 145 W için yeniden başlatma kalıcılığı.** Cihaz sahibi, aynı mekanizmayı (JSON'da 140 → 150 W) kendi PowerShell betiğiyle denemiş ve 150 W'ın ekran döngülerinde ve yeniden başlatmalarda korunduğunu gözlemlemiştir; bu uygulamanın C++ kodu o yöntemin karşılığıdır ve gerçek cihazda çalıştırılmamıştır.
* 130 W ve 135 W EC eşlemesi (`0x05` / 5 ve 10) **beklenen** değerdir; canlı denenmemiştir.
* **Hakkında** düğmesinin yükseltilmiş (yönetici) süreçten varsayılan tarayıcıyı açması (Wine'da çağrının yapıldığı günlükten doğrulandı).
* `build.bat` (Windows'ta derleme) çalıştırılmadı; Linux çapraz derleme ve `make test` çalıştırıldı.
* README'deki salt-okunur PowerShell komutları gerçek bir Windows/PowerShell'de çalıştırılmadı; yalnızca söz dizimi ve alan adları kodla karşılaştırıldı.

## 3. Bilerek yapılmayanlar

230 W / 330 W adaptör profilleri, CPU güç limitleri, fan eğrileri, BIOS/EC firmware, ACPI batarya davranışı ve [sonraki aşamalar](SONRAKI_ASAMALAR.md) bu uygulamada yoktur. ACPI batarya sorunlarının çözüldüğü iddia edilmez.

## 4. Yeniden üretme

```bash
make test                                   # 101 test / 1818 denetim
make -j2 exe demo installer                 # mingw-w64 + NSIS
python3 -I tools/verify_pe.py build/MonsterGpuPower.exe
python3 -I tools/verify_pe.py build/MonsterGpuPower_DEMO.exe --demo
```

`MonsterGpuPower.exe` aynı kaynak ve araç zinciriyle bayt bayt yeniden üretilir (`--no-insert-timestamp`). Kurulum paketi NSIS'in gömdüğü dosya zamanları yüzünden farklı bir derlemede farklı bayt üretir; içeriği aynıdır.
