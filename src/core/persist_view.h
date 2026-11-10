// Ana penceredeki "kalıcılık" kartının durum satırı için ARAYÜZ MANTIĞI (saf; OS çağrısı yok, sahte verilerle birim testlenir):
// gösterilecek durum GERÇEK OEM denetiminden + GERÇEK canlı (EC + NVIDIA) durumdan türetilir. Kullanıcıya görünen Türkçe metinler
// burada DEĞİL, win/strings_tr.h içindedir.
#pragma once
#include <string>

#include "oem_profile.h"
#include "settings.h"

namespace mgp {

enum class PersistKind {
  Checking,     // OEM denetimi henüz yok / sürüyor
  Locked,       // OEM profil modülü kullanılamıyor: profil uygulanamaz (neden: lock)
  Matches,      // OEM dosyasındaki hedef == anlık (EC+NVIDIA tutarlı) profil
  Differs,      // OEM dosyasındaki hedef != anlık profil
  LiveUnknown   // OEM dosyası okundu ama anlık profil tanınmıyor/tutarsız/okunamadı
};

// Ayardaki sahiplik kanıtının MEVCUT dosyayla ilişkisi.
enum class OwnerKind {
  None,     // kayıt yok (ör. yeni kurulum, ayar silinmiş) ya da dosya okunamadı
  Mine,     // dosyanın SHA-256'sı bu uygulamanın bıraktığı sürümle aynı
  Changed   // kayıt var ama dosya farklı: başka bir program (Control Center/OEM güncellemesi) değiştirmiş olabilir
};

struct PersistViewInput {
  bool haveInsp = false;
  oem::Inspection insp;  // en son GERÇEK OEM denetimi
  int liveWatts = 0;     // EC ve NVIDIA'nın birlikte doğruladığı aktif profil (0 = yok/tutarsız/okunamadı)
  OemOwnership own;      // ayardaki sahiplik kanıtı
};

struct PersistView {
  PersistKind kind = PersistKind::Checking;
  oem::Lock lock = oem::Lock::None;  // Locked ise neden
  int oemWatts = 0;                  // OEM dosyasındaki hedef (0 = bilinmiyor)
  int liveWatts = 0;
  OwnerKind owner = OwnerKind::None;
  std::string serviceVersion;        // ServiceVersion kilidinde bulunan sürüm (varsa)
};

PersistView DerivePersistView(const PersistViewInput& in);

}  // namespace mgp
