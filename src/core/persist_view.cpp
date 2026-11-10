#include "persist_view.h"

#include "ec_constants.h"

namespace mgp {

PersistView DerivePersistView(const PersistViewInput& in) {
  PersistView v;
  v.liveWatts = in.liveWatts;
  if (!in.haveInsp) {
    v.kind = PersistKind::Checking;
    return v;
  }
  const oem::Inspection& i = in.insp;

  // Sahiplik kanıtının mevcut dosyayla ilişkisi: yalnız dosya gerçekten okunabildiyse (SHA-256 var) hüküm verilir.
  if (in.own.Present() && i.haveFile && !i.sha.empty()) v.owner = (i.sha == in.own.ownedSha) ? OwnerKind::Mine : OwnerKind::Changed;

  v.oemWatts = (i.haveFile && FindProfile(i.target)) ? i.target : 0;

  if (!i.ok) {
    v.kind = PersistKind::Locked;
    v.lock = i.lock;
    if (i.lock == oem::Lock::ServiceVersion && i.svc.versionRead) v.serviceVersion = i.svc.version;
    return v;
  }

  if (in.liveWatts == 0) v.kind = PersistKind::LiveUnknown;
  else if (in.liveWatts == v.oemWatts) v.kind = PersistKind::Matches;
  else v.kind = PersistKind::Differs;
  return v;
}

}  // namespace mgp
