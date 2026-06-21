#pragma once
// renderer/video/video_renderer.hpp
//
// VideoPlayer -- a GStreamer-backed video decoder that hands the caller a GPU
// TextureHandle to sample each frame, with audio routed through UGW's own
// audio::AudioStream (WASAPI/CoreAudio/PulseAudio/...), not GStreamer's audio sink.
//
// Pipeline: a GStreamer `playbin` (so it accepts local file PATHS and URIs --
// http/rtsp/file -- and auto demuxes + decodes) drives two appsinks:
//   * video appsink -> update() pulls the newest decoded frame and makes it the
//     current GPU texture(); see the zero-copy note below.
//   * audio appsink -> decoded PCM is pushed into a ring buffer drained by a UGW
//     AudioStream callback, so playback uses UGW's audio backend.
//
// Zero-copy: when prefer_zero_copy is set and GStreamer is sharing the SAME GPU
// device as the supplied GraphicDevice, the decoded frame already lives in GPU
// memory (GstGLMemory / GstD3D11Memory / ...). update() then wraps that native
// texture as a TextureHandle via GraphicDevice::import_texture() -- no copy. When
// the decoder lands the frame in system memory instead (no GPU sharing, software
// decode, or a backend without a zero-copy path), update() maps the frame in place
// and does ONE upload into a reused device texture (still no extra CPU copy between
// decode and upload). frame_info.zero_copy reports which path produced the texture.
//
// This whole subsystem only builds when CMake found/built GStreamer (it defines
// WINDOW_SUPPORT_VIDEO). Without it the API still links but every call reports
// VideoResult::ErrorUnsupported, so callers compile and degrade gracefully.

#include "../../graphics_api.hpp"

#include <cstdint>

namespace window {
namespace video {

// ---------------------------------------------------------------------------
// Result codes
// ---------------------------------------------------------------------------
enum class VideoResult {
    Success = 0,
    ErrorUnsupported,        // built without GStreamer (WINDOW_SUPPORT_VIDEO undefined)
    ErrorGStreamer,          // gst_init / element creation failed
    ErrorOpenFailed,         // pipeline failed to reach a playable state
    ErrorNoDevice,           // no GraphicDevice supplied in the desc
    ErrorInvalidParameter,
};

const char* video_result_to_string(VideoResult r);

// True when this build actually links GStreamer (i.e. open() can succeed).
bool video_is_supported();

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
struct VideoPlayerDesc {
    // Required: the device frame textures are created/imported on. Must be the
    // same device the renderer samples with.
    GraphicDevice* device = nullptr;

    bool enable_audio = true;       // route decoded audio to a UGW AudioStream
    bool loop = false;              // restart from the beginning on end-of-stream
    bool prefer_zero_copy = true;   // try GStreamer GPU memory -> import_texture()
    int  audio_device_index = -1;   // UGW output device for audio (-1 = default)

    // Sampled format for the CPU-upload fallback texture. RGBA8 is the safe default
    // (the player negotiates RGBA frames); BGRA8 is also accepted.
    TextureFormat upload_format = TextureFormat::RGBA8_UNORM;
};

// ---------------------------------------------------------------------------
// Per-frame info (what texture() currently holds)
// ---------------------------------------------------------------------------
struct VideoFrameInfo {
    int      width = 0;
    int      height = 0;
    double   pts_seconds = 0.0;     // presentation timestamp of the current frame
    uint64_t frame_index = 0;       // monotonically increasing count of frames shown
    bool     zero_copy = false;     // true = imported GPU texture; false = CPU upload
};

// ---------------------------------------------------------------------------
// VideoPlayer
// ---------------------------------------------------------------------------
class VideoPlayer {
public:
    // Open a video by local path or URI. Returns nullptr and sets *out_result on
    // failure (including ErrorUnsupported when built without GStreamer). The player
    // starts paused -- call play().
    static VideoPlayer* open(const char* uri_or_path, const VideoPlayerDesc& desc,
                             VideoResult* out_result = nullptr);

    // Destroy the player and release all GPU/GStreamer/audio resources.
    void destroy();

    // Pull the newest decoded video frame (if any) and make it the current texture.
    // Call once per render frame, on the thread that owns the GraphicDevice. Returns
    // true if the texture changed this call (a new frame was presented).
    bool update();

    // The GPU texture holding the most-recent frame -- bind/sample this. Invalid
    // (handle.valid() == false) until the first successful update().
    TextureHandle texture() const;

    // Info about the frame currently in texture(). Returns false if no frame yet.
    bool get_frame_info(VideoFrameInfo* out) const;

    // ---- Transport ---------------------------------------------------------
    VideoResult play();
    VideoResult pause();
    VideoResult stop();                 // stop and rewind to the start
    bool        seek(double seconds);   // returns false if the stream isn't seekable

    double duration() const;            // seconds (0 if unknown / live stream)
    double position() const;            // seconds since start
    bool   is_playing() const;
    bool   is_eos() const;              // reached end-of-stream (and not looping)
    int    width() const;
    int    height() const;

    // ---- Audio (forwarded to the UGW AudioStream) --------------------------
    void  set_volume(float v);          // 0..1
    float volume() const;

    struct Impl;   // platform/GStreamer state

private:
    VideoPlayer() = default;
    ~VideoPlayer() = default;
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    Impl* impl_ = nullptr;
};

} // namespace video
} // namespace window
