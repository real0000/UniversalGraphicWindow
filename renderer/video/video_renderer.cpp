// renderer/video/video_renderer.cpp
//
// GStreamer-backed VideoPlayer. This translation unit self-guards: it is always
// compiled (so the API always links), but the real implementation only activates
// when CMake found/built GStreamer and defined WINDOW_SUPPORT_VIDEO. Without it a
// stub reports VideoResult::ErrorUnsupported.
//
// The GStreamer GPU-memory zero-copy fast paths (D3D11 / OpenGL) additionally
// require their plugin dev headers and are gated by WINDOW_SUPPORT_VIDEO_D3D11 /
// WINDOW_SUPPORT_VIDEO_GL; when absent, decoding still works through the system-
// memory CPU-upload path (one upload into a reused device texture, no extra copy).

#include "video_renderer.hpp"

namespace window {
namespace video {

const char* video_result_to_string(VideoResult r) {
    switch (r) {
        case VideoResult::Success:             return "Success";
        case VideoResult::ErrorUnsupported:    return "ErrorUnsupported";
        case VideoResult::ErrorGStreamer:      return "ErrorGStreamer";
        case VideoResult::ErrorOpenFailed:     return "ErrorOpenFailed";
        case VideoResult::ErrorNoDevice:       return "ErrorNoDevice";
        case VideoResult::ErrorInvalidParameter: return "ErrorInvalidParameter";
    }
    return "Unknown";
}

bool video_is_supported() {
#ifdef WINDOW_SUPPORT_VIDEO
    return true;
#else
    return false;
#endif
}

} // namespace video
} // namespace window

#ifndef WINDOW_SUPPORT_VIDEO
// ===========================================================================
// Stub build (no GStreamer) -- every entry point degrades gracefully.
// ===========================================================================
namespace window {
namespace video {

struct VideoPlayer::Impl {};

VideoPlayer* VideoPlayer::open(const char*, const VideoPlayerDesc&, VideoResult* out_result) {
    if (out_result) *out_result = VideoResult::ErrorUnsupported;
    return nullptr;
}
void          VideoPlayer::destroy()                         { delete impl_; impl_ = nullptr; }
bool          VideoPlayer::update()                          { return false; }
TextureHandle VideoPlayer::texture() const                  { return TextureHandle{}; }
bool          VideoPlayer::get_frame_info(VideoFrameInfo*) const { return false; }
VideoResult   VideoPlayer::play()                            { return VideoResult::ErrorUnsupported; }
VideoResult   VideoPlayer::pause()                           { return VideoResult::ErrorUnsupported; }
VideoResult   VideoPlayer::stop()                            { return VideoResult::ErrorUnsupported; }
bool          VideoPlayer::seek(double)                      { return false; }
double        VideoPlayer::duration() const                 { return 0.0; }
double        VideoPlayer::position() const                 { return 0.0; }
bool          VideoPlayer::is_playing() const               { return false; }
bool          VideoPlayer::is_eos() const                   { return false; }
int           VideoPlayer::width() const                    { return 0; }
int           VideoPlayer::height() const                   { return 0; }
void          VideoPlayer::set_volume(float)                {}
float         VideoPlayer::volume() const                   { return 0.0f; }

} // namespace video
} // namespace window

#else  // WINDOW_SUPPORT_VIDEO
// ===========================================================================
// Real GStreamer implementation.
// ===========================================================================
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>

#if defined(WINDOW_SUPPORT_VIDEO_D3D11)
#define GST_USE_UNSTABLE_API           // gst-plugins-bad d3d11 is "unstable API"; opt in to silence the warning
#include <d3d11.h>
#include <gst/d3d11/gstd3d11.h>
#endif
#if defined(WINDOW_SUPPORT_VIDEO_GL)
#include <gst/gl/gst.h>
#include <gst/gl/gstglmemory.h>
#endif

#if defined(WINDOW_SUPPORT_AUDIO)
#include "../../audio/audio.hpp"
#endif

