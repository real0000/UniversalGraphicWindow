/*
 * graphics_config.cpp - Configuration save/load and multi-window creation
 * Uses Boost.PropertyTree for INI format parsing when available
 */

#include "window.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>

#ifdef WINDOW_USE_BOOST_INI
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
namespace pt = boost::property_tree;
#endif

namespace window {

//=============================================================================
// Helper functions for parsing
//=============================================================================

#ifndef WINDOW_USE_BOOST_INI
static void trim_whitespace(char* str) {
    if (!str) return;

    char* start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) {
        start++;
    }

    char* end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }

    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

static bool parse_int(const char* value, int* out) {
    if (!value || !out) return false;
    char* end;
    long val = strtol(value, &end, 10);
    if (end == value || *end != '\0') return false;
    *out = static_cast<int>(val);
    return true;
}

static bool parse_bool(const char* value, bool* out) {
    if (!value || !out) return false;
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 || strcmp(value, "yes") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 || strcmp(value, "no") == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool parse_backend_cstr(const char* value, Backend* out) {
    if (!value || !out) return false;
    if (strcmp(value, "auto") == 0 || strcmp(value, "Auto") == 0) {
        *out = Backend::Auto;
        return true;
    }
    if (strcmp(value, "opengl") == 0 || strcmp(value, "OpenGL") == 0) {
        *out = Backend::OpenGL;
        return true;
    }
    if (strcmp(value, "vulkan") == 0 || strcmp(value, "Vulkan") == 0) {
        *out = Backend::Vulkan;
        return true;
    }
    if (strcmp(value, "d3d11") == 0 || strcmp(value, "D3D11") == 0) {
        *out = Backend::D3D11;
        return true;
    }
    if (strcmp(value, "d3d12") == 0 || strcmp(value, "D3D12") == 0) {
        *out = Backend::D3D12;
        return true;
    }
    if (strcmp(value, "metal") == 0 || strcmp(value, "Metal") == 0) {
        *out = Backend::Metal;
        return true;
    }
    return false;
}

static bool parse_swap_mode_cstr(const char* value, SwapMode* out) {
    if (!value || !out) return false;
    if (strcmp(value, "fifo") == 0 || strcmp(value, "Fifo") == 0 || strcmp(value, "vsync") == 0) {
        *out = SwapMode::Fifo;
        return true;
    }
    if (strcmp(value, "fifo_relaxed") == 0 || strcmp(value, "FifoRelaxed") == 0 || strcmp(value, "adaptive") == 0) {
        *out = SwapMode::FifoRelaxed;
        return true;
    }
    if (strcmp(value, "mailbox") == 0 || strcmp(value, "Mailbox") == 0 || strcmp(value, "triple_buffer") == 0) {
        *out = SwapMode::Mailbox;
        return true;
    }
    if (strcmp(value, "immediate") == 0 || strcmp(value, "Immediate") == 0 || strcmp(value, "no_vsync") == 0) {
        *out = SwapMode::Immediate;
        return true;
    }
    if (strcmp(value, "auto") == 0 || strcmp(value, "Auto") == 0) {
        *out = SwapMode::Auto;
        return true;
    }
    return false;
}
#endif

#ifdef WINDOW_USE_BOOST_INI
static bool parse_backend(const std::string& value, Backend* out) {
    if (!out) return false;
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "auto") {
        *out = Backend::Auto;
        return true;
    }
    if (lower == "opengl") {
        *out = Backend::OpenGL;
        return true;
    }
    if (lower == "vulkan") {
        *out = Backend::Vulkan;
        return true;
    }
    if (lower == "d3d11") {
        *out = Backend::D3D11;
        return true;
    }
    if (lower == "d3d12") {
        *out = Backend::D3D12;
        return true;
    }
    if (lower == "metal") {
        *out = Backend::Metal;
        return true;
    }
    return false;
}

static bool parse_swap_mode_boost(const std::string& value, SwapMode* out) {
    if (!out) return false;
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "fifo" || lower == "vsync") {
        *out = SwapMode::Fifo;
        return true;
    }
    if (lower == "fifo_relaxed" || lower == "adaptive") {
        *out = SwapMode::FifoRelaxed;
        return true;
    }
    if (lower == "mailbox" || lower == "triple_buffer") {
        *out = SwapMode::Mailbox;
        return true;
    }
    if (lower == "immediate" || lower == "no_vsync") {
        *out = SwapMode::Immediate;
        return true;
    }
    if (lower == "auto") {
        *out = SwapMode::Auto;
        return true;
    }
    return false;
}
#endif

