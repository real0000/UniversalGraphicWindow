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
#include <utility>

namespace window {
namespace gfx {

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

// Indirect-args buffer (draw/dispatch arguments produced on the GPU).
class IndirectBuffer : public Buffer {
public:
    bool create(GraphicDevice* device, uint32_t size,
                ResourceUsage usage = ResourceUsage::Default,
                const void* data = nullptr, const char* debug_name = nullptr);
};

} // namespace gfx
} // namespace window
