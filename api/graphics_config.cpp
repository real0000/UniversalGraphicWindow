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
    fprintf(file, "device_name = %s\n", device_name);
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
        fprintf(file, "[window.%s]\n", win.name);
        fprintf(file, "title = %s\n", win.title);
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

            std::string device_name_str = graphics->get<std::string>("device_name", "");
            strncpy(out_config->device_name, device_name_str.c_str(), MAX_DEVICE_NAME_LENGTH - 1);
            out_config->device_name[MAX_DEVICE_NAME_LENGTH - 1] = '\0';

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

                strncpy(win.name, window_name.c_str(), MAX_WINDOW_NAME_LENGTH - 1);
                win.name[MAX_WINDOW_NAME_LENGTH - 1] = '\0';

                std::string title_str = window_tree.get<std::string>("title", "Window");
                strncpy(win.title, title_str.c_str(), MAX_DEVICE_NAME_LENGTH - 1);
                win.title[MAX_DEVICE_NAME_LENGTH - 1] = '\0';

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
            strncpy(out_config->windows[0].name, "main", MAX_WINDOW_NAME_LENGTH - 1);
            strncpy(out_config->windows[0].title, "Window", MAX_DEVICE_NAME_LENGTH - 1);
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
    char window_name[MAX_WINDOW_NAME_LENGTH] = "";
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
                    strncpy(window_name, section + 7, MAX_WINDOW_NAME_LENGTH - 1);
                    window_name[MAX_WINDOW_NAME_LENGTH - 1] = '\0';

                    // Find or create window config
                    current_window_idx = -1;
                    for (int i = 0; i < out_config->window_count; i++) {
                        if (strcmp(out_config->windows[i].name, window_name) == 0) {
                            current_window_idx = i;
                            break;
                        }
                    }
                    if (current_window_idx < 0 && out_config->window_count < MAX_CONFIG_WINDOWS) {
                        current_window_idx = out_config->window_count;
                        strncpy(out_config->windows[current_window_idx].name, window_name, MAX_WINDOW_NAME_LENGTH - 1);
                        out_config->window_count++;
                    }
                } else {
                    current_window_idx = -1;
                    window_name[0] = '\0';
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
                strncpy(out_config->device_name, value, MAX_DEVICE_NAME_LENGTH - 1);
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
                strncpy(win.title, value, MAX_DEVICE_NAME_LENGTH - 1);
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
        strncpy(out_config->windows[0].name, "main", MAX_WINDOW_NAME_LENGTH - 1);
        strncpy(out_config->windows[0].title, "Window", MAX_DEVICE_NAME_LENGTH - 1);
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
    if (device_index >= 0 && device_name[0] != '\0') {
        DeviceEnumeration devices;
        enumerate_devices(backend, &devices);

        bool found = false;
        for (int i = 0; i < devices.device_count; i++) {
            if (strcmp(devices.devices[i].name, device_name) == 0) {
                device_index = devices.devices[i].device_index;
                found = true;
                break;
            }
        }

        if (!found) {
            device_index = -1;
            device_name[0] = '\0';
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
        if (win.name[0] == '\0') {
            snprintf(win.name, MAX_WINDOW_NAME_LENGTH, "window_%d", i);
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
            if (strcmp(windows[i].name, windows[j].name) == 0) {
                char suffix[16];
                snprintf(suffix, sizeof(suffix), "_%d", j);
                size_t len = strlen(windows[j].name);
                size_t suffix_len = strlen(suffix);
                if (len + suffix_len < MAX_WINDOW_NAME_LENGTH) {
                    strcat(windows[j].name, suffix);
                } else {
                    strncpy(windows[j].name + MAX_WINDOW_NAME_LENGTH - suffix_len - 1, suffix, suffix_len + 1);
                }
                all_valid = false;
            }
        }
    }

    return all_valid;
}

WindowConfigEntry* Config::find_window(const char* name) {
    if (!name) return nullptr;
    for (int i = 0; i < window_count; i++) {
        if (strcmp(windows[i].name, name) == 0) {
            return &windows[i];
        }
    }
    return nullptr;
}

const WindowConfigEntry* Config::find_window(const char* name) const {
    if (!name) return nullptr;
    for (int i = 0; i < window_count; i++) {
        if (strcmp(windows[i].name, name) == 0) {
            return &windows[i];
        }
    }
    return nullptr;
}

bool Config::add_window(const WindowConfigEntry& entry) {
    if (window_count >= MAX_CONFIG_WINDOWS) return false;
    if (find_window(entry.name) != nullptr) return false;

    windows[window_count] = entry;
    window_count++;
    return true;
}

bool Config::remove_window(const char* name) {
    if (!name) return false;

    int idx = -1;
    for (int i = 0; i < window_count; i++) {
        if (strcmp(windows[i].name, name) == 0) {
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
// Multi-Window Creation
//=============================================================================

std::vector<Window*> create_windows(const Config& config, Result* out_result) {
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
    Window* first_window = Window::create(first_config, &result);
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

        Window* window = Window::create(win_config, &result);
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

std::vector<Window*> create_windows_from_config(const char* filepath, Result* out_result) {
    Config config;
    if (!Config::load(filepath, &config)) {
        // Use defaults if file not found
        config = Config{};
    }
    return create_windows(config, out_result);
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

} // namespace window
