// ibus D-Bus client — see ibus_client.hpp. Raw POSIX socket + hand-rolled D-Bus
// marshalling (no libdbus/libibus/GLib). The wire format here was validated
// against a live ibus daemon before being committed.
#include "ibus_client.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>

#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace window {
namespace {

constexpr uint32_t IBUS_RELEASE_MASK   = 1u << 30;
constexpr uint32_t IBUS_CAP_PREEDIT    = 1u << 0;   // we render preedit ourselves
constexpr uint32_t IBUS_CAP_FOCUS      = 1u << 3;

using Bytes = std::string;   // a byte buffer; std::string is convenient here

// ── little-endian D-Bus marshaller ──────────────────────────────────────────
struct Marshal {
    Bytes b;
    void align(size_t n) { while (b.size() % n) b.push_back('\0'); }
    void u8(uint8_t v)  { b.push_back(char(v)); }
    void u32(uint32_t v){ align(4); for (int i = 0; i < 4; i++) b.push_back(char(v >> (8 * i))); }
    void str(const std::string& s) { u32(uint32_t(s.size())); b += s; b.push_back('\0'); }
    void sig(const std::string& s) { u8(uint8_t(s.size()));   b += s; b.push_back('\0'); }
};

uint32_t rd_u32(const Bytes& b, size_t off) {
    if (off + 4 > b.size()) return 0;
    return uint32_t(uint8_t(b[off]))        | (uint32_t(uint8_t(b[off+1])) << 8) |
          (uint32_t(uint8_t(b[off+2])) << 16) | (uint32_t(uint8_t(b[off+3])) << 24);
}

// One method_call message (PATH/INTERFACE/MEMBER/DESTINATION [+SIGNATURE]).
Bytes make_call(uint32_t serial, const std::string& path, const std::string& iface,
                const std::string& member, const std::string& dest,
                const std::string& body_sig, const Bytes& body) {
    Marshal m;
    m.u8('l'); m.u8(1); m.u8(0); m.u8(1);          // LE, METHOD_CALL, flags, ver
    m.u32(uint32_t(body.size()));
    m.u32(serial);
    Marshal hf;
    auto field = [&](uint8_t code, char type, const std::string& val) {
        hf.align(8); hf.u8(code); hf.sig(std::string(1, type));
        if (type == 'g') hf.sig(val); else hf.str(val);
    };
    field(1, 'o', path);
    if (!iface.empty())    field(2, 's', iface);
    field(3, 's', member);
    field(6, 's', dest);
    if (!body_sig.empty()) field(8, 'g', body_sig);
    m.align(4); m.u32(uint32_t(hf.b.size()));
    m.b += hf.b;
    m.align(8);
    m.b += body;
    return m.b;
}

bool write_all(int fd, const void* p, size_t n) {
    const char* c = static_cast<const char*>(p);
    while (n) { ssize_t w = ::write(fd, c, n); if (w <= 0) return false; c += w; n -= size_t(w); }
    return true;
}
bool read_n(int fd, void* p, size_t n) {
    char* c = static_cast<char*>(p);
    while (n) { ssize_t r = ::read(fd, c, n); if (r <= 0) return false; c += r; n -= size_t(r); }
    return true;
}

// Extract the text out of an IBusText "(sa{sv}sv)" embedded as a variant at
// offset i (advancing i past it). Layout validated against live ibus.
std::string parse_ibustext(const Bytes& b, size_t& i) {
    auto al = [&](size_t n){ while (i % n) i++; };
    if (i >= b.size()) return "";
    uint8_t sl = uint8_t(b[i]); i += 1 + sl + 1;            // skip variant signature
    al(8);                                                  // struct align
    al(4); uint32_t l1 = rd_u32(b, i); i += 4 + l1 + 1;     // skip class name "IBusText"
    al(4); uint32_t la = rd_u32(b, i); i += 4;              // a{sv} attachments length
    al(8); i += la;                                         // dict-entry element align (even if empty), then content
    al(4); uint32_t l2 = rd_u32(b, i); i += 4;              // the text string
    std::string txt = (i + l2 <= b.size()) ? b.substr(i, l2) : std::string();
    i += l2 + 1;
    return txt;
}

std::string ibus_socket_path() {
    const char* disp = ::getenv("DISPLAY");
    std::string d = disp ? disp : ":0";
    auto colon = d.find(':');
    std::string num = colon == std::string::npos ? "0" : d.substr(colon + 1);
    if (auto dot = num.find('.'); dot != std::string::npos) num = num.substr(0, dot);
    std::ifstream mi("/etc/machine-id"); std::string mid; std::getline(mi, mid);
    const char* home = ::getenv("HOME");
    if (!home || mid.empty()) return "";
    std::ifstream f(std::string(home) + "/.config/ibus/bus/" + mid + "-unix-" + num);
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("IBUS_ADDRESS=", 0) == 0) {
            auto v = line.substr(13);
            auto p = v.find("path=");
            if (p == std::string::npos) return "";
            auto c = v.find(',', p);
            return v.substr(p + 5, c - (p + 5));
        }
    }
    return "";
}

}  // namespace

