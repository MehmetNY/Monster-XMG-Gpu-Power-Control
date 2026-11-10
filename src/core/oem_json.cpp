#include "oem_json.h"

#include <cstring>
#include <unordered_set>

#include "ec_constants.h"

namespace mgp {
namespace oem {

// ---------------------------------------------------------------------------------------------------- UTF-8
bool IsStrictUtf8(const uint8_t* p, size_t n) {
  size_t i = 0;
  while (i < n) {
    const uint8_t c = p[i];
    if (c < 0x80) {
      ++i;
      continue;
    }
    size_t need = 0;
    uint8_t lo = 0x80, hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) {
      need = 1;
    } else if (c == 0xE0) {
      need = 2;
      lo = 0xA0;
    } else if (c >= 0xE1 && c <= 0xEC) {
      need = 2;
    } else if (c == 0xED) {
      need = 2;
      hi = 0x9F;  // U+D800..U+DFFF (vekil çiftler) geçersiz
    } else if (c == 0xEE || c == 0xEF) {
      need = 2;
    } else if (c == 0xF0) {
      need = 3;
      lo = 0x90;
    } else if (c >= 0xF1 && c <= 0xF3) {
      need = 3;
    } else if (c == 0xF4) {
      need = 3;
      hi = 0x8F;  // > U+10FFFF geçersiz
    } else {
      return false;  // 0x80..0xC1 (devam baytı / kısa kodlama), 0xF5..0xFF
    }
    if (i + need >= n) return false;  // devam baytları için yeterli veri yok (p[i+1..i+need] dizi içinde olmalı)
    if (p[i + 1] < lo || p[i + 1] > hi) return false;
    for (size_t k = 2; k <= need; ++k)
      if (p[i + k] < 0x80 || p[i + k] > 0xBF) return false;
    i += need + 1;
  }
  return true;
}

EnvelopeError CheckEnvelope(const std::vector<uint8_t>& bytes, Envelope& env) {
  env = Envelope();
  if (bytes.empty()) return EnvelopeError::Empty;
  if (bytes.size() > kMaxJsonBytes) return EnvelopeError::TooLarge;
  if (bytes.size() >= 2 && ((bytes[0] == 0xFF && bytes[1] == 0xFE) || (bytes[0] == 0xFE && bytes[1] == 0xFF)))
    return EnvelopeError::Utf16OrUtf32;
  env.bom = bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF;
  env.bodyOffset = env.bom ? 3 : 0;
  if (env.bodyOffset >= bytes.size()) return EnvelopeError::Empty;
  if (!IsStrictUtf8(bytes.data() + env.bodyOffset, bytes.size() - env.bodyOffset)) return EnvelopeError::InvalidUtf8;
  return EnvelopeError::None;
}

const char* EnvelopeErrorName(EnvelopeError e) {
  switch (e) {
    case EnvelopeError::None: return "ok";
    case EnvelopeError::Empty: return "file is empty";
    case EnvelopeError::TooLarge: return "file is larger than the safety limit";
    case EnvelopeError::Utf16OrUtf32: return "UTF-16/UTF-32 encoding (unknown encoding, refused)";
    case EnvelopeError::InvalidUtf8: return "invalid UTF-8 (unknown encoding, refused)";
  }
  return "?";
}

// ---------------------------------------------------------------------------------------------------- JSON ayrıştırıcı
struct Parser {
  const uint8_t* s;
  size_t n;
  size_t i = 0;
  JsonDoc& d;
  std::string& err;
  int depth = 0;

  Parser(const uint8_t* body, size_t len, JsonDoc& doc, std::string& e) : s(body), n(len), d(doc), err(e) {}

