# Cihaz kabul testi (v1.4.0) — kendi cihazınızda

> **Bu listeyi SİZ, kendi Monster Tulpar T5 V21.7 cihazınızda yapacaksınız.** Uygulama gerçek donanımda henüz çalıştırılmadı; bu adımların hiçbiri yazar tarafından gerçek cihazda yürütülmedi.
> Sonucu siz doldurun; geçmeyen adımı **geçti diye varsaymayın**. Bir adım kalırsa bir [Issue](../../../issues) açıp çıktıyı ve `%LOCALAPPDATA%\MonsterGpuPower\logs\mgp.log` dosyasını ekleyin.
>
> Kapsam dışı (bu testte YAPMAYIN): FurMark / Cinebench / CPU+GPU tam yük; 230 W adaptör politikası ve 330 W sınırlaması; ACPI batarya davranışı (bu proje bunları çözdüğünü iddia etmez).
> Aşağıdaki PowerShell komutları **yönetici** penceresinde çalışır ve yalnızca **okur / kopyalar**.

Her adımda aynı üç doğrulama kullanılır, kısaca **(J) (E) (N)**:

| | Nasıl bakılır | Beklenen (150 W için) |
|---|---|---|
| **(J)** OEM JSON | `Select-String -Path "$d\Mode3_Profile1.json" -Pattern ConfigurableTGPTarget` | `"ConfigurableTGPTarget": 150` |
| **(E)** EC | uygulamanın üst panelinde *EC 0x0743 / 0x0744* | `0x05 / 25 W` |
| **(N)** NVIDIA | `nvidia-smi -q -d POWER` → *Current Power Limit* | `150.00 W` |

Ortak değişken (her PowerShell penceresinde bir kez):

```powershell
$d = 'C:\Program Files\OEM\Control Center\UniwillService\MyControlCenter\UserPofiles'
```

## 0. Hazırlık: yedek ve başlangıç kaydı (uygulamayı AÇMADAN önce)

1. GCUService sürümü **1.0.2.70** olmalı:
   `(Get-Item 'C:\Program Files\OEM\Control Center\UniwillService\MyControlCenter\GCUService.exe').VersionInfo.FileVersion`
   Farklıysa uygulama yazmayı zaten reddeder; testi durdurun ve bir Issue açın.
2. OEM klasörünü **ayrıca yedekleyin**:
   ```powershell
   $b = "$env:USERPROFILE\Desktop\OEM_UserPofiles_YEDEK_$(Get-Date -Format yyyyMMdd-HHmmss)"
   New-Item -ItemType Directory $b | Out-Null
   Copy-Item "$d\*" $b -Force
   Get-ChildItem $b | Select-Object Name, Length
   ```
3. Başlangıç kaydını alıp saklayın (kıyas için):
   ```powershell
   Get-ChildItem $d -File | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
   Get-ChildItem $d -File | Get-FileHash -Algorithm SHA256 | Select-Object @{n='Dosya';e={Split-Path $_.Path -Leaf}}, Hash | Format-Table -AutoSize
   ```
4. Görev Zamanlayıcı'daki görevleri not edin (kurulumdan önce):
   ```powershell
   schtasks /query /tn MonsterGpuPower_ApplyAtLogon   # v1.0–v1.3'te "açılışta uygula"yı kurduysanız VAR (hata = yok)
   schtasks /query /tn MonsterGpuPower_Autostart      # açılışta başlat açıksa VAR
   ```

## 1. Kurulum, ilk açılışta SIFIR yazma

