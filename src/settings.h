#pragma once
#include "model.h"
namespace dp {
std::filesystem::path settingsPath();
Settings loadSettings();
bool saveSettings(const Settings &);
bool startupEnabled();
bool setStartup(bool enabled);
} // namespace dp
