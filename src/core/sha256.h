// SHA-256 (FIPS 180-4), saf C++, bağımlılıksız. OEM profil dosyasının parmak izi ve yedek doğrulaması için kullanılır.
// Kriptografik kimlik doğrulama değil, DEĞİŞİKLİK TESPİTİ içindir (TOCTOU / sahiplik kanıtı).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mgp {

class Sha256 {
 public:
  Sha256();
  void Update(const uint8_t* data, size_t len);
  // Özeti verir ve nesneyi sıfırlar.
  void Final(uint8_t out[32]);

 private:
  void Block(const uint8_t* p);
  uint32_t h_[8];
  uint8_t buf_[64];
  size_t bufLen_ = 0;
  uint64_t total_ = 0;
};

// Küçük harfli 64 karakterlik onaltılık özet.
std::string Sha256Hex(const uint8_t* data, size_t len);
inline std::string Sha256Hex(const std::vector<uint8_t>& v) { return Sha256Hex(v.data(), v.size()); }
inline std::string Sha256Hex(const std::string& s) { return Sha256Hex(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

// 64 onaltılık hane mi? (ayar dosyası doğrulaması)
bool IsSha256Hex(const std::string& s);

}  // namespace mgp