// ── message read + dispatch ─────────────────────────────────────────────────
bool IBusClient::read_one(int timeout_ms, int& type, std::string& member,
                          uint32_t& reply_serial, std::string& body) {
    if (fd_ < 0) return false;
    pollfd pfd{fd_, POLLIN, 0};
    if (::poll(&pfd, 1, timeout_ms) <= 0) return false;
    unsigned char hdr[16];
    if (!read_n(fd_, hdr, 16)) { ::close(fd_); fd_ = -1; return false; }
    Bytes h(reinterpret_cast<char*>(hdr), 16);
    type = hdr[1];
    uint32_t blen = rd_u32(h, 4), alen = rd_u32(h, 12);
    Bytes fields(alen, '\0');
    if (alen && !read_n(fd_, &fields[0], alen)) { ::close(fd_); fd_ = -1; return false; }
    size_t pad = (8 - ((16 + alen) % 8)) % 8;
    char tmp[8];
    if (pad && !read_n(fd_, tmp, pad)) { ::close(fd_); fd_ = -1; return false; }
    body.assign(blen, '\0');
    if (blen && !read_n(fd_, &body[0], blen)) { ::close(fd_); fd_ = -1; return false; }

    reply_serial = 0; member.clear();
    size_t i = 0; auto al = [&](size_t n){ while (i % n) i++; };
    while (i < fields.size()) {
        al(8); if (i >= fields.size()) break;
        uint8_t code = uint8_t(fields[i++]);
        uint8_t slen = uint8_t(fields[i++]);
        std::string vs = fields.substr(i, slen); i += slen + 1;
        if (vs == "u")              { al(4); uint32_t v = rd_u32(fields, i); i += 4; if (code == 5) reply_serial = v; }
        else if (vs == "s" || vs == "o") { al(4); uint32_t l = rd_u32(fields, i); i += 4; std::string s = fields.substr(i, l); i += l + 1; if (code == 3) member = s; }
        else if (vs == "g")         { uint8_t l = uint8_t(fields[i++]); i += l + 1; }
        else break;
    }
    return true;
}

void IBusClient::dispatch(int type, const std::string& member, uint32_t reply_serial,
                          const std::string& body) {
    if (type == 2 && body.size() >= 4) {                 // METHOD_RETURN (ProcessKeyEvent → b)
        last_handled_ = (rd_u32(body, 0) != 0);
        (void)reply_serial;
        return;
    }
    if (type != 4) return;                               // only signals below
    if (member == "CommitText") {
        size_t i = 0; std::string t = parse_ibustext(body, i);
        if (!t.empty() && on_commit) on_commit(t);
    } else if (member == "UpdatePreeditText" || member == "UpdatePreeditTextWithMode") {
        // Signature (v u b) — IBusText variant, uint32 cursor_pos, bool visible — or
        // (v u b u) for *WithMode. parse_ibustext only reads up to the text and leaves
        // `i` inside the variant (IBusText has a trailing attribute list), so the cursor
        // is NOT at `i`. It IS the first fixed word after the variant, i.e. read it from
        // the TAIL: the body ends with [cursor u][visible b] (+ [mode u] for WithMode),
        // each a 4-byte word. (cursor is in codepoints → caret position within preedit.)
        size_t i = 0; std::string t = parse_ibustext(body, i);
        const size_t trail = (member == "UpdatePreeditTextWithMode") ? 12u : 8u;
        int cursor = (body.size() >= trail) ? static_cast<int>(rd_u32(body, body.size() - trail)) : 0;
        if (on_preedit) on_preedit(t, cursor);
    } else if (member == "ShowPreeditText") {
        // nothing extra; last UpdatePreeditText already carried the text
    } else if (member == "HidePreeditText") {
        if (on_preedit) on_preedit(std::string(), 0);
    } else if (member == "ForwardKeyEvent") {            // (u keyval, u keycode, u state)
        if (body.size() >= 12 && on_forward)
            on_forward(rd_u32(body, 0), rd_u32(body, 4), rd_u32(body, 8));
    }
}

// ── public API ──────────────────────────────────────────────────────────────
IBusClient::~IBusClient() { if (fd_ >= 0) ::close(fd_); }

bool IBusClient::call_no_reply(const std::string& member, const std::string& body_sig,
                               const std::string& body_bytes) {
    if (fd_ < 0 || ctx_path_.empty()) return false;
    Bytes msg = make_call(serial_++, ctx_path_, "org.freedesktop.IBus.InputContext",
                          member, "org.freedesktop.IBus", body_sig, body_bytes);
    return write_all(fd_, msg.data(), msg.size());
}

