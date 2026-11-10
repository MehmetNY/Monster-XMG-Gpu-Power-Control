#include "oem_names.h"

#include <cstring>

namespace mgp {
namespace oem {
namespace {
bool Digits(const std::string& s, size_t pos, size_t n) {
  if (pos + n > s.size()) return false;
  for (size_t i = 0; i < n; ++i)
    if (s[pos + i] < '0' || s[pos + i] > '9') return false;
  return true;
}
bool StartsWith(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
}  // namespace

bool IsHex8(const std::string& s) {
  if (s.size() != 8) return false;
  for (char c : s)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

bool IsStamp(const std::string& s) { return s.size() == 15 && Digits(s, 0, 8) && s[8] == '-' && Digits(s, 9, 6); }

std::string MakeBackupName(const std::string& stamp, const std::string& id) {
  return std::string(kProfileLeaf) + kBackupInfix + stamp + "-" + id + ".bak";
}

bool ParseBackupName(const std::string& leaf, std::string* stamp, std::string* id) {
  const std::string head = std::string(kProfileLeaf) + kBackupInfix;
  const std::string tail = ".bak";
  // baş + 15 (stamp) + '-' + 8 (id) + ".bak"
  if (!StartsWith(leaf, head) || leaf.size() != head.size() + 15 + 1 + 8 + tail.size()) return false;
  const std::string st = leaf.substr(head.size(), 15);
  if (leaf[head.size() + 15] != '-') return false;
  const std::string i8 = leaf.substr(head.size() + 16, 8);
  if (leaf.compare(head.size() + 24, tail.size(), tail) != 0) return false;
  if (!IsStamp(st) || !IsHex8(i8)) return false;
  if (stamp) *stamp = st;
  if (id) *id = i8;
  return true;
}

std::string MakeTempName(const char* infix, const std::string& id) { return std::string(kProfileLeaf) + infix + id; }

bool IsOwnTempName(const std::string& leaf) {
  for (const char* infix : {kTmpInfix, kSwapInfix, kRejectedInfix}) {
    const std::string head = std::string(kProfileLeaf) + infix;
    if (StartsWith(leaf, head) && IsHex8(leaf.substr(head.size()))) return true;
  }
  return false;
}

bool IsKnownLeaf(const std::string& leaf) {
  return leaf == kMainOptionLeaf || leaf == kProfileLeaf || ParseBackupName(leaf, nullptr, nullptr) || IsOwnTempName(leaf);
}

}  // namespace oem
}  // namespace mgp
