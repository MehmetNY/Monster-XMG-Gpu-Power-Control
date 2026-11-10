#pragma once
#include "../core/settings.h"

namespace mgp {
namespace win {

Settings LoadSettings();
bool SaveSettings(const Settings& s);

}  // namespace win
}  // namespace mgp