static const char* backend_to_config_string(Backend backend) {
    switch (backend) {
        case Backend::Auto: return "auto";
        case Backend::OpenGL: return "opengl";
        case Backend::Vulkan: return "vulkan";
        case Backend::D3D11: return "d3d11";
        case Backend::D3D12: return "d3d12";
        case Backend::Metal: return "metal";
        default: return "auto";
    }
}

static const char* swap_mode_to_config_string(SwapMode mode) {
    switch (mode) {
        case SwapMode::Fifo: return "fifo";
        case SwapMode::FifoRelaxed: return "fifo_relaxed";
        case SwapMode::Mailbox: return "mailbox";
        case SwapMode::Immediate: return "immediate";
        case SwapMode::Auto: return "auto";
        default: return "auto";
    }
}

//=============================================================================
// Public API - SwapMode string conversion
//=============================================================================

const char* swap_mode_to_string(SwapMode mode) {
    switch (mode) {
        case SwapMode::Fifo: return "Fifo";
        case SwapMode::FifoRelaxed: return "FifoRelaxed";
        case SwapMode::Mailbox: return "Mailbox";
        case SwapMode::Immediate: return "Immediate";
        case SwapMode::Auto: return "Auto";
        default: return "Auto";
    }
}

bool parse_swap_mode(const char* value, SwapMode* out) {
    if (!value || !out) return false;

    // Case-insensitive comparison
    auto lower_equal = [](const char* a, const char* b) {
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
            if (ca != cb) return false;
            a++; b++;
        }
        return *a == *b;
    };

    if (lower_equal(value, "fifo") || lower_equal(value, "vsync")) {
        *out = SwapMode::Fifo;
        return true;
    }
    if (lower_equal(value, "fifo_relaxed") || lower_equal(value, "fiforelaxed") || lower_equal(value, "adaptive")) {
        *out = SwapMode::FifoRelaxed;
        return true;
    }
    if (lower_equal(value, "mailbox") || lower_equal(value, "triple_buffer") || lower_equal(value, "triplebuffer")) {
        *out = SwapMode::Mailbox;
        return true;
    }
    if (lower_equal(value, "immediate") || lower_equal(value, "no_vsync") || lower_equal(value, "novsync")) {
        *out = SwapMode::Immediate;
        return true;
    }
    if (lower_equal(value, "auto")) {
        *out = SwapMode::Auto;
        return true;
    }
    return false;
}

//=============================================================================
// Config Implementation
//=============================================================================

