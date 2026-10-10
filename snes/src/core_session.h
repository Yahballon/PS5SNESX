// (AI-assisted) PS5SNES libretro host. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <libretro.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace snes {
class Session {
public:
    explicit Session(std::string save_directory);
    ~Session();
    bool Load(const std::string& path, std::vector<uint8_t> rom);
    bool Unload();
    void Run();
    bool SaveBattery();
    bool SaveState();
    bool LoadState();
    void SetOption(const std::string& key, const std::string& value) { m_variables[key] = value; }
    bool loaded() const { return m_loaded; }
    std::string error;
    std::vector<uint32_t> pixels;
    std::vector<int16_t> audio;
    unsigned width = 256, height = 240;
    double fps = 60, sample_rate = 48000;
    uint16_t buttons[2] = {};
private:
    static Session* current;
    static bool Environment(unsigned cmd, void* data);
    static void Video(const void*, unsigned, unsigned, size_t);
    static void Sample(int16_t, int16_t);
    static size_t Samples(const int16_t*, size_t);
    static int16_t Input(unsigned, unsigned, unsigned, unsigned);
    static void Log(enum retro_log_level, const char*, ...);
    std::string m_directory, m_path, m_name, m_content_dir, m_extension, m_save_path;
    std::vector<uint8_t> m_rom;
    std::map<std::string, std::string> m_variables;
    retro_game_info_ext m_info = {};
    retro_pixel_format m_format = RETRO_PIXEL_FORMAT_0RGB1555;
    bool m_loaded = false;
};
}