namespace window {
namespace video {

namespace {

constexpr uint32_t kGL_TEXTURE_2D = 0x0DE1;   // avoid pulling a GL header just for this

void ensure_gst_init() {
    static std::once_flag once;
    std::call_once(once, [] { gst_init(nullptr, nullptr); });
}

// appsink caps: prefer the backend's GPU-memory feature (zero-copy) when its
// plugin path is compiled in, but always accept system-memory RGBA/BGRA so
// negotiation never hard-fails -- update() picks the path per sample.
std::string video_caps_string(Backend backend, bool zero_copy) {
    const char* sys = "video/x-raw, format=(string){ RGBA, BGRA }";
    const char* feature = nullptr;
    if (zero_copy) {
#if defined(WINDOW_SUPPORT_VIDEO_D3D11)
        if (backend == Backend::D3D11) feature = "memory:D3D11Memory";
#endif
#if defined(WINDOW_SUPPORT_VIDEO_GL)
        if (backend == Backend::OpenGL) feature = "memory:GLMemory";
#endif
    }
    if (!feature) return sys;
    std::string gpu = std::string("video/x-raw(") + feature + "), format=(string){ RGBA, BGRA }";
    return gpu + "; " + sys;   // GPU memory preferred, system memory fallback
}

} // namespace

// ---------------------------------------------------------------------------
// Audio bridge: GStreamer's audio appsink pushes interleaved float PCM into a
// ring buffer; a UGW AudioStream callback drains it -> playback uses UGW audio.
// ---------------------------------------------------------------------------
#if defined(WINDOW_SUPPORT_AUDIO)
class AudioBridge : public window::audio::IAudioCallback {
public:
    void push(const float* samples, size_t count) {
        std::lock_guard<std::mutex> lk(mutex_);
        data_.insert(data_.end(), samples, samples + count);
    }
    void set_volume(float v) { volume_ = v < 0.0f ? 0.0f : v; }
    float volume() const { return volume_; }
    void clear() { std::lock_guard<std::mutex> lk(mutex_); data_.clear(); head_ = 0; }