1. `MONSTER_GPU_POWER_Setup.exe` ile kurun (SHA-256'yı `SHA256SUMS.txt` ile karşılaştırın). Kurulum, eski `MonsterGpuPower_ApplyAtLogon` görevi varsa **siler** (hiçbir profil uygulamaz);
   OEM klasörüne dokunmaz.
2. 0.3 komutlarını yeniden çalıştırın. **Geçti:** dosya listesi, boyutlar, `LastWriteTime` ve SHA-256'lar **birebir aynı**; yeni `.bak` yok. ☐ Geçti ☐ Kaldı
3. Uygulamayı açın. Durum şeridinde cihaz kimliği *doğrulandı*, sürücü *Bağlı* görünmeli; *KALICI UYGULAMA* kutusunda dosyadaki gerçek değer görünmeli
   (ör. `OEM profilinde 140 W kayıtlı · Anlık profil: 140 W (OEM varsayılanı)`). Yalnızca açmak hiçbir şey yazmamalıdır; 0.3'ü tekrar edip doğrulayın. ☐ Geçti ☐ Kaldı

## 2. İlk kalıcı profil (önerilen sıra: 145 → 150)

1. **145 W** düğmesine tıklayın; risk onayını okuyup onaylayın. (Dosyada 140 W dışında bir değer varsa — ör. eski elle denemelerden — önce "yabancı değer" kutusu çıkar; 5. bölümdeki gibi yanıtlayın.)
2. Sonuç satırı iki katmanı bildirmeli: *(a) anlık: EC geri okundu, NVIDIA 145 W doğruladı*; *(b) OEM profil dosyası: ConfigurableTGPTarget = 145 yazıldı ve geri okundu*.
3. Doğrulayın: **(J)** `145`; EC `0x05 / 20 W`; NVIDIA `145.00 W`.
4. `$d` klasöründe **tam 1 yeni** `Mode3_Profile1.json.mgp-original-<tarih>-<kod>.bak` olmalı ve içeriği 0.3'teki özgün dosyayla bayt bayt aynı olmalı (SHA-256 karşılaştırın). Yedek salt-okunur olmalı.
5. `Mode3_Profile1.json` özgün dosyadan yalnızca `ConfigurableTGPTarget` değerinde farklı olmalı (örn. `fc.exe` veya bir fark aracıyla). ☐ Geçti ☐ Kaldı
6. Aynı profile **tekrar tıklayın**: yazma ve yeni `.bak` olmamalı ("zaten etkin / OEM profili zaten …" iletisi). ☐ Geçti ☐ Kaldı
7. Sonra **150 W** seçin; aynı doğrulamaları (150 / `0x05 / 25 W` / `150.00 W`) yapın, bu kez toplam 2 yedek olmalı. ☐ Geçti ☐ Kaldı

## 3. Yeniden başlatma kalıcılığı (en önemli adım)

1. Uygulamadan **X ile** çıkın (tepsiye küçültmeyin; "Çıkışta OEM 140 W'a dön" **kapalı** olsun).
2. Bilgisayarı **yeniden başlatın**. Uygulamayı **AÇMADAN** (J), (E), (N) doğrulayın. ☐ Geçti ☐ Kaldı
3. En az **iki** normal yeniden başlatma ve birkaç ekran kapanma/açılma döngüsü sonrasında tekrarlayın. GCUService dosyayı **ne zaman** okuyor, uygulama bunu bilemez; bu test o soruyu sizin cihazınızda yanıtlar.
4. Sonuç tutmuyorsa (ör. 140 W'a dönüyorsa) bir Issue açın; **"başarılı" demeyin**, çünkü bu uygulama yeniden başlatma sonrası etkiyi doğrulayamaz.

## 4. Diğer profiller

125, 130, 135 ve 140 W için de 2. adımdaki doğrulamaları yapın. **130 ve 135 W canlı denenmemiş eşlemelerdir**: uygulama yazdıktan sonra EC'yi geri okur, NVIDIA'yı doğrular ve tutmazsa
önceki değere döner; yine de bu adımları **en sona** bırakın. Her profil için yeniden başlatma kalıcılığı (3. adım) ayrıca denenmemiştir. ☐ Geçti ☐ Kaldı

## 5. Yabancı değer koruması

1. Uygulamayı kapatın. `Mode3_Profile1.json` içindeki hedefi **elle** (yönetici olarak, yedeğiniz varken) bir başka izinli değere (ör. 130) değiştirin ve uygulamayı açın.
2. Bir profil seçin: *"OEM profil dosyasında şu an 130 W kayıtlı ve bu değeri bu uygulama yazmadı…"* kutusu çıkmalı. **Hayır** deyin → hiçbir şey değişmemeli (ne anlık değer ne dosya). ☐ Geçti ☐ Kaldı
3. Tekrar seçin, bu kez **Evet** deyin → önce anlık değer uygulanıp doğrulanmalı, sonra dosya yedeklenip değiştirilmeli. ☐ Geçti ☐ Kaldı

## 6. Ayarlar

* **Uygulamayı açılışta başlat:** açın; `schtasks /query /tn MonsterGpuPower_Autostart` görevi göstermeli. Yeniden oturum açın: uygulama ~10 sn sonra **tepside** başlamalı ve **hiçbir profil uygulamamalı**
  (0.3 komutlarıyla dosyaların değişmediğini doğrulayın). Kapatınca görev silinmeli. ☐ Geçti ☐ Kaldı
* **Çıkışta OEM 140 W'a dön:** açın, bir profil seçin, uygulamadan çıkın: anlık değer 140 W olmalı (NVIDIA `140.00 W`), OEM **dosyası değişmemeli** (seçtiğiniz değer kalmalı). ☐ Geçti ☐ Kaldı

## 7. Geri dönüş

Uygulamada **140 W** seçin ya da `$b` yedeğinden/`.bak` dosyasından `Mode3_Profile1.json`'u geri kopyalayın. Yedeklerin hiçbiri uygulama tarafından silinmez.

## 8. Kaldırma

Kaldırıcıyı çalıştırın: `MonsterGpuPower_Autostart` ve `MonsterGpuPower_ApplyAtLogon` görevleri silinmeli; OEM klasörü ve yedekler **değişmemeli** (0.3 komutlarıyla doğrulayın). ☐ Geçti ☐ Kaldı