  bool Fail(const char* m) {
    err = std::string(m) + " (byte " + std::to_string(i) + ")";
    return false;
  }
  void Ws() {
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
  }
  bool NewNode(JKind k, size_t b, int& idx) {
    if (d.nodes_.size() >= kMaxJsonNodes) return Fail("too many JSON nodes");
    idx = static_cast<int>(d.nodes_.size());
    d.nodes_.emplace_back();
    d.nodes_.back().kind = k;
    d.nodes_.back().begin = b;
    d.nodes_.back().end = b;
    return true;
  }
  static void AppendUtf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) {
      o.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      o.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      o.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  bool Hex4(uint32_t& cp) {
    if (i + 4 > n) return Fail("truncated \\u escape");
    cp = 0;
    for (int k = 0; k < 4; ++k) {
      const uint8_t c = s[i + static_cast<size_t>(k)];
      uint32_t v;
      if (c >= '0' && c <= '9') v = c - '0';
      else if (c >= 'a' && c <= 'f') v = 10u + (c - 'a');
      else if (c >= 'A' && c <= 'F') v = 10u + (c - 'A');
      else return Fail("bad hex digit in \\u escape");
      cp = (cp << 4) | v;
    }
    i += 4;
    return true;
  }
  bool String(std::string& out) {
    ++i;  // açılış tırnağı
    out.clear();
    for (;;) {
      if (i >= n) return Fail("unterminated string");
      const uint8_t c = s[i];
      if (c == '"') {
        ++i;
        return true;
      }
      if (c < 0x20) return Fail("control character inside string");
      if (c != '\\') {
        out.push_back(static_cast<char>(c));
        ++i;
        continue;
      }
      ++i;
      if (i >= n) return Fail("unterminated escape");
      const uint8_t e = s[i++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          uint32_t cp = 0;
          if (!Hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 2 > n || s[i] != '\\' || s[i + 1] != 'u') return Fail("lone high surrogate");
            i += 2;
            uint32_t lo = 0;
            if (!Hex4(lo)) return false;
            if (lo < 0xDC00 || lo > 0xDFFF) return Fail("invalid surrogate pair");
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return Fail("lone low surrogate");
          }
          if (cp == 0) return Fail("NUL escape in string");
          AppendUtf8(out, cp);
          break;
        }
        default: return Fail("invalid escape");
      }
    }
  }
  bool Number(int& idx) {
    const size_t b = i;
    if (s[i] == '-') ++i;
    if (i >= n) return Fail("bad number");
    size_t digitsStart = i;
    if (s[i] == '0') {
      ++i;
    } else if (s[i] >= '1' && s[i] <= '9') {
      while (i < n && s[i] >= '0' && s[i] <= '9') ++i;
    } else {
      return Fail("bad number");
    }
    const size_t digitsEnd = i;
    bool integer = true;
    if (i < n && s[i] == '.') {
      integer = false;
      ++i;
      if (i >= n || s[i] < '0' || s[i] > '9') return Fail("bad number fraction");
      while (i < n && s[i] >= '0' && s[i] <= '9') ++i;
    }
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
      integer = false;
      ++i;
      if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
      if (i >= n || s[i] < '0' || s[i] > '9') return Fail("bad number exponent");
      while (i < n && s[i] >= '0' && s[i] <= '9') ++i;
    }
    if (!NewNode(JKind::Number, b, idx)) return false;
    JNode& nd = d.nodes_[static_cast<size_t>(idx)];
    nd.end = i;
    nd.isInteger = integer;
    if (integer && digitsEnd - digitsStart <= 18) {
      long long v = 0;
      for (size_t k = digitsStart; k < digitsEnd; ++k) v = v * 10 + (s[k] - '0');
      nd.intValue = (s[b] == '-') ? -v : v;
      nd.intFits = true;
    }
    return true;
  }
  bool Literal(const char* word, JKind k, int& idx) {
    const size_t len = std::strlen(word);
    if (i + len > n || std::memcmp(s + i, word, len) != 0) return Fail("invalid literal");
    if (!NewNode(k, i, idx)) return false;
    i += len;
    d.nodes_[static_cast<size_t>(idx)].end = i;
    return true;
  }
  bool Object(int& idx) {
    if (++depth > kMaxJsonDepth) return Fail("nesting too deep");
    if (!NewNode(JKind::Object, i, idx)) return false;
    ++i;  // '{'
    Ws();
    if (i < n && s[i] == '}') {
      ++i;
      d.nodes_[static_cast<size_t>(idx)].end = i;
      --depth;
      return true;
    }
    std::unordered_set<std::string> seen;
    for (;;) {
      Ws();
      if (i >= n || s[i] != '"') return Fail("expected string key");
      std::string key;
      if (!String(key)) return false;
      if (!seen.insert(key).second) return Fail("duplicate key");
      Ws();
      if (i >= n || s[i] != ':') return Fail("expected ':'");
      ++i;
      Ws();
      int child = -1;
      if (!Value(child)) return false;
      d.nodes_[static_cast<size_t>(idx)].members.emplace_back(std::move(key), child);
      Ws();
      if (i >= n) return Fail("unterminated object");
      if (s[i] == ',') {
        ++i;
        continue;
      }
      if (s[i] == '}') {
        ++i;
        break;
      }
      return Fail("expected ',' or '}'");
    }
    d.nodes_[static_cast<size_t>(idx)].end = i;
    --depth;
    return true;
  }
  bool Array(int& idx) {
    if (++depth > kMaxJsonDepth) return Fail("nesting too deep");
    if (!NewNode(JKind::Array, i, idx)) return false;
    ++i;  // '['
    Ws();
    if (i < n && s[i] == ']') {
      ++i;
      d.nodes_[static_cast<size_t>(idx)].end = i;
      --depth;
      return true;
    }
    for (;;) {
      Ws();
      int child = -1;
      if (!Value(child)) return false;
      d.nodes_[static_cast<size_t>(idx)].items.push_back(child);
      Ws();
      if (i >= n) return Fail("unterminated array");
      if (s[i] == ',') {
        ++i;
        continue;
      }
      if (s[i] == ']') {
        ++i;
        break;
      }
      return Fail("expected ',' or ']'");
    }
    d.nodes_[static_cast<size_t>(idx)].end = i;
    --depth;
    return true;
  }
  bool Value(int& idx) {
    if (i >= n) return Fail("unexpected end of input");
    switch (s[i]) {
      case '{': return Object(idx);
      case '[': return Array(idx);
      case '"': {
        const size_t b = i;
        std::string str;
        if (!String(str)) return false;
        if (!NewNode(JKind::String, b, idx)) return false;
        d.nodes_[static_cast<size_t>(idx)].end = i;
        d.nodes_[static_cast<size_t>(idx)].str = std::move(str);
        return true;
      }
      case 't': return Literal("true", JKind::True, idx);
      case 'f': return Literal("false", JKind::False, idx);
      case 'n': return Literal("null", JKind::Null, idx);
      default:
        if (s[i] == '-' || (s[i] >= '0' && s[i] <= '9')) return Number(idx);
        return Fail("unexpected character");
    }
  }
};

