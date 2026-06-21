// renderer/buffer/gpu_buffer.cpp — see gpu_buffer.hpp.

#include "gpu_buffer.hpp"

namespace window {
namespace gfx {

//=============================================================================
// BufferView
//=============================================================================

void BufferView::bind(GraphicCommander* cmd, uint32_t slot) const {
    if (!cmd || !buffer.valid()) return;
    switch (type) {
        case BufferViewType::Vertex:
            cmd->bind_vertex_buffer(slot, buffer, offset);
            break;
        case BufferViewType::Index:
            cmd->bind_index_buffer(buffer, index_format, offset);
            break;
        case BufferViewType::Uniform:
            cmd->bind_uniform_buffer(slot, buffer, offset, size);
            break;
        case BufferViewType::Storage:
            cmd->bind_storage_buffer(slot, buffer, offset, size);
            break;
        case BufferViewType::Indirect:
            // Not bound: pass the buffer straight to draw_*_indirect / dispatch_indirect.
            break;
    }
}

//=============================================================================
// Buffer
//=============================================================================

void Buffer::move_from(Buffer& o) noexcept {
    device_ = o.device_;
    handle_ = o.handle_;
    desc_   = o.desc_;
    o.device_ = nullptr;
    o.handle_ = BufferHandle{};
    o.desc_   = BufferDesc{};
}

Buffer& Buffer::operator=(Buffer&& o) noexcept {
    if (this != &o) {
        destroy();
        move_from(o);
    }
    return *this;
}

bool Buffer::create(GraphicDevice* device, const BufferDesc& desc) {
    destroy();
    if (!device) return false;
    BufferHandle h = device->create_buffer(desc);
    if (!h.valid()) return false;
    device_ = device;
    handle_ = h;
    desc_   = desc;
    desc_.initial_data = nullptr;   // don't retain the caller's pointer past creation
    return true;
}

bool Buffer::create(GraphicDevice* device, BufferType type, uint32_t size,
                    ResourceUsage usage, const void* data, uint32_t stride,
                    const char* debug_name) {
    BufferDesc d;
    d.size         = size;
    d.type         = type;
    d.usage        = usage;
    d.stride       = stride;
    d.initial_data = data;
    d.debug_name   = debug_name;
    return create(device, d);
}

void Buffer::destroy() {
    if (device_ && handle_.valid()) {
        device_->destroy_buffer(handle_);
    }
    device_ = nullptr;
    handle_ = BufferHandle{};
    desc_   = BufferDesc{};
}

void Buffer::update(const void* data, uint32_t size, uint32_t offset) {
    if (device_ && handle_.valid()) device_->update_buffer(handle_, data, size, offset);
}

void* Buffer::map(uint32_t offset, uint32_t size) {
    return (device_ && handle_.valid()) ? device_->map_buffer(handle_, offset, size) : nullptr;
}

void Buffer::unmap() {
    if (device_ && handle_.valid()) device_->unmap_buffer(handle_);
}

void Buffer::read(void* dst, uint32_t size, uint32_t offset) {
    if (device_ && handle_.valid()) device_->read_buffer(handle_, dst, size, offset);
}

//---- Views ------------------------------------------------------------------

BufferView Buffer::view(BufferViewType type, uint32_t offset, uint32_t size,
                        uint32_t stride) const {
    BufferView v;
    v.buffer = handle_;
    v.type   = type;
    v.offset = offset;
    v.size   = size;
    v.stride = stride;
    return v;
}

BufferView Buffer::vertex_view(uint32_t offset, uint32_t stride) const {
    return view(BufferViewType::Vertex, offset, 0, stride ? stride : desc_.stride);
}

BufferView Buffer::index_view(IndexFormat fmt, uint32_t offset) const {
    BufferView v = view(BufferViewType::Index, offset);
    v.index_format = fmt;
    return v;
}

BufferView Buffer::uniform_view(uint32_t offset, uint32_t size) const {
    return view(BufferViewType::Uniform, offset, size);
}

BufferView Buffer::storage_view(uint32_t offset, uint32_t size, StorageAccess access) const {
    BufferView v = view(BufferViewType::Storage, offset, size, desc_.stride);
    v.access = access;
    return v;
}

BufferView Buffer::indirect_view(uint32_t offset) const {
    return view(BufferViewType::Indirect, offset);
}

//=============================================================================
// Typed buffers
//=============================================================================

bool VertexBuffer::create(GraphicDevice* device, uint32_t vertex_count, uint32_t stride,
                          const void* data, ResourceUsage usage, const char* debug_name) {
    return Buffer::create(device, BufferType::Vertex, vertex_count * stride, usage,
                          data, stride, debug_name);
}

bool IndexBuffer::create(GraphicDevice* device, uint32_t index_count, IndexFormat fmt,
                         const void* data, ResourceUsage usage, const char* debug_name) {
    uint32_t bpi = (fmt == IndexFormat::UInt16) ? 2u : 4u;
    if (!Buffer::create(device, BufferType::Index, index_count * bpi, usage,
                        data, bpi, debug_name))
        return false;
    format_ = fmt;
    return true;
}

bool IndexBuffer::create_from(GraphicDevice* device, const uint16_t* idx, uint32_t count,
                              ResourceUsage usage, const char* name) {
    return create(device, count, IndexFormat::UInt16, idx, usage, name);
}

bool IndexBuffer::create_from(GraphicDevice* device, const uint32_t* idx, uint32_t count,
                              ResourceUsage usage, const char* name) {
    return create(device, count, IndexFormat::UInt32, idx, usage, name);
}

bool ConstBuffer::create(GraphicDevice* device, uint32_t size, ResourceUsage usage,
                         const void* data, const char* debug_name) {
    return Buffer::create(device, BufferType::Uniform, size, usage, data, 0, debug_name);
}

bool StorageBuffer::create(GraphicDevice* device, uint32_t size, uint32_t stride,
                           ResourceUsage usage, const void* data, const char* debug_name) {
    return Buffer::create(device, BufferType::Storage, size, usage, data, stride, debug_name);
}

bool IndirectBuffer::create(GraphicDevice* device, uint32_t size, ResourceUsage usage,
                            const void* data, const char* debug_name) {
    return Buffer::create(device, BufferType::Indirect, size, usage, data, 0, debug_name);
}

} // namespace gfx
} // namespace window
