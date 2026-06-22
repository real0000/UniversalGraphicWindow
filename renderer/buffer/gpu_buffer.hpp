#pragma once
// renderer/buffer/gpu_buffer.hpp
//
// Typed, RAII wrappers over the backend-neutral GraphicDevice buffer resources
// (graphics_api.hpp), plus a lightweight *view* model so that one allocation can be
// bound several different ways.
//
// The RHI exposes a single handle-based buffer (BufferHandle) created with one
// BufferType (Vertex / Index / Uniform / Storage / Indirect). These wrappers add:
//   * ownership + lifetime (destroy on scope exit, move-only, no double-free),
//   * the element metadata the raw handle drops (stride, index format, counts),
//   * one-call binding helpers, and
//   * BufferView — a cheap, copyable (buffer, offset, size, stride/format, role)
//     descriptor. A single Buffer can spawn many views: a big vertex buffer sliced
//     into per-mesh vertex windows, a uniform buffer sliced into per-object constant
//     windows (dynamic offsets), or a storage buffer exposed as both a read and a
//     read-write window. Views own nothing — the Buffer owns the allocation — so they
//     are free to create and pass around.
//
// All classes are header-declared / cpp-defined and live in window::gfx, matching the
// rest of renderer/ (vector_renderer, assets). They forward to a GraphicDevice you keep
// alive for the buffer's lifetime.