bool JsonDoc::Parse(const uint8_t* body, size_t n, std::string& err) {
  nodes_.clear();
  err.clear();
  Parser p(body, n, *this, err);
  p.Ws();
  int root = -1;
  if (!p.Value(root)) {
    nodes_.clear();
    return false;
  }
  p.Ws();
  if (p.i != n) {
    p.Fail("trailing data after the JSON value");
    nodes_.clear();
    return false;
  }
  if (nodes_.empty() || root != 0) {
    err = "internal: root node is not first";
    nodes_.clear();
    return false;
  }
  return true;
}

const JNode* JsonDoc::Member(const JNode& obj, const char* key) const {
  if (obj.kind != JKind::Object) return nullptr;
  for (const auto& m : obj.members)
    if (m.first == key) return &nodes_[static_cast<size_t>(m.second)];
  return nullptr;
}

size_t JsonDoc::CountKeyAnywhere(const char* key) const {
  size_t c = 0;
  for (const JNode& nd : nodes_) {
    if (nd.kind != JKind::Object) continue;
    for (const auto& m : nd.members)
      if (m.first == key) ++c;
  }
  return c;
}

int FlagValue(const JsonDoc&, const JNode* n) {
  if (!n) return -1;
  if (n->kind == JKind::True) return 1;
  if (n->kind == JKind::False) return 0;
  if (n->kind == JKind::Number && n->isInteger && n->intFits) {
    if (n->intValue == 0) return 0;
    if (n->intValue == 1) return 1;
  }
  return -1;
}

