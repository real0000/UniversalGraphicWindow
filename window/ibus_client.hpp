// ibus input-method client (Linux/X11).
//
// Talks to the ibus daemon directly over its private D-Bus socket, the same
// channel Qt/GTK/Electron use (QT_IM_MODULE=ibus etc.) — NOT the legacy XIM
// bridge (XOpenIM/XFilterEvent). On systems where the XIM bridge is disabled or
// broken, this is the only way a raw-Xlib app gets CJK/complex input. The wire
// protocol (SASL EXTERNAL auth + D-Bus marshalling) is implemented over a plain
// POSIX socket, so there is no libdbus / libibus / GLib dependency.
//
// Usage: connect() once; on each X KeyPress/KeyRelease call process_key() and,
// if it returns true, do not handle the key yourself — committed text arrives
// via on_commit and composing text via on_preedit.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace window {

class IBusClient {
public:
    IBusClient() = default;
    ~IBusClient();
    IBusClient(const IBusClient&) = delete;
    IBusClient& operator=(const IBusClient&) = delete;

    // Connect to the ibus bus for the current $DISPLAY and create an input
    // context. Returns false (and leaves active()==false) if ibus is not
    // running / reachable, in which case the caller should fall back to plain
    // XLookupString. client_name is a free-form identifier for ibus logs.
    bool connect(const char* client_name);
    bool active() const { return fd_ >= 0 && !ctx_path_.empty(); }

    void focus_in();
    void focus_out();

    // Feed a key to ibus. keyval is the X keysym, keycode the hardware code
    // (X keycode − 8), state the X modifier mask; press=false marks a release.
    // Returns true if ibus consumed the key (the caller must not also process
    // it). Any resulting commit/preedit/forward is dispatched synchronously via
    // the callbacks before this returns.
    bool process_key(uint32_t keyval, uint32_t keycode, uint32_t state, bool press);

    // Drain any pending signals without blocking (late commits/preedit). Safe to
    // call once per frame.
    void pump();

    // Committed UTF-8 text (insert into the focused field).
    std::function<void(const std::string&)>                          on_commit;
    // Current composing/preedit UTF-8 text (empty = nothing composing) + the IME's
    // own cursor within it (in codepoints). The caller renders this inline; ibus does
    // not draw it when its panel is off.
    std::function<void(const std::string& text, int cursor_codepoints)> on_preedit;
    // A key ibus chose not to use and handed back (treat as a normal keypress).
    std::function<void(uint32_t keyval, uint32_t keycode, uint32_t state)> on_forward;

private:
    bool     call_no_reply(const std::string& member, const std::string& body_sig,
                           const std::string& body_bytes);
    void     dispatch(int type, const std::string& member, uint32_t reply_serial,
                      const std::string& body);
    bool     read_one(int timeout_ms, int& type, std::string& member,
                      uint32_t& reply_serial, std::string& body);

    int          fd_ = -1;
    std::string  ctx_path_;        // InputContext object path
    uint32_t     serial_ = 1;
    bool         last_handled_ = false;
};

}  // namespace window
