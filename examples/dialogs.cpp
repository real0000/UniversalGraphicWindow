/*
 * dialogs.cpp - Common dialog demo (file open/save, folder, color, font)
 *
 * Demonstrates the cross-platform common-dialog API. A native OS dialog is
 * used where one exists; otherwise the library draws its own window. The same
 * code runs on every platform.
 *
 * Run with no arguments for an interactive walk-through, or pass a dialog name
 * (open|save|folder|color|font) to show just one.
 */

#include "window.hpp"
#include <cstdio>
#include <cstring>
#include <string>

using namespace window;

static void print_file_result(const char* what, const FileDialogResult& r) {
    if (!r.ok) {
        std::printf("[%s] cancelled\n", what);
        return;
    }
    std::printf("[%s] filter=%d, %zu path(s):\n", what, r.filter_index, r.paths.size());
    for (const auto& p : r.paths) std::printf("    %s\n", p.c_str());
}

static void demo_open() {
    FileDialogOptions o;
    o.title = "Open an image or text file";
    o.allow_multiple = true;
    o.filters = {
        {"Image Files", "png;jpg;jpeg;bmp;gif"},
        {"Text Files",  "txt;md;log"},
        {"All Files",   "*"},
    };
    print_file_result("open", Window::show_open_file_dialog(o));
}

static void demo_save() {
    FileDialogOptions o;
    o.title = "Save document as";
    o.initial_name = "untitled.txt";
    o.filters = {
        {"Text Files", "txt"},
        {"All Files",  "*"},
    };
    print_file_result("save", Window::show_save_file_dialog(o));
}

static void demo_folder() {
    FileDialogOptions o;
    o.title = "Pick a folder";
    print_file_result("folder", Window::show_folder_dialog(o));
}

static void demo_color() {
    ColorDialogOptions o;
    o.title = "Pick a color";
    o.initial = {64, 128, 220, 255};
    o.allow_alpha = true;
    ColorDialogResult r = Window::show_color_dialog(o);
    if (r.ok)
        std::printf("[color] rgba(%u, %u, %u, %u)\n", r.color.r, r.color.g, r.color.b, r.color.a);
    else
        std::printf("[color] cancelled\n");
}

static void demo_font() {
    FontDialogOptions o;
    o.title = "Pick a font";
    o.initial.family = "Arial";
    o.initial.size_pt = 14.0f;
    o.allow_color = true;
    FontDialogResult r = Window::show_font_dialog(o);
    if (r.ok)
        std::printf("[font] %s %.1fpt%s%s%s%s\n",
            r.font.family.c_str(), r.font.size_pt,
            r.font.bold ? " bold" : "", r.font.italic ? " italic" : "",
            r.font.underline ? " underline" : "", r.font.strikeout ? " strikeout" : "");
    else
        std::printf("[font] cancelled\n");
}

int main(int argc, char** argv) {
    if (argc > 1) {
        const char* which = argv[1];
        if      (!std::strcmp(which, "open"))   demo_open();
        else if (!std::strcmp(which, "save"))   demo_save();
        else if (!std::strcmp(which, "folder")) demo_folder();
        else if (!std::strcmp(which, "color"))  demo_color();
        else if (!std::strcmp(which, "font"))   demo_font();
        else std::printf("usage: %s [open|save|folder|color|font]\n", argv[0]);
        return 0;
    }

    std::printf("Common dialog demo - a native dialog is used where the OS\n"
                "provides one, otherwise the library draws its own window.\n\n");
    demo_open();
    demo_save();
    demo_folder();
    demo_color();
    demo_font();
    return 0;
}