// ---------------------------------------------------------------------------------------------------- Şema
namespace {

size_t CountRaw(const uint8_t* p, size_t n, const char* needle) {
  const size_t m = std::strlen(needle);
  size_t c = 0;
  if (m == 0 || n < m) return 0;
  for (size_t i = 0; i + m <= n; ++i)
    if (std::memcmp(p + i, needle, m) == 0) ++c;
  return c;
}

ProfileFacts Bad(ProfileFacts f, Defect d, const std::string& why) {
  f.ok = false;
  f.defect = d;
  f.detail = why;
  return f;
}

}  // namespace

ProfileFacts ParseProfile(const std::vector<uint8_t>& bytes) {
  ProfileFacts f;
  Envelope env;
  const EnvelopeError ee = CheckEnvelope(bytes, env);
  if (ee != EnvelopeError::None) {
    f.envelope = ee;
    return Bad(f, Defect::Envelope, EnvelopeErrorName(ee));
  }
  f.bom = env.bom;
  f.bodyOffset = env.bodyOffset;
  const uint8_t* body = bytes.data() + env.bodyOffset;
  const size_t bn = bytes.size() - env.bodyOffset;

  JsonDoc d;
  std::string perr;
  if (!d.Parse(body, bn, perr)) return Bad(f, Defect::Syntax, "JSON syntax: " + perr);
  const JNode& root = d.Root();
  if (root.kind != JKind::Object) return Bad(f, Defect::RootNotObject, "JSON root is not an object");

  const JNode* act = d.Member(root, "Activated");
  if (!act) return Bad(f, Defect::Missing, "missing Activated");
  f.activated = FlagValue(d, act);
  if (f.activated < 0) return Bad(f, Defect::WrongType, "Activated is not a boolean/0/1");

  const JNode* gpu = d.Member(root, "GPU");
  if (!gpu) return Bad(f, Defect::Missing, "missing GPU object");
  if (gpu->kind != JKind::Object) return Bad(f, Defect::WrongType, "GPU is not an object");

  const JNode* sw = d.Member(*gpu, "ConfigurableTGPSwitch");
  const JNode* db = d.Member(*gpu, "DynamicBoostSwitch");
  const JNode* tg = d.Member(*gpu, "ConfigurableTGPTarget");
  if (!sw) return Bad(f, Defect::Missing, "missing GPU.ConfigurableTGPSwitch");
  if (!db) return Bad(f, Defect::Missing, "missing GPU.DynamicBoostSwitch");
  if (!tg) return Bad(f, Defect::Missing, "missing GPU.ConfigurableTGPTarget");
  f.tgpSwitch = FlagValue(d, sw);
  f.dynBoostSwitch = FlagValue(d, db);
  if (f.tgpSwitch < 0) return Bad(f, Defect::WrongType, "GPU.ConfigurableTGPSwitch is not a boolean/0/1");
  if (f.dynBoostSwitch < 0) return Bad(f, Defect::WrongType, "GPU.DynamicBoostSwitch is not a boolean/0/1");
  if (tg->kind != JKind::Number) return Bad(f, Defect::WrongType, "GPU.ConfigurableTGPTarget is not a number");

  // Kaynak betikteki "tek benzersiz hedef alanı" şartı: hem belge ağacında hem ham metinde TAM bir tane.
  if (d.CountKeyAnywhere("ConfigurableTGPTarget") != 1) return Bad(f, Defect::TargetAmbiguous, "ConfigurableTGPTarget is not unique in the document");
  if (CountRaw(body, bn, "\"ConfigurableTGPTarget\"") != 1) return Bad(f, Defect::TargetAmbiguous, "ConfigurableTGPTarget text is not unique in the file");

  if (!tg->isInteger || !tg->intFits || tg->end - tg->begin != 3 || tg->intValue < 100 || tg->intValue > 999)
    return Bad(f, Defect::TargetFormat, "GPU.ConfigurableTGPTarget is not a plain 3-digit integer token");
  for (size_t k = tg->begin; k < tg->end; ++k)
    if (body[k] < '0' || body[k] > '9') return Bad(f, Defect::TargetFormat, "target token is not 3 ASCII digits");

  f.target = static_cast<int>(tg->intValue);
  f.targetFileOffset = env.bodyOffset + tg->begin;
  f.ok = true;
  f.defect = Defect::None;
  f.detail.clear();
  return f;
}