#include "../../graphics_api.hpp"

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace window {
namespace gfx {

// Round `v` up to a multiple of `a` (any non-zero alignment). Used to hide GPU buffer
// alignment (std140/std430 element stride, dynamic-offset granularity) from callers.
inline uint32_t align_up(uint32_t v, uint32_t a) { return a ? (v + a - 1) / a * a : v; }

//=============================================================================
// BufferView — a bindable window onto (a sub-range of) a Buffer.
//=============================================================================

// How a view binds itself to a GraphicCommander.
enum class BufferViewType : uint8_t {
    Vertex,    // bind_vertex_buffer(slot, buffer, offset)
    Index,     // bind_index_buffer(buffer, index_format, offset)
    Uniform,   // bind_uniform_buffer(slot, buffer, offset, size)   (constant buffer / cbuffer)
    Storage,   // bind_storage_buffer(slot, buffer, offset, size)   (SSBO / UAV buffer)
    Indirect,  // consumed directly as the args buffer of draw_*_indirect / dispatch_indirect
};

// A cheap value type describing one way to bind one (sub-range of a) buffer. Copyable;
// owns nothing. Build them from a Buffer (buf.vertex_view(), buf.uniform_view(off,sz), …)
// or by hand. The same buffer can have any number of live views at once.
struct BufferView {
    BufferHandle   buffer;
    BufferViewType type   = BufferViewType::Vertex;
    uint32_t       offset = 0;                            // byte offset into the buffer
    uint32_t       size   = 0;                            // byte length (0 = to end; Uniform/Storage)
    uint32_t       stride = 0;                            // element stride (vertex / structured)
    IndexFormat    index_format = IndexFormat::UInt32;    // Index views only
    StorageAccess  access = StorageAccess::ReadWrite;     // Storage views (hint for the binding)

    bool valid() const { return buffer.valid(); }

    // Bind to a commander. `slot` is the shader binding slot (ignored for Index, which
    // has no slot, and Indirect, which is not "bound"). Storage/Uniform pass offset+size.
    void bind(GraphicCommander* cmd, uint32_t slot = 0) const;
};

//=============================================================================
// Buffer — RAII owner of one GraphicDevice buffer allocation.
//=============================================================================

class Buffer {
public:
    Buffer() = default;
    ~Buffer() { destroy(); }
    Buffer(Buffer&& o) noexcept { move_from(o); }
    Buffer& operator=(Buffer&& o) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // Generic creation. If desc.initial_data is set, the contents upload at creation.
    bool create(GraphicDevice* device, const BufferDesc& desc);
    // Convenience: build the descriptor from explicit fields.
    bool create(GraphicDevice* device, BufferType type, uint32_t size,
                ResourceUsage usage = ResourceUsage::Default,
                const void* data = nullptr, uint32_t stride = 0,
                const char* debug_name = nullptr);
    void destroy();

    bool           valid()  const { return handle_.valid(); }
    BufferHandle   handle() const { return handle_; }
    operator BufferHandle() const { return handle_; }     // bind/destroy directly with a Buffer
    GraphicDevice* device() const { return device_; }
    uint32_t       size()   const { return desc_.size; }
    uint32_t       stride() const { return desc_.stride; }
    BufferType     type()   const { return desc_.type; }
    const BufferDesc& desc() const { return desc_; }

    // ---- Data transfer (forward to the owning device) -----------------------
    void  update(const void* data, uint32_t size, uint32_t offset = 0);
    void* map(uint32_t offset = 0, uint32_t size = 0);     // needs Dynamic/Staging usage
    void  unmap();
    void  read(void* dst, uint32_t size, uint32_t offset = 0);

    // ---- Views (one buffer → many bindable windows) -------------------------
    // The role must be compatible with how the underlying allocation can be bound
    // (a Vertex buffer makes vertex views, a Storage buffer makes read/read-write
    // storage views, etc.); see the header note above.
    BufferView view(BufferViewType type, uint32_t offset = 0, uint32_t size = 0,
                    uint32_t stride = 0) const;
    BufferView vertex_view(uint32_t offset = 0, uint32_t stride = 0) const;
    BufferView index_view(IndexFormat fmt = IndexFormat::UInt32, uint32_t offset = 0) const;
    BufferView uniform_view(uint32_t offset = 0, uint32_t size = 0) const;
    BufferView storage_view(uint32_t offset = 0, uint32_t size = 0,
                            StorageAccess access = StorageAccess::ReadWrite) const;
    BufferView indirect_view(uint32_t offset = 0) const;

protected:
    void move_from(Buffer& o) noexcept;

    GraphicDevice* device_ = nullptr;
    BufferHandle   handle_;
    BufferDesc     desc_{};
};

//=============================================================================
// Typed buffers — thin subclasses that carry the element metadata + a 1-call bind.
//=============================================================================

// Vertex buffer: vertex_count * stride bytes of per-vertex (or per-instance) data.
class VertexBuffer : public Buffer {
public:
    bool create(GraphicDevice* device, uint32_t vertex_count, uint32_t stride,
                const void* data = nullptr, ResourceUsage usage = ResourceUsage::Default,
                const char* debug_name = nullptr);
    // From a typed array: stride = sizeof(V), count = element count.
    template <class V>
    bool create_from(GraphicDevice* device, const V* verts, uint32_t count,
                     ResourceUsage usage = ResourceUsage::Immutable, const char* name = nullptr) {
        return create(device, count, (uint32_t)sizeof(V), verts, usage, name);
    }
    uint32_t vertex_count() const { return stride() ? size() / stride() : 0; }
    void bind(GraphicCommander* cmd, uint32_t slot = 0, uint32_t offset = 0) const {
        cmd->bind_vertex_buffer(slot, handle_, offset);
    }
};

// Index buffer: index_count indices of UInt16 or UInt32.
class IndexBuffer : public Buffer {
public:
    bool create(GraphicDevice* device, uint32_t index_count, IndexFormat fmt,
                const void* data = nullptr, ResourceUsage usage = ResourceUsage::Default,
                const char* debug_name = nullptr);
    bool create_from(GraphicDevice* device, const uint16_t* idx, uint32_t count,
                     ResourceUsage usage = ResourceUsage::Immutable, const char* name = nullptr);
    bool create_from(GraphicDevice* device, const uint32_t* idx, uint32_t count,
                     ResourceUsage usage = ResourceUsage::Immutable, const char* name = nullptr);
    IndexFormat format() const { return format_; }
    uint32_t    index_count() const {
        return size() / (format_ == IndexFormat::UInt16 ? 2u : 4u);
    }
    void bind(GraphicCommander* cmd, uint32_t offset = 0) const {
        cmd->bind_index_buffer(handle_, format_, offset);
    }

private:
    IndexFormat format_ = IndexFormat::UInt32;
};

// Constant buffer (uniform / cbuffer). Defaults to Dynamic so set()/update() are cheap.
class ConstBuffer : public Buffer {
public:
    bool create(GraphicDevice* device, uint32_t size,
                ResourceUsage usage = ResourceUsage::Dynamic,
                const void* data = nullptr, const char* debug_name = nullptr);
    // Size the buffer for a POD struct T, optionally seeding it.
    template <class T>
    bool create_for(GraphicDevice* device, const T* initial = nullptr,
                    ResourceUsage usage = ResourceUsage::Dynamic, const char* name = nullptr) {
        return create(device, (uint32_t)sizeof(T), usage, initial, name);
    }
    // Replace the whole buffer with a struct value.
    template <class T>
    void set(const T& value) { update(&value, (uint32_t)sizeof(T), 0); }
    void bind(GraphicCommander* cmd, uint32_t slot, uint32_t offset = 0, uint32_t size = 0) const {
        cmd->bind_uniform_buffer(slot, handle_, offset, size);
    }
};
// The RHI calls this a uniform buffer; expose both spellings.
using UniformBuffer = ConstBuffer;

// Storage buffer (SSBO / structured UAV). Read in shaders as an SRV and/or written as a
// UAV — exactly the "one buffer, two views" case the backends support natively.
class StorageBuffer : public Buffer {
public:
    bool create(GraphicDevice* device, uint32_t size, uint32_t stride = 0,
                ResourceUsage usage = ResourceUsage::Default,
                const void* data = nullptr, const char* debug_name = nullptr);
    template <class T>
    bool create_array(GraphicDevice* device, uint32_t count, const T* data = nullptr,
                      ResourceUsage usage = ResourceUsage::Default, const char* name = nullptr) {
        return create(device, count * (uint32_t)sizeof(T), (uint32_t)sizeof(T), usage, data, name);
    }
    uint32_t element_count() const { return stride() ? size() / stride() : 0; }
    void bind(GraphicCommander* cmd, uint32_t slot, uint32_t offset = 0, uint32_t size = 0) const {
        cmd->bind_storage_buffer(slot, handle_, offset, size);
    }
    // A read-only and a read-write view of the same allocation.
    BufferView read_view(uint32_t offset = 0, uint32_t size = 0) const {
        return storage_view(offset, size, StorageAccess::Read);
    }
    BufferView read_write_view(uint32_t offset = 0, uint32_t size = 0) const {
        return storage_view(offset, size, StorageAccess::ReadWrite);
    }
};

//=============================================================================
// Typed editing wrappers — a CPU-side mirror you edit naturally, then flush().
//=============================================================================
//
// ConstBuffer/StorageBuffer take raw bytes (set<T>() / update()). These add an
// owned CPU copy so the contents read/write like a plain struct or a std::vector:
//
//   TypedConstBuffer<FrameUBO> frame;  frame.create(dev);
//   frame->view_proj = vp;  frame->metallic = 0.5f;  frame.flush();   // edit + upload
//
//   TypedStorageBuffer<Particle> parts;  parts.create(dev, 1024);
//   for (auto& p : parts) p.pos += vel;     // range-for over the mirror
//   parts[0].mass = 2.0f;  parts.flush();    // operator[] then upload
//
// flush() pushes the whole mirror (or one element) to the GPU; sync() pulls it back.
// Editing is purely CPU-side until flush() — so batch edits, then one upload.

// Constant/uniform buffer with a typed CPU mirror reachable via ->, *, and data().
template <class T>
class TypedConstBuffer : public ConstBuffer {
public:
    bool create(GraphicDevice* device, const T& initial = T{},
                ResourceUsage usage = ResourceUsage::Dynamic, const char* name = nullptr) {
        cpu_ = initial;
        return ConstBuffer::create(device, (uint32_t)sizeof(T), usage, &cpu_, name);
    }
    T&       data()              { return cpu_; }
    const T& data()  const       { return cpu_; }
    T*       operator->()        { return &cpu_; }
    const T* operator->() const  { return &cpu_; }
    T&       operator*()         { return cpu_; }
    const T& operator*()  const  { return cpu_; }
    // Upload the CPU mirror to the GPU (call after editing through ->/*).
    void flush()                 { update(&cpu_, (uint32_t)sizeof(T), 0); }
    // Replace the whole struct and upload in one step.
    void set(const T& value)     { cpu_ = value; flush(); }
    // Pull the current GPU contents back into the CPU mirror (needs read-capable usage).
    void sync()                  { read(&cpu_, (uint32_t)sizeof(T), 0); }

private:
    T cpu_{};
};

// Storage buffer with a std::vector-like CPU mirror: operator[], size(), range-for.
template <class T>
class TypedStorageBuffer : public StorageBuffer {
public:
    bool create(GraphicDevice* device, uint32_t count, const T* data = nullptr,
                ResourceUsage usage = ResourceUsage::Default, const char* name = nullptr) {
        cpu_.assign(count, T{});
        if (data) for (uint32_t i = 0; i < count; ++i) cpu_[i] = data[i];
        return StorageBuffer::create(device, count * (uint32_t)sizeof(T), (uint32_t)sizeof(T),
                                     usage, cpu_.empty() ? nullptr : cpu_.data(), name);
    }

    // ---- vector-like CPU-side access (edit, then flush) ---------------------
    T&       operator[](size_t i)       { return cpu_[i]; }
    const T& operator[](size_t i) const { return cpu_[i]; }
    size_t   size()  const { return cpu_.size(); }
    bool     empty() const { return cpu_.empty(); }
    T*       begin()       { return cpu_.data(); }
    T*       end()         { return cpu_.data() + cpu_.size(); }
    const T* begin() const { return cpu_.data(); }
    const T* end()   const { return cpu_.data() + cpu_.size(); }
    std::vector<T>&       vec()       { return cpu_; }
    const std::vector<T>& vec() const { return cpu_; }

    // Upload the whole mirror, or a single element, to the GPU.
    void flush()          { if (!cpu_.empty()) update(cpu_.data(), (uint32_t)(cpu_.size() * sizeof(T)), 0); }
    void flush(size_t i)  { update(&cpu_[i], (uint32_t)sizeof(T), (uint32_t)(i * sizeof(T))); }
    // Pull the current GPU contents back into the CPU mirror (needs read-capable usage).
    void sync()           { if (!cpu_.empty()) read(cpu_.data(), (uint32_t)(cpu_.size() * sizeof(T)), 0); }

private:
    std::vector<T> cpu_;
};

//=============================================================================
// UniformArray<T> — a []-indexed array of uniform structs, alignment fully hidden.
//=============================================================================
//
// Use it like std::vector. Each element gets a GPU slot padded to the device's uniform-buffer
// offset alignment (typically 256B) so any single element can be bound by dynamic offset, but
// the CPU side stays a tight array of T — you never compute a stride or an offset:
//
//   UniformArray<ObjectCB> objs;  objs.create(dev, n);
//   objs[i].model = m;                            // ordinary [] set
//   objs.flush();                                 // one upload; padding handled internally
//   mat->set_uniform_element("Object", objs, i);  // bind element i (offset hidden)
//
// flush()/sync() transfer the whole buffer (offset 0), so the per-element padding is invisible
// and each backend's partial-update rules are sidestepped; binding uses the aligned per-element
// offset, which every backend honors (D3D12 CBV 256B, D3D11.1 first-constant, VK/GL min offset).
template <class T>
class UniformArray : public Buffer {
public:
    bool create(GraphicDevice* device, uint32_t count, ResourceUsage usage = ResourceUsage::Default,
                const char* name = nullptr) {
        GraphicsCapabilities caps{}; if (device) device->get_capabilities(&caps);
        const uint32_t a = caps.min_uniform_buffer_offset_alignment > 0 ? (uint32_t)caps.min_uniform_buffer_offset_alignment : 256u;
        stride_ = align_up((uint32_t)sizeof(T), a);
        cpu_.assign(count, T{});
        return Buffer::create(device, BufferType::Uniform, stride_ * count, usage, nullptr, 0, name);
    }

    // ---- std::vector-like CPU-side access; edit then flush() --------------------
    T&       operator[](size_t i)       { return cpu_[i]; }
    const T& operator[](size_t i) const { return cpu_[i]; }
    size_t   size()  const { return cpu_.size(); }
    bool     empty() const { return cpu_.empty(); }
    T*       begin()       { return cpu_.data(); }
    T*       end()         { return cpu_.data() + cpu_.size(); }
    const T* begin() const { return cpu_.data(); }
    const T* end()   const { return cpu_.data() + cpu_.size(); }

    uint32_t stride()           const { return stride_; }                  // aligned GPU slot size (internal)
    uint32_t element_size()     const { return (uint32_t)sizeof(T); }
    uint32_t byte_offset(size_t i) const { return stride_ * (uint32_t)i; } // aligned bind offset of element i

    // Upload the whole array in one transfer (per-element padding handled internally).
    void flush() {
        if (cpu_.empty()) return;
        std::vector<uint8_t> blob((size_t)stride_ * cpu_.size(), 0);
        for (size_t i = 0; i < cpu_.size(); ++i) std::memcpy(blob.data() + i * stride_, &cpu_[i], sizeof(T));
        update(blob.data(), (uint32_t)blob.size(), 0);
    }
    // Pull the GPU contents back into the CPU mirror (needs read-capable usage).
    void sync() {
        if (cpu_.empty()) return;
        std::vector<uint8_t> blob((size_t)stride_ * cpu_.size());
        read(blob.data(), (uint32_t)blob.size(), 0);
        for (size_t i = 0; i < cpu_.size(); ++i) std::memcpy(&cpu_[i], blob.data() + i * stride_, sizeof(T));
    }
    // Bind element i as the constant buffer at `slot` (direct path; for Material use set_uniform_element).
    void bind(GraphicCommander* cmd, uint32_t slot, size_t i) const {
        cmd->bind_uniform_buffer(slot, handle(), byte_offset(i), (uint32_t)sizeof(T));
    }

private:
    std::vector<T> cpu_;
    uint32_t       stride_ = 0;
};

// Indirect-args buffer (draw/dispatch arguments produced on the GPU).
class IndirectBuffer : public Buffer {
public:
    bool create(GraphicDevice* device, uint32_t size,
                ResourceUsage usage = ResourceUsage::Default,
                const void* data = nullptr, const char* debug_name = nullptr);
};

} // namespace gfx
} // namespace window
