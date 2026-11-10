// Yalnız TESTLERİN kullandığı küçük yardımcılar (üretim kodunda çağıranı olmadığı için src/ altında durmazlar).
#pragma once
#include "../src/core/persist_txn.h"

namespace mgp {

// Anlık katman (EC + NVIDIA) doğrulandı ve geri alma gerekmedi.
inline bool LiveOk(const PersistTxnResult& r) { return r.live.Succeeded() && !r.liveRestoreAttempted; }

// İKİ katman da doğrulandı: anlık uygulama + OEM profil dosyası (geri alma yok).
inline bool BothLayersOk(const PersistTxnResult& r) {
  return r.persistAttempted && r.live.Succeeded() && r.oem.Success() && !r.liveRestoreAttempted;
}

// Bu uygulamanın OEM dosyasında KESİNLİKLE değişiklik yapmadığı doğrulanmış mı? (FailedIndeterminate / Committed = hayır.)
// Not: Refused/Changed durumunda dosyayı başka bir program değiştirmiş olabilir; "bizim değiştirmediğimiz" kesindir.
inline bool OemFileKnownUnchanged(const PersistTxnResult& t) {
  if (!t.persistAttempted) return true;  // commit hiç çağrılmadı
  switch (t.oem.kind) {
    case oem::CommitKind::Refused:
    case oem::CommitKind::FailedNoChange:
    case oem::CommitKind::AlreadyCurrent:
      return true;
    case oem::CommitKind::Committed:
    case oem::CommitKind::FailedIndeterminate:
      return false;
  }
  return false;
}

}  // namespace mgp