bool Config::save(const char* filepath) const {
#ifdef WINDOW_USE_BOOST_INI
    try {
        pt::ptree tree;

        // Graphics section (shared settings)
        tree.put("graphics.backend", backend_to_config_string(backend));
        tree.put("graphics.device_index", device_index);
        tree.put("graphics.device_name", device_name);
        tree.put("graphics.swap_mode", swap_mode_to_config_string(swap_mode));
        tree.put("graphics.vsync", vsync);
        tree.put("graphics.samples", samples);
        tree.put("graphics.back_buffers", back_buffers);
        tree.put("graphics.color_bits", color_bits);
        tree.put("graphics.depth_bits", depth_bits);
        tree.put("graphics.stencil_bits", stencil_bits);

        // Window sections
        for (int i = 0; i < window_count; i++) {
            const WindowConfigEntry& win = windows[i];
            std::string section = std::string("window.") + win.name;

            tree.put(section + ".title", win.title);
            tree.put(section + ".monitor", win.monitor_index);
            tree.put(section + ".x", win.x);
            tree.put(section + ".y", win.y);
            tree.put(section + ".width", win.width);
            tree.put(section + ".height", win.height);
            tree.put(section + ".fullscreen", win.fullscreen);

            char style_str[512];
            window_style_to_string(win.style, style_str, sizeof(style_str));
            tree.put(section + ".style", style_str);
        }

        // Write with custom formatting to produce proper INI file
        std::ofstream file(filepath);
        if (!file) return false;

        file << "# Graphics Configuration File\n";
        file << "# Generated by window library\n\n";

        // Write graphics section
        file << "[graphics]\n";
        file << "backend = " << tree.get<std::string>("graphics.backend") << "\n";
        file << "device_index = " << tree.get<int>("graphics.device_index") << "\n";
        file << "device_name = " << tree.get<std::string>("graphics.device_name") << "\n";
        file << "swap_mode = " << tree.get<std::string>("graphics.swap_mode") << "\n";
        file << "vsync = " << (tree.get<bool>("graphics.vsync") ? "true" : "false") << "\n";
        file << "samples = " << tree.get<int>("graphics.samples") << "\n";
        file << "back_buffers = " << tree.get<int>("graphics.back_buffers") << "\n";
        file << "color_bits = " << tree.get<int>("graphics.color_bits") << "\n";
        file << "depth_bits = " << tree.get<int>("graphics.depth_bits") << "\n";
        file << "stencil_bits = " << tree.get<int>("graphics.stencil_bits") << "\n";
        file << "\n";

        // Write window sections
        for (int i = 0; i < window_count; i++) {
            const WindowConfigEntry& win = windows[i];
            std::string section = std::string("window.") + win.name;

            file << "[" << section << "]\n";
            file << "title = " << tree.get<std::string>(section + ".title") << "\n";
            file << "monitor = " << tree.get<int>(section + ".monitor") << "\n";
            file << "x = " << tree.get<int>(section + ".x") << "\n";
            file << "y = " << tree.get<int>(section + ".y") << "\n";
            file << "width = " << tree.get<int>(section + ".width") << "\n";
            file << "height = " << tree.get<int>(section + ".height") << "\n";
            file << "fullscreen = " << (tree.get<bool>(section + ".fullscreen") ? "true" : "false") << "\n";
            file << "style = " << tree.get<std::string>(section + ".style") << "\n";
            file << "\n";
        }

        return true;
    } catch (const std::exception&) {
        return false;
    }
#else
    FILE* file = fopen(filepath, "w");
    if (!file) return false;

    fprintf(file, "# Graphics Configuration File\n");
    fprintf(file, "# Generated by window library\n\n");

    // Graphics section (shared settings)
    fprintf(file, "[graphics]\n");
    fprintf(file, "backend = %s\n", backend_to_config_string(backend));
    fprintf(file, "device_index = %d\n", device_index);
    fprintf(file, "device_name = %s\n", device_name.c_str());
    fprintf(file, "swap_mode = %s\n", swap_mode_to_config_string(swap_mode));
    fprintf(file, "vsync = %s\n", vsync ? "true" : "false");
    fprintf(file, "samples = %d\n", samples);
    fprintf(file, "back_buffers = %d\n", back_buffers);
    fprintf(file, "color_bits = %d\n", color_bits);
    fprintf(file, "depth_bits = %d\n", depth_bits);
    fprintf(file, "stencil_bits = %d\n", stencil_bits);
    fprintf(file, "\n");

    // Window sections
    for (int i = 0; i < window_count; i++) {
        const WindowConfigEntry& win = windows[i];
        fprintf(file, "[window.%s]\n", win.name.c_str());
        fprintf(file, "title = %s\n", win.title.c_str());
        fprintf(file, "monitor = %d\n", win.monitor_index);
        fprintf(file, "x = %d\n", win.x);
        fprintf(file, "y = %d\n", win.y);
        fprintf(file, "width = %d\n", win.width);
        fprintf(file, "height = %d\n", win.height);
        fprintf(file, "fullscreen = %s\n", win.fullscreen ? "true" : "false");

        char style_str[512];
        window_style_to_string(win.style, style_str, sizeof(style_str));
        fprintf(file, "style = %s\n", style_str);
        fprintf(file, "\n");
    }

    fclose(file);
    return true;
#endif
}

