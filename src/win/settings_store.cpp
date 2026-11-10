#include "settings_store.h"

#include "win_util.h"

namespace mgp {
namespace win {

static std::wstring SettingsPath() { return SettingsDir() + L"\\settings.ini"; }

Settings LoadSettings() {
  Settings s;
  std::string text;
  if (ReadFileAll(SettingsPath(), text, 16 * 1024)) s = ParseSettings(text);
  return s;
}

bool SaveSettings(const Settings& s) { return WriteFileAtomic(SettingsPath(), SerializeSettings(s)); }

}  // namespace win
}  // namespace mgp