    bool on_audio_playback(window::audio::AudioBuffer& out,
                           const window::audio::AudioStreamTime&) override {
        float* dst = static_cast<float*>(out.data);
        if (!dst) return true;
        const int need = out.frame_count * out.channel_count;
        const float vol = volume_;
        std::lock_guard<std::mutex> lk(mutex_);
        const int avail = static_cast<int>(data_.size() - head_);
        const int n = need < avail ? need : avail;
        for (int i = 0; i < n; ++i)    dst[i] = data_[head_ + i] * vol;
        for (int i = n; i < need; ++i) dst[i] = 0.0f;     // underrun -> silence
        head_ += n;
        // Periodically compact the consumed front so the buffer doesn't grow forever.
        if (head_ > 48000u * 8u) { data_.erase(data_.begin(), data_.begin() + head_); head_ = 0; }
        return true;
    }

private:
    std::mutex          mutex_;
    std::vector<float>  data_;
    size_t              head_ = 0;
    float               volume_ = 1.0f;
};
#endif // WINDOW_SUPPORT_AUDIO

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct VideoPlayer::Impl {
    VideoPlayerDesc desc;
    GraphicDevice*  device  = nullptr;
    Backend         backend = Backend::OpenGL;

    GstElement* pipeline   = nullptr;   // playbin
    GstElement* video_sink = nullptr;   // appsink
    GstElement* audio_sink = nullptr;   // appsink (optional)
    GstBus*     bus        = nullptr;

    // Current presented texture.
    TextureHandle  tex;                 // what texture() returns
    bool           tex_zero_copy = false;
    GstSample*     held_sample = nullptr;  // keeps a zero-copy frame's native resource alive

    // Reused CPU-upload texture (created lazily, resized on demand).
    TextureHandle  cpu_tex;
    int            cpu_w = 0, cpu_h = 0;
    std::vector<uint8_t> scratch;       // row-repack staging when stride != tight

    VideoFrameInfo frame_info;
    bool           have_frame = false;
    bool           eos = false;
    bool           playing = false;
    int            vid_w = 0, vid_h = 0;

#if defined(WINDOW_SUPPORT_AUDIO)
    AudioBridge               audio_bridge;
    window::audio::AudioStream* audio_stream = nullptr;
#endif

    // ---- frame texture management ------------------------------------------
    void release_current_imported() {
        // Drop a previously imported (zero-copy) texture + its keep-alive sample.
        // The reused cpu_tex is never released here (only in shutdown()).
        if (tex_zero_copy && tex.valid()) device->destroy_texture(tex);
        if (held_sample) { gst_sample_unref(held_sample); held_sample = nullptr; }
        tex = TextureHandle{};
        tex_zero_copy = false;
    }

    void ensure_cpu_texture(int w, int h) {
        if (cpu_tex.valid() && cpu_w == w && cpu_h == h) return;
        if (cpu_tex.valid()) device->destroy_texture(cpu_tex);
        TextureDesc td;
        td.width = w; td.height = h;
        td.format = desc.upload_format;
        td.usage  = TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_COPY_DST;
        td.debug_name = "video_frame";
        cpu_tex = device->create_texture(td);
        cpu_w = w; cpu_h = h;
    }

    void upload_cpu(int w, int h, const uint8_t* src, int stride) {
        ensure_cpu_texture(w, h);
        if (!cpu_tex.valid()) return;
        const int tight = w * 4;
        const uint8_t* data = src;
        if (stride != tight) {
            scratch.resize(static_cast<size_t>(tight) * h);
            for (int y = 0; y < h; ++y)
                std::memcpy(scratch.data() + static_cast<size_t>(y) * tight,
                            src + static_cast<size_t>(y) * stride, tight);
            data = scratch.data();
        }
        TextureRegion r; r.width = w; r.height = h;
        device->update_texture(cpu_tex, r, data);
        release_current_imported();
        tex = cpu_tex;
        tex_zero_copy = false;
    }

    // Try to wrap the sample's GPU memory directly (zero-copy). Returns true on success.
    bool try_import_zero_copy(GstSample* sample, GstBuffer* buf, const GstVideoInfo* vinfo) {
        if (!desc.prefer_zero_copy) return false;
        TextureHandle imported{};
        const int w = GST_VIDEO_INFO_WIDTH(vinfo);
        const int h = GST_VIDEO_INFO_HEIGHT(vinfo);
#if defined(WINDOW_SUPPORT_VIDEO_D3D11)
        if (backend == Backend::D3D11) {
            GstMemory* mem = gst_buffer_peek_memory(buf, 0);
            if (mem && gst_is_d3d11_memory(mem)) {
                auto* dmem = reinterpret_cast<GstD3D11Memory*>(mem);
                auto* native = reinterpret_cast<ID3D11Texture2D*>(gst_d3d11_memory_get_resource_handle(dmem));
                if (native) {
                    NativeTextureDesc nd;
                    nd.d3d_resource = native; nd.width = w; nd.height = h;
                    nd.format = TextureFormat::RGBA8_UNORM;
                    imported = device->import_texture(nd);
                }
            }
        }
#endif
#if defined(WINDOW_SUPPORT_VIDEO_GL)
        if (backend == Backend::OpenGL) {
            GstMemory* mem = gst_buffer_peek_memory(buf, 0);
            if (mem && gst_is_gl_memory(mem)) {
                auto* gmem = reinterpret_cast<GstGLMemory*>(mem);
                const guint texid = gst_gl_memory_get_texture_id(gmem);
                if (texid) {
                    NativeTextureDesc nd;
                    nd.gl_texture = texid; nd.gl_target = kGL_TEXTURE_2D;
                    nd.width = w; nd.height = h; nd.format = TextureFormat::RGBA8_UNORM;
                    imported = device->import_texture(nd);
                }
            }
        }
#endif
        (void)buf;
        if (!imported.valid()) return false;
        release_current_imported();
        tex = imported;
        tex_zero_copy = true;
        held_sample = gst_sample_ref(sample);   // keep the native resource alive
        return true;
    }

    // ---- bus ----------------------------------------------------------------
    void poll_bus() {
        if (!bus) return;
        GstMessage* msg;
        while ((msg = gst_bus_pop_filtered(
                    bus, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR))) != nullptr) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
                if (desc.loop) {
                    gst_element_seek_simple(pipeline, GST_FORMAT_TIME,
                        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), 0);
                } else {
                    eos = true;
                }
            } else {  // GST_MESSAGE_ERROR
                GError* err = nullptr; gchar* dbg = nullptr;
                gst_message_parse_error(msg, &err, &dbg);
                if (err) g_error_free(err);
                if (dbg) g_free(dbg);
                eos = true;
            }
            gst_message_unref(msg);
        }
    }

    void shutdown() {
        if (pipeline) gst_element_set_state(pipeline, GST_STATE_NULL);
#if defined(WINDOW_SUPPORT_AUDIO)
        if (audio_stream) { audio_stream->stop(); audio_stream->destroy(); audio_stream = nullptr; }
#endif
        release_current_imported();
        if (cpu_tex.valid() && device) { device->destroy_texture(cpu_tex); cpu_tex = TextureHandle{}; }
        if (bus)        { gst_object_unref(bus);        bus = nullptr; }
        if (video_sink) { gst_object_unref(video_sink); video_sink = nullptr; }
        if (audio_sink) { gst_object_unref(audio_sink); audio_sink = nullptr; }
        if (pipeline)   { gst_object_unref(pipeline);   pipeline = nullptr; }
    }
};

