#pragma once

#include <string>

// Native OHL Music settings window. Reads/writes the installed key=value config and asks the
// running daemon to reload the surround pipeline after Apply.
int RunMusicSettingsGui(const std::string& configPath);