bool Config::load(const char* filepath, Config* out_config) {
    if (!out_config) return false;

#ifdef WINDOW_USE_BOOST_INI
    try {
        pt::ptree tree;
        pt::read_ini(filepath, tree);

        // Initialize with defaults
        *out_config = Config{};
        out_config->window_count = 0;

        // Parse graphics section
        if (auto graphics = tree.get_child_optional("graphics")) {
            std::string backend_str = graphics->get<std::string>("backend", "auto");
            parse_backend(backend_str, &out_config->backend);

            out_config->device_index = graphics->get<int>("device_index", -1);

            out_config->device_name = graphics->get<std::string>("device_name", "");

            std::string swap_mode_str = graphics->get<std::string>("swap_mode", "auto");
            parse_swap_mode_boost(swap_mode_str, &out_config->swap_mode);

            out_config->vsync = graphics->get<bool>("vsync", true);
            out_config->samples = graphics->get<int>("samples", 1);
            out_config->back_buffers = graphics->get<int>("back_buffers", 2);
            out_config->color_bits = graphics->get<int>("color_bits", 32);
            out_config->depth_bits = graphics->get<int>("depth_bits", 24);
            out_config->stencil_bits = graphics->get<int>("stencil_bits", 8);
        }

        // Parse window sections (sections starting with "window.")
        for (const auto& section : tree) {
            const std::string& section_name = section.first;
            if (section_name.substr(0, 7) == "window." && out_config->window_count < MAX_CONFIG_WINDOWS) {
                std::string window_name = section_name.substr(7);
                const pt::ptree& window_tree = section.second;

                WindowConfigEntry& win = out_config->windows[out_config->window_count];

                win.name = window_name;
                win.title = window_tree.get<std::string>("title", "Window");

                win.monitor_index = window_tree.get<int>("monitor", 0);
                win.x = window_tree.get<int>("x", -1);
                win.y = window_tree.get<int>("y", -1);
                win.width = window_tree.get<int>("width", 800);
                win.height = window_tree.get<int>("height", 600);
                win.fullscreen = window_tree.get<bool>("fullscreen", false);

                std::string style_str = window_tree.get<std::string>("style", "default");
                parse_window_style(style_str.c_str(), &win.style);

                out_config->window_count++;
            }
        }

        // Ensure at least one window exists
        if (out_config->window_count == 0) {
            out_config->window_count = 1;
            out_config->windows[0].name = "main";
            out_config->windows[0].title = "Window";
        }

        // Validate after loading
        out_config->validate();

        return true;
    } catch (const pt::ini_parser_error&) {
        return false;
    } catch (const std::exception&) {
        return false;
    }
#else
    FILE* file = fopen(filepath, "r");
    if (!file) return false;

    // Initialize with defaults
    *out_config = Config{};
    out_config->window_count = 0;  // Will be populated from file

    char line[1024];
    char section[128] = "";
    std::string window_name;
    int current_window_idx = -1;

    while (fgets(line, sizeof(line), file)) {
        trim_whitespace(line);

        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // Section header
        if (line[0] == '[') {
            char* end = strchr(line, ']');
            if (end) {
                *end = '\0';
                strncpy(section, line + 1, sizeof(section) - 1);
                section[sizeof(section) - 1] = '\0';

                // Check for window section: [window.name]
                if (strncmp(section, "window.", 7) == 0) {
                    window_name = section + 7;

                    // Find or create window config
                    current_window_idx = -1;
                    for (int i = 0; i < out_config->window_count; i++) {
                        if (out_config->windows[i].name == window_name) {
                            current_window_idx = i;
                            break;
                        }
                    }
                    if (current_window_idx < 0 && out_config->window_count < MAX_CONFIG_WINDOWS) {
                        current_window_idx = out_config->window_count;
                        out_config->windows[current_window_idx].name = window_name;
                        out_config->window_count++;
                    }
                } else {
                    current_window_idx = -1;
                    window_name.clear();
                }
            }
            continue;
        }

        // Key = Value
        char* eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char* key = line;
        char* value = eq + 1;
        trim_whitespace(key);
        trim_whitespace(value);

        // Parse graphics section
        if (strcmp(section, "graphics") == 0) {
            if (strcmp(key, "backend") == 0) {
                parse_backend_cstr(value, &out_config->backend);
            } else if (strcmp(key, "device_index") == 0) {
                parse_int(value, &out_config->device_index);
            } else if (strcmp(key, "device_name") == 0) {
                out_config->device_name = value;
            } else if (strcmp(key, "swap_mode") == 0) {
                parse_swap_mode_cstr(value, &out_config->swap_mode);
            } else if (strcmp(key, "vsync") == 0) {
                parse_bool(value, &out_config->vsync);
            } else if (strcmp(key, "samples") == 0) {
                parse_int(value, &out_config->samples);
            } else if (strcmp(key, "back_buffers") == 0) {
                parse_int(value, &out_config->back_buffers);
            } else if (strcmp(key, "color_bits") == 0) {
                parse_int(value, &out_config->color_bits);
            } else if (strcmp(key, "depth_bits") == 0) {
                parse_int(value, &out_config->depth_bits);
            } else if (strcmp(key, "stencil_bits") == 0) {
                parse_int(value, &out_config->stencil_bits);
            }
        }
        // Parse window section
        else if (current_window_idx >= 0 && current_window_idx < out_config->window_count) {
            WindowConfigEntry& win = out_config->windows[current_window_idx];
            if (strcmp(key, "title") == 0) {
                win.title = value;
            } else if (strcmp(key, "monitor") == 0) {
                parse_int(value, &win.monitor_index);
            } else if (strcmp(key, "x") == 0) {
                parse_int(value, &win.x);
            } else if (strcmp(key, "y") == 0) {
                parse_int(value, &win.y);
            } else if (strcmp(key, "width") == 0) {
                parse_int(value, &win.width);
            } else if (strcmp(key, "height") == 0) {
                parse_int(value, &win.height);
            } else if (strcmp(key, "fullscreen") == 0) {
                parse_bool(value, &win.fullscreen);
            } else if (strcmp(key, "style") == 0) {
                parse_window_style(value, &win.style);
            }
        }
    }

    fclose(file);

    // Ensure at least one window exists
    if (out_config->window_count == 0) {
        out_config->window_count = 1;
        out_config->windows[0].name = "main";
        out_config->windows[0].title = "Window";
    }

    // Validate after loading
    out_config->validate();

    return true;
#endif
}