#if defined(WINDOW_SUPPORT_AUDIO)
// GStreamer streaming-thread callback: push decoded float PCM into the ring.
static GstFlowReturn on_audio_new_sample(GstAppSink* sink, gpointer user) {
    auto* impl = static_cast<VideoPlayer::Impl*>(user);
    GstSample* s = gst_app_sink_pull_sample(sink);
    if (!s) return GST_FLOW_OK;
    if (GstBuffer* b = gst_sample_get_buffer(s)) {
        GstMapInfo mi;
        if (gst_buffer_map(b, &mi, GST_MAP_READ)) {
            impl->audio_bridge.push(reinterpret_cast<const float*>(mi.data), mi.size / sizeof(float));
            gst_buffer_unmap(b, &mi);
        }
    }
    gst_sample_unref(s);
    return GST_FLOW_OK;
}
#endif

// ---------------------------------------------------------------------------
// VideoPlayer
// ---------------------------------------------------------------------------
VideoPlayer* VideoPlayer::open(const char* uri_or_path, const VideoPlayerDesc& desc,
                               VideoResult* out_result) {
    auto fail = [&](VideoResult r) -> VideoPlayer* { if (out_result) *out_result = r; return nullptr; };
    if (!uri_or_path || !*uri_or_path) return fail(VideoResult::ErrorInvalidParameter);
    if (!desc.device)                  return fail(VideoResult::ErrorNoDevice);

    ensure_gst_init();

    auto* impl = new VideoPlayer::Impl();
    impl->desc    = desc;
    impl->device  = desc.device;
    impl->backend = desc.device->get_backend();

    // "test"/"videotestsrc://" plays a synthetic SMPTE pattern (+ tone) via a manual
    // pipeline, so the decode->texture->present path runs with no media file. Anything
    // else goes through playbin (local file paths and real URIs).
    const bool is_test = (std::strcmp(uri_or_path, "test") == 0 ||
                          std::strcmp(uri_or_path, "videotestsrc://") == 0 ||
                          std::strncmp(uri_or_path, "test://", 7) == 0);

    GstElement* pipeline = nullptr;
    GstElement* vsink = nullptr;   // owned ref -> impl->video_sink
    GstElement* asink = nullptr;   // owned ref -> impl->audio_sink (or null)

    if (is_test) {
        std::string adesc;
#if defined(WINDOW_SUPPORT_AUDIO)
        if (desc.enable_audio)
            adesc = " audiotestsrc is-live=true ! audioconvert ! audioresample ! "
                    "audio/x-raw,format=F32LE,layout=interleaved,channels=2,rate=48000 ! "
                    "appsink name=asink max-buffers=8 drop=false sync=true emit-signals=true";
#endif
        std::string ldesc =
            "videotestsrc is-live=true pattern=smpte ! videoconvert ! video/x-raw,format=RGBA ! "
            "appsink name=vsink max-buffers=1 drop=true sync=true" + adesc;
        GError* perr = nullptr;
        pipeline = gst_parse_launch(ldesc.c_str(), &perr);
        if (perr) g_error_free(perr);
        if (!pipeline) { impl->shutdown(); delete impl; return fail(VideoResult::ErrorGStreamer); }
        vsink = gst_bin_get_by_name(GST_BIN(pipeline), "vsink");      // returns an owned ref
        asink = gst_bin_get_by_name(GST_BIN(pipeline), "asink");      // null if absent
        if (!vsink) { gst_object_unref(pipeline); impl->shutdown(); delete impl; return fail(VideoResult::ErrorGStreamer); }
    } else {
        pipeline = gst_element_factory_make("playbin", "ugw_player");
        if (!pipeline) { impl->shutdown(); delete impl; return fail(VideoResult::ErrorGStreamer); }
        gchar* uri = gst_uri_is_valid(uri_or_path) ? g_strdup(uri_or_path)
                                                   : gst_filename_to_uri(uri_or_path, nullptr);
        if (!uri) { gst_object_unref(pipeline); impl->shutdown(); delete impl; return fail(VideoResult::ErrorInvalidParameter); }
        g_object_set(pipeline, "uri", uri, nullptr);
        g_free(uri);

        vsink = gst_element_factory_make("appsink", "ugw_vsink");
        if (!vsink) { gst_object_unref(pipeline); impl->shutdown(); delete impl; return fail(VideoResult::ErrorGStreamer); }
        std::string caps_str = video_caps_string(impl->backend, desc.prefer_zero_copy);
        GstCaps* caps = gst_caps_from_string(caps_str.c_str());
        g_object_set(vsink, "caps", caps, "max-buffers", 1, "drop", TRUE, "sync", TRUE, "emit-signals", FALSE, nullptr);
        gst_caps_unref(caps);
        gst_object_ref(vsink);                            // keep our own ref past the set into playbin
        g_object_set(pipeline, "video-sink", vsink, nullptr);
#if defined(WINDOW_SUPPORT_AUDIO)
        if (desc.enable_audio) {
            asink = gst_element_factory_make("appsink", "ugw_asink");
            if (asink) {
                GstCaps* acaps = gst_caps_from_string("audio/x-raw, format=F32LE, layout=interleaved, rate=48000, channels=2");
                g_object_set(asink, "caps", acaps, "max-buffers", 8, "drop", FALSE, "sync", TRUE, "emit-signals", TRUE, nullptr);
                gst_caps_unref(acaps);
                gst_object_ref(asink);                    // keep our own ref
                g_object_set(pipeline, "audio-sink", asink, nullptr);
            }
        }
#endif
    }

    impl->pipeline   = pipeline;
    impl->video_sink = vsink;     // one owned ref in all paths
    impl->audio_sink = asink;     // one owned ref, or null

    // ---- audio appsink -> UGW AudioStream (shared by both source kinds) -----
#if defined(WINDOW_SUPPORT_AUDIO)
    if (asink) {
        GstAppSinkCallbacks cbs = {};
        cbs.new_sample = on_audio_new_sample;
        gst_app_sink_set_callbacks(GST_APP_SINK(asink), &cbs, impl, nullptr);
        // Create (but don't start) a UGW playback stream fed by the bridge.
        window::audio::AudioManager::initialize(window::audio::AudioBackend::Auto);
        window::audio::AudioStreamConfig sc;
        sc.format = window::audio::AudioFormat::default_format();  // F32, 48k, stereo
        sc.mode = window::audio::AudioStreamMode::Playback;
        sc.output_device_index = desc.audio_device_index;
        impl->audio_stream = window::audio::AudioStream::create(sc, nullptr);
        if (impl->audio_stream) impl->audio_stream->set_callback(&impl->audio_bridge);
    }
#endif

    impl->bus = gst_element_get_bus(pipeline);

    // Bring the pipeline up to PAUSED so caps/duration are negotiated and the first
    // frame is decoded; the caller starts playback with play().
    GstStateChangeReturn sret = gst_element_set_state(pipeline, GST_STATE_PAUSED);
    if (sret == GST_STATE_CHANGE_FAILURE) { impl->shutdown(); delete impl; return fail(VideoResult::ErrorOpenFailed); }
    gst_element_get_state(pipeline, nullptr, nullptr, 5 * GST_SECOND);  // wait for PAUSED (preroll)

    auto* player = new VideoPlayer();
    player->impl_ = impl;
    if (out_result) *out_result = VideoResult::Success;
    return player;
}

