# Sürücü arayüzü (ABI), EC haritası ve bağımsızlık sınırları

Bu belge, uygulamanın EC'ye nasıl eriştiğini ve bunun **neye dayandığını** (kanıt düzeyiyle) kaydeder.
Kanıt kaynağı: cihaz sahibinin kendi ölçümleri ve üreticinin `ACPIDriverDll.dll` dosyasındaki ReadEC/WriteEC makine kodunun incelenmesi
(OEM ikilileri bu depoda **yer almaz**). Tek merkezi sabit dosyası: [`src/core/ec_constants.h`](../src/core/ec_constants.h).

## 1. Aygıt ve IOCTL'ler (OEM DLL'nin ReadEC/WriteEC makine kodundan okundu)

| Öğe | Değer |
|---|---|
| Aygıt yolu | `\\.\ACPIDriver` (`CreateFileW`, GENERIC_READ\|WRITE) |
| ReadEC | `DeviceIoControl` kodu `0x9C40A488` (DeviceType 0x9C40, METHOD_BUFFERED, FILE_ANY_ACCESS/RW) |
| WriteEC | `DeviceIoControl` kodu `0x9C40A48C` |
| Tampon | giriş ve çıkış **aynı işaretçi**, ikisi de **0x400000 (4 MiB)** bayt; içerik `{DWORD adres; DWORD değer;}` |
| Okuma sonucu | çıkış tamponunun ilk baytı |
| OEM DLL'nin yaptığı hata | `DeviceIoControl` dönüş değerini yok sayar; başarısız çağrıda tampondaki adresin düşük baytı "eski yankı" olarak okunur (0x43/0x44 sahte durum) |

Uygulama bunları şöyle ele alır: dönüş değeri ve `bytesReturned >= 1` denetlenir; okuma sonucu yalnız IOCTL başarılıysa kabul edilir;
yalnız 0x0743–0x0746 okunur, yalnız izin listesindeki (adres, değer) çiftleri yazılır (`IsAllowedEcWrite`); her çağrıda tampon sıfırlanır.

**Doğrulanmamış varsayım:** Sürücünün daha küçük tamponu kabul edip etmediği bilinmiyor; bu yüzden OEM DLL ile **birebir aynı 4 MiB** kullanılır.
Her çağrı 4 MiB'lık geçici bellek ayırır/serbest bırakır (yoklama sırasında saniyede ~1 çağrı çifti; ihmal edilebilir yük).

## 2. EC haritası

| Adres | Anlam | Uygulama |
|---|---|---|
| `0x0743` | cTGP/DB kontrol baytı. `0x01` = OEM AutomaticOverboost KAPALI (125 W), `0x05` = AÇIK (bit0 genel + bit2 cTGP) | okur + **yazar** (yalnız 0x01/0x05) |
| `0x0744` | cTGP artışı (W), 5'in katları, 0..25 | okur + **yazar** |
| `0x0745` | TPP | yalnız okur (beklenen 0) |
| `0x0746` | Linux sürücüsünde "MAX_TGP" | yalnız okur (beklenen 0) |

Profil eşlemesi ve kanıt düzeyi:

| Profil | 0x0743 | 0x0744 | Kanıt |
|---|---|---|---|
| 125 W | 0x01 | 0 | OEM kapalıyken canlı ölçüldü |
| 130 W | 0x05 | 5 | **Beklenen eşleme, canlı denenmedi** |
| 135 W | 0x05 | 10 | **Beklenen eşleme, canlı denenmedi** |
| 140 W | 0x05 | 15 | OEM açıkken canlı ölçüldü |
| 145 W | 0x05 | 20 | EC + NVIDIA doğrulandı (yüksüz yoklama) |
| 150 W | 0x05 | 25 | EC + NVIDIA doğrulandı, yükte ~150 W ölçüldü |

`0x0743` bit adları Linux `uniwill-acpi.c` makrolarından alınmıştır (yalnız adlandırma; Linux bu baytı bu amaçla yazmıyor).
Yazma sırası: yükselirken önce kontrol baytı (ofset 0 iken = 125 W), sonra ofset; alçalırken önce ofset, sonra kontrol baytı.

## 3. Sürücü kimliği ve bağımlılıkların sınırı

* Çekirdek sürücü Uniwill **`ACPIDriver`** (PnP/INF) sürücüsüdür; donanım kimliği **`ACPI\INOU0000`**. Control Center'dan **ayrı** bir sürücü paketidir;
  Control Center'ın kendisi (`GCUService`) da sürücünün varlığını denetler.
* EC'ye erişim için bu uygulama **`ACPIDriver` çekirdek sürücüsünün kurulu ve çalışır olmasını gerektirir**. Sürücünün `.sys/.inf` dosyaları bu depoda **yoktur**
  ve yeniden dağıtılamaz (üreticiye aittir). Sürücü yoksa uygulama EC'ye yazmaz ve nedenini açıkça söyler.
* EC erişimi için Control Center'ın `ACPIDriverDll.dll` dosyasına gerek yoktur: uygulama aygıtı doğrudan `DeviceIoControl` ile açar (makine kodu analizine dayanır).
* **Kalıcılık için ise** Control Center'ın `GCUService` hizmeti gerekir: v1.4.0'da her profil seçimi, OEM profil dosyasına da kaydedilir ve OEM profil dosyası
  kullanılamıyorsa **hiçbir şey uygulanmaz** (bkz. [README](../README.md#kalıcılık-neye-dayanır)). Yani uygulama "Control Center'dan tamamen bağımsız" değildir.
* İmza denetimi/test-imzalama atlatma ve sürücü yükleme hilesi **yoktur ve önerilmez**.
