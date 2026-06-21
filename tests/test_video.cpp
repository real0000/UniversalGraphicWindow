// test_video - headless unit tests for the VideoPlayer API surface.
//
// No display, GPU device, or media file required. Verifies result-string coverage,
// the supported/unsupported flag, and that open() rejects bad arguments with the
// right VideoResult. When built without GStreamer (WINDOW_SUPPORT_VIDEO undefined),
// the stub must report ErrorUnsupported for every open().

#include "../renderer/video/video_renderer.hpp"

#include <cstdio>
#include <string>

using namespace window;
using window::video::VideoResult;
using window::video::VideoPlayer;
using window::video::VideoPlayerDesc;

static int g_pass = 0, g_fail = 0;
static void check(const char* name, bool ok, const std::string& detail = "") {
    std::printf("  [%s] %-32s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    ok ? ++g_pass : ++g_fail;
}

int main() {
    std::printf("VideoPlayer API tests\n");

    // ---- result strings -----------------------------------------------------
    const VideoResult all[] = {
        VideoResult::Success, VideoResult::ErrorUnsupported, VideoResult::ErrorGStreamer,
        VideoResult::ErrorOpenFailed, VideoResult::ErrorNoDevice, VideoResult::ErrorInvalidParameter,
    };
    bool strings_ok = true;
    for (VideoResult r : all) {
        const char* s = video::video_result_to_string(r);
        if (!s || !*s || std::string(s) == "Unknown") strings_ok = false;
    }
    check("result_to_string", strings_ok, "all enum values map to a non-empty name");

    const bool supported = video::video_is_supported();
    std::printf("  (video support compiled in: %s)\n", supported ? "yes" : "no");

    // ---- open() argument validation ----------------------------------------
    // Null path: invalid parameter when supported, unsupported when not.
    {
        VideoResult r = VideoResult::Success;
        VideoPlayer* p = VideoPlayer::open(nullptr, VideoPlayerDesc{}, &r);
        const VideoResult want = supported ? VideoResult::ErrorInvalidParameter : VideoResult::ErrorUnsupported;
        check("open(null path)", p == nullptr && r == want,
              std::string("-> ") + video::video_result_to_string(r));
        if (p) p->destroy();
    }

    // Valid-looking path but no device: ErrorNoDevice when supported, else unsupported.
    {
        VideoResult r = VideoResult::Success;
        VideoPlayerDesc d;            // device left null
        VideoPlayer* p = VideoPlayer::open("videotestsrc://", d, &r);
        const VideoResult want = supported ? VideoResult::ErrorNoDevice : VideoResult::ErrorUnsupported;
        check("open(no device)", p == nullptr && r == want,
              std::string("-> ") + video::video_result_to_string(r));
        if (p) p->destroy();
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