void VideoPlayer::destroy() {
    if (impl_) { impl_->shutdown(); delete impl_; impl_ = nullptr; }
    delete this;
}

bool VideoPlayer::update() {
    if (!impl_) return false;
    impl_->poll_bus();

    GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(impl_->video_sink), 0);
    if (!sample) return false;

    GstBuffer* buf  = gst_sample_get_buffer(sample);
    GstCaps*   caps = gst_sample_get_caps(sample);
    GstVideoInfo vinfo;
    if (!buf || !caps || !gst_video_info_from_caps(&vinfo, caps)) {
        gst_sample_unref(sample);
        return false;
    }

    const int w = GST_VIDEO_INFO_WIDTH(&vinfo);
    const int h = GST_VIDEO_INFO_HEIGHT(&vinfo);
    impl_->vid_w = w; impl_->vid_h = h;

    bool presented = false;
    if (impl_->try_import_zero_copy(sample, buf, &vinfo)) {
        presented = true;   // held_sample now owns `sample` -- don't unref it below
    } else {
        GstVideoFrame frame;
        if (gst_video_frame_map(&frame, &vinfo, buf, GST_MAP_READ)) {
            const uint8_t* src = static_cast<const uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
            const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
            impl_->upload_cpu(w, h, src, stride);
            gst_video_frame_unmap(&frame);
            presented = true;
        }
        gst_sample_unref(sample);
    }

    if (presented) {
        const GstClockTime pts = GST_BUFFER_PTS(buf);
        impl_->frame_info.width = w;
        impl_->frame_info.height = h;
        impl_->frame_info.pts_seconds = GST_CLOCK_TIME_IS_VALID(pts) ? (double)pts / GST_SECOND : 0.0;
        impl_->frame_info.frame_index += 1;
        impl_->frame_info.zero_copy = impl_->tex_zero_copy;
        impl_->have_frame = true;
    }
    return presented;
}