bool Config::validate() {
    bool all_valid = true;

    // Validate samples (must be power of 2, max 16)
    if (samples < 1 || samples > 16 || (samples & (samples - 1)) != 0) {
        samples = 1;
        all_valid = false;
    }

    // Validate back buffers
    if (back_buffers < 1 || back_buffers > 4) {
        back_buffers = 2;
        all_valid = false;
    }

    // Validate color bits
    if (color_bits != 16 && color_bits != 24 && color_bits != 32) {
        color_bits = 32;
        all_valid = false;
    }

    // Validate depth bits
    if (depth_bits != 0 && depth_bits != 16 && depth_bits != 24 && depth_bits != 32) {
        depth_bits = 24;
        all_valid = false;
    }

    // Validate stencil bits
    if (stencil_bits != 0 && stencil_bits != 8) {
        stencil_bits = 8;
        all_valid = false;
    }

    // Validate backend
    if (!is_backend_supported(backend)) {
        backend = Backend::Auto;
        all_valid = false;
    }

    // Validate device exists (if specified)
    if (device_index >= 0 && !device_name.empty()) {
        DeviceEnumeration devices;
        enumerate_devices(backend, &devices);

        bool found = false;
        for (int i = 0; i < devices.device_count; i++) {
            if (devices.devices[i].name == device_name) {
                device_index = devices.devices[i].device_index;
                found = true;
                break;
            }
        }

        if (!found) {
            device_index = -1;
            device_name.clear();
            all_valid = false;
        }
    }

    // Enumerate monitors for window validation
    MonitorEnumeration monitors;
    enumerate_monitors(&monitors);

    // Validate each window
    for (int i = 0; i < window_count; i++) {
        WindowConfigEntry& win = windows[i];

        // Validate name
        if (win.name.empty()) {
            win.name = "window_" + std::to_string(i);
            all_valid = false;
        }

        // Validate window size
        if (win.width < 1) {
            win.width = 800;
            all_valid = false;
        }
        if (win.height < 1) {
            win.height = 600;
            all_valid = false;
        }

        // Validate monitor
        if (win.monitor_index >= 0 && win.monitor_index >= monitors.monitor_count) {
            win.monitor_index = 0;
            all_valid = false;
        }
    }

    // Check for duplicate window names
    for (int i = 0; i < window_count; i++) {
        for (int j = i + 1; j < window_count; j++) {
            if (windows[i].name == windows[j].name) {
                windows[j].name += "_" + std::to_string(j);
                all_valid = false;
            }
        }
    }

    return all_valid;
}

