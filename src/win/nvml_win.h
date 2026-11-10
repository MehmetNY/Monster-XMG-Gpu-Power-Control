// NVIDIA NVML: sürücüyle gelen nvml.dll dinamik yüklenir (nvidia-smi çalıştırılmaz, ek yazılım kurdurulmaz).
// YALNIZ OKUMA: bu uygulama NVML üzerinden güç limiti YAZMAZ.
#pragma once
#include "../core/interfaces.h"
#include "win_util.h"

namespace mgp {
namespace win {

class NvmlTelemetry : public INvidiaTelemetry {
 public:
  NvmlTelemetry();
  ~NvmlTelemetry() override;
  bool Init(std::string& err) override;
  bool ReadStatic(NvStatic& out, std::string& err) override;
  bool ReadDynamic(NvDynamic& out) override;
  void Shutdown() override;

 private:
  struct Fns;
  Fns* f_ = nullptr;
  HMODULE mod_ = nullptr;
  void* dev_ = nullptr;
  bool inited_ = false;
};

}  // namespace win
}  // namespace mgp