TextureHandle VideoPlayer::texture() const { return impl_ ? impl_->tex : TextureHandle{}; }

bool VideoPlayer::get_frame_info(VideoFrameInfo* out) const {
    if (!impl_ || !impl_->have_frame || !out) return false;
    *out = impl_->frame_info;
    return true;
}

VideoResult VideoPlayer::play() {
    if (!impl_) return VideoResult::ErrorUnsupported;
    impl_->eos = false;
    if (gst_element_set_state(impl_->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        return VideoResult::ErrorOpenFailed;
    impl_->playing = true;
#if defined(WINDOW_SUPPORT_AUDIO)
    if (impl_->audio_stream) { if (impl_->audio_stream->is_running()) impl_->audio_stream->resume();
                               else impl_->audio_stream->start(); }
#endif
    return VideoResult::Success;
}

VideoResult VideoPlayer::pause() {
    if (!impl_) return VideoResult::ErrorUnsupported;
    gst_element_set_state(impl_->pipeline, GST_STATE_PAUSED);
    impl_->playing = false;
#if defined(WINDOW_SUPPORT_AUDIO)
    if (impl_->audio_stream) impl_->audio_stream->pause();
#endif
    return VideoResult::Success;
}

VideoResult VideoPlayer::stop() {
    if (!impl_) return VideoResult::ErrorUnsupported;
    gst_element_set_state(impl_->pipeline, GST_STATE_PAUSED);
    gst_element_seek_simple(impl_->pipeline, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), 0);
    impl_->playing = false;
    impl_->eos = false;
#if defined(WINDOW_SUPPORT_AUDIO)
    if (impl_->audio_stream) impl_->audio_stream->stop();
    impl_->audio_bridge.clear();
#endif
    return VideoResult::Success;
}

bool VideoPlayer::seek(double seconds) {
    if (!impl_) return false;
    if (seconds < 0.0) seconds = 0.0;
    gboolean ok = gst_element_seek_simple(
        impl_->pipeline, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
        static_cast<gint64>(seconds * GST_SECOND));
#if defined(WINDOW_SUPPORT_AUDIO)
    if (ok) impl_->audio_bridge.clear();
#endif
    return ok != FALSE;
}

double VideoPlayer::duration() const {
    if (!impl_) return 0.0;
    gint64 dur = 0;
    if (gst_element_query_duration(impl_->pipeline, GST_FORMAT_TIME, &dur) && dur > 0)
        return (double)dur / GST_SECOND;
    return 0.0;
}

double VideoPlayer::position() const {
    if (!impl_) return 0.0;
    gint64 pos = 0;
    if (gst_element_query_position(impl_->pipeline, GST_FORMAT_TIME, &pos) && pos >= 0)
        return (double)pos / GST_SECOND;
    return 0.0;
}

bool VideoPlayer::is_playing() const { return impl_ && impl_->playing; }
bool VideoPlayer::is_eos() const     { return impl_ && impl_->eos; }
int  VideoPlayer::width() const      { return impl_ ? impl_->vid_w : 0; }
int  VideoPlayer::height() const     { return impl_ ? impl_->vid_h : 0; }

void VideoPlayer::set_volume(float v) {
    if (!impl_) return;
#if defined(WINDOW_SUPPORT_AUDIO)
    impl_->audio_bridge.set_volume(v);
    if (impl_->audio_stream) impl_->audio_stream->set_volume(v);
#else
    (void)v;
#endif
}

float VideoPlayer::volume() const {
#if defined(WINDOW_SUPPORT_AUDIO)
    return impl_ ? impl_->audio_bridge.volume() : 0.0f;
#else
    return 0.0f;
#endif
}

} // namespace video
} // namespace window

#endif // WINDOW_SUPPORT_VIDEO