bool IBusClient::connect(const char* client_name) {
    std::string sock = ibus_socket_path();
    if (sock.empty()) return false;

    fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) return false;
    sockaddr_un a{}; a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, sock.c_str(), sizeof(a.sun_path) - 1);
    if (::connect(fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) { ::close(fd_); fd_ = -1; return false; }

    // SASL EXTERNAL auth: NUL, AUTH EXTERNAL <hex(uid)>, expect OK, BEGIN.
    char nul = 0; write_all(fd_, &nul, 1);
    std::string uid = std::to_string(::getuid()), hex; char hb[4];
    for (char ch : uid) { std::snprintf(hb, sizeof hb, "%02x", static_cast<unsigned char>(ch)); hex += hb; }
    std::string auth = "AUTH EXTERNAL " + hex + "\r\n";
    write_all(fd_, auth.data(), auth.size());
    char buf[256]; ssize_t n = ::read(fd_, buf, sizeof buf - 1);
    if (n < 2 || std::strncmp(buf, "OK", 2) != 0) { ::close(fd_); fd_ = -1; return false; }
    std::string begin = "BEGIN\r\n"; write_all(fd_, begin.data(), begin.size());

    int type; std::string member, rbody; uint32_t rs;
    // Hello → unique name (value unused, but the call is required).
    uint32_t hs = serial_++;
    Bytes hello = make_call(hs, "/org/freedesktop/DBus", "org.freedesktop.DBus", "Hello",
                            "org.freedesktop.DBus", "", {});
    if (!write_all(fd_, hello.data(), hello.size())) { ::close(fd_); fd_ = -1; return false; }
    bool got = false;
    for (int k = 0; k < 50 && read_one(500, type, member, rs, rbody); ++k)
        if (type == 2 && rs == hs) { got = true; break; }
    if (!got) { ::close(fd_); fd_ = -1; return false; }

    // CreateInputContext(name) → object path.
    Marshal nameb; nameb.str(client_name ? client_name : "app");
    uint32_t cs = serial_++;
    Bytes cic = make_call(cs, "/org/freedesktop/IBus", "org.freedesktop.IBus",
                          "CreateInputContext", "org.freedesktop.IBus", "s", nameb.b);
    if (!write_all(fd_, cic.data(), cic.size())) { ::close(fd_); fd_ = -1; return false; }
    got = false;
    for (int k = 0; k < 50 && read_one(500, type, member, rs, rbody); ++k) {
        if (type == 2 && rs == cs && rbody.size() >= 4) {
            uint32_t l = rd_u32(rbody, 0); ctx_path_ = rbody.substr(4, l); got = true; break;
        }
        if (type == 3 && rs == cs) break;     // ERROR
    }
    if (!got || ctx_path_.empty()) { ::close(fd_); fd_ = -1; ctx_path_.clear(); return false; }

    { Marshal m; m.u32(IBUS_CAP_PREEDIT | IBUS_CAP_FOCUS); call_no_reply("SetCapabilities", "u", m.b); }
    call_no_reply("FocusIn", "", {});
    return true;
}

void IBusClient::focus_in()  { call_no_reply("FocusIn",  "", {}); }
void IBusClient::focus_out() { call_no_reply("FocusOut", "", {}); }

bool IBusClient::process_key(uint32_t keyval, uint32_t keycode, uint32_t state, bool press) {
    if (!active()) return false;
    Marshal m; m.u32(keyval); m.u32(keycode); m.u32(state | (press ? 0u : IBUS_RELEASE_MASK));
    uint32_t s = serial_++;
    Bytes msg = make_call(s, ctx_path_, "org.freedesktop.IBus.InputContext",
                          "ProcessKeyEvent", "org.freedesktop.IBus", "uuu", m.b);
    if (!write_all(fd_, msg.data(), msg.size())) { ::close(fd_); fd_ = -1; ctx_path_.clear(); return false; }
    // Block until the bool reply (matching serial), dispatching any commit/
    // preedit signals that arrive first. ibus is local, so this is sub-ms.
    last_handled_ = false;
    int type; std::string member, body; uint32_t rs;
    for (int k = 0; k < 200; ++k) {
        if (!read_one(200, type, member, rs, body)) break;
        if (type == 2 && rs == s) { dispatch(type, member, rs, body); break; }
        dispatch(type, member, rs, body);
    }
    return last_handled_;
}

void IBusClient::pump() {
    if (fd_ < 0) return;
    int type; std::string member, body; uint32_t rs;
    while (read_one(0, type, member, rs, body)) dispatch(type, member, rs, body);
}

}  // namespace window