MainOptionFacts ParseMainOption(const std::vector<uint8_t>& bytes) {
  MainOptionFacts f;
  Envelope env;
  const EnvelopeError ee = CheckEnvelope(bytes, env);
  if (ee != EnvelopeError::None) {
    f.defect = Defect::Envelope;
    f.envelope = ee;
    f.detail = EnvelopeErrorName(ee);
    return f;
  }
  JsonDoc d;
  std::string perr;
  if (!d.Parse(bytes.data() + env.bodyOffset, bytes.size() - env.bodyOffset, perr)) {
    f.defect = Defect::Syntax;
    f.detail = "JSON syntax: " + perr;
    return f;
  }
  const JNode& root = d.Root();
  if (root.kind != JKind::Object) {
    f.defect = Defect::RootNotObject;
    f.detail = "JSON root is not an object";
    return f;
  }
  const JNode* om = d.Member(root, "OperatingMode");
  const JNode* ti = d.Member(root, "TurboProfileIndex");
  if (!om || !ti) {
    f.defect = Defect::Missing;
    f.detail = !om ? "missing OperatingMode" : "missing TurboProfileIndex";
    return f;
  }
  if (om->kind != JKind::Number || !om->isInteger || !om->intFits || ti->kind != JKind::Number || !ti->isInteger || !ti->intFits) {
    f.defect = Defect::WrongType;
    f.detail = "OperatingMode/TurboProfileIndex are not integers";
    return f;
  }
  f.operatingMode = om->intValue;
  f.turboProfileIndex = ti->intValue;
  f.ok = true;
  return f;
}

// ---------------------------------------------------------------------------------------------------- Yama
bool PatchTarget(const std::vector<uint8_t>& original, int newWatts, std::vector<uint8_t>& out, std::string& err) {
  out.clear();
  err.clear();
  if (!FindProfile(newWatts)) {
    err = "refused: " + std::to_string(newWatts) + " W is not a supported profile";
    return false;
  }
  const std::string digits = std::to_string(newWatts);
  if (digits.size() != 3) {
    err = "refused: target is not a 3-digit value";
    return false;
  }
  const ProfileFacts f = ParseProfile(original);
  if (!f.ok) {
    err = "refused: source file is not a supported OEM profile (" + f.detail + ")";
    return false;
  }
  std::vector<uint8_t> patched = original;
  for (size_t k = 0; k < 3; ++k) patched[f.targetFileOffset + k] = static_cast<uint8_t>(digits[k]);

  // Çıktının bağımsız doğrulaması: aynı uzunluk, yalnız o 3 bayt farklı, yeniden çözümlenince yeni hedef, diğer alanlar aynı.
  if (patched.size() != original.size()) {
    err = "internal: patched size differs";
    return false;
  }
  for (size_t k = 0; k < original.size(); ++k) {
    const bool inside = k >= f.targetFileOffset && k < f.targetFileOffset + 3;
    if (!inside && patched[k] != original[k]) {
      err = "internal: a byte outside the target changed";
      return false;
    }
  }
  const ProfileFacts g = ParseProfile(patched);
  if (!g.ok || g.target != newWatts || g.targetFileOffset != f.targetFileOffset || g.activated != f.activated ||
      g.tgpSwitch != f.tgpSwitch || g.dynBoostSwitch != f.dynBoostSwitch || g.bom != f.bom) {
    err = "internal: patched file failed re-parse verification";
    return false;
  }
  out = std::move(patched);
  return true;
}

bool SameModuloTarget(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
  if (a.size() != b.size()) return false;
  const ProfileFacts fa = ParseProfile(a), fb = ParseProfile(b);
  if (!fa.ok || !fb.ok || fa.targetFileOffset != fb.targetFileOffset || fa.bom != fb.bom) return false;
  for (size_t k = 0; k < a.size(); ++k) {
    if (k >= fa.targetFileOffset && k < fa.targetFileOffset + 3) continue;
    if (a[k] != b[k]) return false;
  }
  return true;
}

}  // namespace oem
}  // namespace mgp
