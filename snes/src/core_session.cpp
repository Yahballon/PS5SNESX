// (AI-assisted) SNES core callbacks and persistent saves. SPDX-License-Identifier: GPL-3.0-or-later
#include "core_session.h"
#include "fe_file_io.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace snes {
Session* Session::current = nullptr;
Session::Session(std::string directory) : m_directory(std::move(directory)) {
    current = this;
    retro_set_environment(Environment);
    retro_set_video_refresh(Video);
    retro_set_audio_sample(Sample);
    retro_set_audio_sample_batch(Samples);
    retro_set_input_poll([]{});
    retro_set_input_state(Input);
    retro_init();
    retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    retro_set_controller_port_device(1, RETRO_DEVICE_JOYPAD);
}
Session::~Session() { Unload(); retro_deinit(); current = nullptr; }
void Session::Log(enum retro_log_level, const char* format, ...) {
    va_list args; va_start(args, format); std::vprintf(format, args); va_end(args);
}
bool Session::Environment(unsigned cmd, void* data) {
    auto& s = *current;
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback*>(data)->log = Log; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char**>(data) = s.m_directory.c_str(); return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *static_cast<unsigned*>(data) = 0; return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        for (auto* v = static_cast<retro_variable*>(data); v && v->key; ++v) {
            if (!v->value) continue;
            std::string text = v->value;
            const auto semi = text.find(';');
            if (semi == std::string::npos) continue;
            const auto start = text.find_first_not_of(' ', semi + 1);
            if (start != std::string::npos)
                s.m_variables.try_emplace(v->key, text.substr(start, text.find('|', start) - start));
        }
        s.m_variables.try_emplace("bsnes_crop_overscan","disabled");
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* v = static_cast<retro_variable*>(data);
        auto found = s.m_variables.find(v->key ? v->key : "");
        v->value = found == s.m_variables.end() ? nullptr : found->second.c_str();
        return v->value != nullptr;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool*>(data) = false; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        const auto format = *static_cast<retro_pixel_format*>(data);
        if (format != RETRO_PIXEL_FORMAT_XRGB8888 && format != RETRO_PIXEL_FORMAT_RGB565 &&
            format != RETRO_PIXEL_FORMAT_0RGB1555) return false;
        s.m_format = format; return true;
    }
    case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
        *static_cast<const retro_game_info_ext**>(data) = &s.m_info; return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *static_cast<bool*>(data) = true; return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH; return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        const auto& av = *static_cast<retro_system_av_info*>(data);
        s.fps = av.timing.fps; s.sample_rate = av.timing.sample_rate; return true;
    }
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: return true;
    default: return false;
    }
}
bool Session::Load(const std::string& path, std::vector<uint8_t> rom) {
    if (!Unload()) return false;
    error.clear();
    if (rom.size() < 16 || rom.size() > (32u << 20)) { error = "Invalid ROM size"; return false; }
    m_path = path; m_rom = std::move(rom);
    const auto slash = path.find_last_of('/');
    m_content_dir = slash == std::string::npos ? "" : path.substr(0, slash);
    m_name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    const auto dot = m_name.find_last_of('.');
    m_extension = dot == std::string::npos ? "nes" : m_name.substr(dot + 1);
    if (dot != std::string::npos) m_name.resize(dot);
    // Content-derived names avoid collisions for similarly named games on different shares.
    uint64_t hash = UINT64_C(14695981039346656037);
    for (uint8_t byte : m_rom) { hash ^= byte; hash *= UINT64_C(1099511628211); }
    char key[32]; std::snprintf(key, sizeof(key), "%016llx", static_cast<unsigned long long>(hash));
    m_save_path = m_directory + "/" + key;
    m_info = {};
    m_info.full_path = m_path.c_str(); m_info.dir = m_content_dir.c_str();
    m_info.name = m_name.c_str(); m_info.ext = m_extension.c_str();
    m_info.data = m_rom.data(); m_info.size = m_rom.size(); m_info.persistent_data = true;
    retro_deinit();retro_init(); // (AI-assisted) Apply the chosen SNES core settings before loading.
    retro_game_info info{m_path.c_str(), m_rom.data(), m_rom.size(), nullptr};
    if (!retro_load_game(&info)) { error = "SNES core could not load this ROM"; m_rom.clear(); return false; }
    m_loaded = true;
    retro_system_av_info av{}; retro_get_system_av_info(&av);
    fps = av.timing.fps; sample_rate = av.timing.sample_rate;
    if (fps < 40 || fps > 75 || sample_rate < 8000 || sample_rate > 192000) {
        error = "Unsupported core timing or audio rate"; retro_unload_game(); m_loaded = false; return false;
    }
    std::string saved, read_error;
    if (!fe::fileio::Read(m_save_path + ".sav", saved, read_error, true)) error = read_error;
    const size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    void* memory = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (memory && saved.size() == size && size) std::memcpy(memory, saved.data(), size);
    else if (!saved.empty()) error = "Battery save size does not match this ROM";
    std::printf("[snes] loaded %s: %.3f fps, %.0f Hz, SRAM %zu\n", path.c_str(), fps, sample_rate, size);
    return true;
}
bool Session::SaveBattery() {
    if (!m_loaded) return true;
    const size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    const auto* memory = static_cast<const char*>(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
    if (!memory || !size) return true;
    return fe::fileio::WriteAtomic(m_save_path + ".sav", std::string(memory, size), error);
}
bool Session::Unload() {
    if (m_loaded) {
        if (!SaveBattery()) return false;
        retro_unload_game(); m_loaded = false;
    }
    m_rom.clear(); pixels.clear(); audio.clear(); buttons[0] = buttons[1] = 0;
    return true;
}
bool Session::SaveState() {
    if (!m_loaded) return false;
    std::string bytes(retro_serialize_size(), '\0');
    if (bytes.empty() || !retro_serialize(bytes.data(), bytes.size())) { error = "Save state failed"; return false; }
    return fe::fileio::WriteAtomic(m_save_path + ".state", bytes, error);
}
bool Session::LoadState() {
    if (!m_loaded) return false;
    std::string bytes;
    if (!fe::fileio::Read(m_save_path + ".state", bytes, error)) return false;
    if (bytes.size() != retro_serialize_size() || !retro_unserialize(bytes.data(), bytes.size())) {
        error = "Incompatible save state"; return false;
    }
    return true;
}
void Session::Run() { if (m_loaded) { audio.clear(); retro_run(); } }
int16_t Session::Input(unsigned port, unsigned device, unsigned, unsigned id) {
    if (port >= 2 || device != RETRO_DEVICE_JOYPAD) return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return static_cast<int16_t>(current->buttons[port]);
    return id < 16 && (current->buttons[port] & (1u << id)) ? 1 : 0;
}
void Session::Sample(int16_t left, int16_t right) { int16_t pair[]{left,right}; Samples(pair,1); }
size_t Session::Samples(const int16_t* data, size_t frames) {
    if (!data || frames > 8192 || current->audio.size() + frames * 2 > 32768) return 0;
    current->audio.insert(current->audio.end(), data, data + frames * 2); return frames;
}
void Session::Video(const void* data, unsigned w, unsigned h, size_t pitch) {
    if (!data) return;
    auto& s = *current;
    const size_t bytes = s.m_format == RETRO_PIXEL_FORMAT_XRGB8888 ? 4 : 2;
    if (!w || !h || w > 512 || h > 576 || pitch < w * bytes) { s.error = "Invalid core video frame"; return; }
    s.width = w; s.height = h; s.pixels.resize(w * h);
    for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
        const auto* at = static_cast<const uint8_t*>(data) + y * pitch + x * bytes;
        uint32_t r,g,b;
        if (bytes == 4) { uint32_t p; std::memcpy(&p,at,4); r=(p>>16)&255;g=(p>>8)&255;b=p&255; }
        else { uint16_t p; std::memcpy(&p,at,2); bool rgb565=s.m_format==RETRO_PIXEL_FORMAT_RGB565;
            r=((p>>(rgb565?11:10))&31)*255/31;g=((p>>5)&(rgb565?63:31))*255/(rgb565?63:31);b=(p&31)*255/31; }
        s.pixels[y*w+x] = 0xff000000u | r | (g<<8) | (b<<16);
    }
}
}
