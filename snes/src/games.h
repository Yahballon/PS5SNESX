// (AI-assisted) SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace snes {
struct Game { std::string path, title; uint64_t bytes = 0; };
std::vector<Game> Scan(const std::vector<std::string>& directories);
bool ReadRom(const std::string& path, std::vector<uint8_t>& rom, std::string& error);
uint16_t MapButtons(uint32_t raw);
}