WindowConfigEntry* Config::find_window(const char* name) {
    if (!name) return nullptr;
    for (int i = 0; i < window_count; i++) {
        if (windows[i].name == name) {
            return &windows[i];
        }
    }
    return nullptr;
}

const WindowConfigEntry* Config::find_window(const char* name) const {
    if (!name) return nullptr;
    for (int i = 0; i < window_count; i++) {
        if (windows[i].name == name) {
            return &windows[i];
        }
    }
    return nullptr;
}

bool Config::add_window(const WindowConfigEntry& entry) {
    if (window_count >= MAX_CONFIG_WINDOWS) return false;
    if (find_window(entry.name.c_str()) != nullptr) return false;

    windows[window_count] = entry;
    window_count++;
    return true;
}

bool Config::remove_window(const char* name) {
    if (!name) return false;

    int idx = -1;
    for (int i = 0; i < window_count; i++) {
        if (windows[i].name == name) {
            idx = i;
            break;
        }
    }

    if (idx < 0) return false;

    // Shift remaining windows
    for (int i = idx; i < window_count - 1; i++) {
        windows[i] = windows[i + 1];
    }
    window_count--;
    return true;
}

//=============================================================================
// Helper functions
//=============================================================================

bool find_display_mode(const MonitorInfo& monitor, int width, int height, int refresh_rate, DisplayMode* out_mode) {
    if (!out_mode) return false;

    for (int i = 0; i < monitor.mode_count; i++) {
        const DisplayMode& mode = monitor.modes[i];
        if (mode.width == width && mode.height == height) {
            if (refresh_rate == 0 || mode.refresh_rate == refresh_rate) {
                *out_mode = mode;
                return true;
            }
        }
    }

    if (refresh_rate != 0) {
        for (int i = 0; i < monitor.mode_count; i++) {
            const DisplayMode& mode = monitor.modes[i];
            if (mode.width == width && mode.height == height) {
                *out_mode = mode;
                return true;
            }
        }
    }

    return false;
}

bool get_primary_monitor(MonitorInfo* out_monitor) {
    if (!out_monitor) return false;

    MonitorEnumeration monitors;
    enumerate_monitors(&monitors);

    for (int i = 0; i < monitors.monitor_count; i++) {
        if (monitors.monitors[i].is_primary) {
            *out_monitor = monitors.monitors[i];
            return true;
        }
    }

    if (monitors.monitor_count > 0) {
        *out_monitor = monitors.monitors[0];
        return true;
    }

    return false;
}

//=============================================================================
// Window::create and Window::create_from_config
// Multi-window creation implementation
//=============================================================================

std::vector<Window*> Window::create(const Config& config, Result* out_result) {
    auto set_result = [&](Result r) { if (out_result) *out_result = r; };

    std::vector<Window*> result_windows;

    if (config.window_count == 0) {
        set_result(Result::ErrorInvalidParameter);
        return result_windows;
    }

    // Create the first window with a new graphics context
    Config first_config = config;
    first_config.shared_graphics = nullptr;

    Result result;
    Window* first_window = create_window_impl(first_config, &result);
    if (!first_window) {
        set_result(result);
        return result_windows;
    }

    result_windows.push_back(first_window);
    Graphics* shared_gfx = first_window->graphics();

    // Create remaining windows with shared graphics context
    for (int i = 1; i < config.window_count; i++) {
        Config win_config = config;
        // Copy window-specific settings
        win_config.windows[0] = config.windows[i];
        win_config.window_count = 1;
        win_config.shared_graphics = shared_gfx;

        Window* window = create_window_impl(win_config, &result);
        if (!window) {
            // Clean up already created windows on failure
            for (Window* w : result_windows) {
                w->destroy();
            }
            result_windows.clear();
            set_result(result);
            return result_windows;
        }

        result_windows.push_back(window);
    }

    set_result(Result::Success);
    return result_windows;
}

std::vector<Window*> Window::create_from_config(const char* filepath, Result* out_result) {
    Config config;
    if (!Config::load(filepath, &config)) {
        // Use defaults if file not found
        config = Config{};
    }
    return Window::create(config, out_result);
}

} // namespace window
