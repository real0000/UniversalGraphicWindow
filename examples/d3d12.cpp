/*
 * d3d12.cpp - Direct3D 12 window example (Windows only)
 */

#include "window.hpp"

#ifdef WINDOW_PLATFORM_WIN32

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cmath>

int main() {
    window::Config config;
    config.title = "Direct3D 12 Example";
    config.width = 800;
    config.height = 600;
    config.graphics_api = window::GraphicsAPI::D3D12;
    config.d3d.debug_layer = true;

    window::Result result;
    window::Window* win = window::Window::create(config, &result);

    if (result != window::Result::Success) {
        printf("Failed to create window: %s\n", window::result_to_string(result));
        return 1;
    }

    const window::GraphicsContext* ctx = win->get_graphics_context();
    ID3D12Device* device = static_cast<ID3D12Device*>(ctx->d3d12.device);
    ID3D12CommandQueue* command_queue = static_cast<ID3D12CommandQueue*>(ctx->d3d12.command_queue);
    IDXGISwapChain4* swap_chain = static_cast<IDXGISwapChain4*>(ctx->d3d12.swap_chain);
    ID3D12DescriptorHeap* rtv_heap = static_cast<ID3D12DescriptorHeap*>(ctx->d3d12.rtv_heap);

    printf("Direct3D 12 context created!\n");
    printf("Frame count: %u\n", ctx->d3d12.frame_count);
    printf("D3D12 Ultimate: %s\n", ctx->d3d12.supports_ultimate ? "Yes" : "No");

    // Create command allocator and list
    ID3D12CommandAllocator* command_allocator;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    IID_PPV_ARGS(&command_allocator));

    ID3D12GraphicsCommandList* command_list;
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                              command_allocator, nullptr,
                              IID_PPV_ARGS(&command_list));
    command_list->Close();

    // Create fence for synchronization
    ID3D12Fence* fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    UINT64 fence_value = 1;
    HANDLE fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // Get RTV descriptor size
    UINT rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create render targets
    ID3D12Resource* render_targets[2];
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 2; i++) {
        swap_chain->GetBuffer(i, IID_PPV_ARGS(&render_targets[i]));
        device->CreateRenderTargetView(render_targets[i], nullptr, rtv_handle);
        rtv_handle.ptr += rtv_descriptor_size;
    }

    float time = 0.0f;

    while (!win->should_close()) {
        win->poll_events();

        UINT frame_index = swap_chain->GetCurrentBackBufferIndex();

        // Reset command allocator and list
        command_allocator->Reset();
        command_list->Reset(command_allocator, nullptr);

        // Transition to render target state
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = render_targets[frame_index];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1, &barrier);

        // Clear render target
        float r = (sinf(time) + 1.0f) * 0.5f;
        float g = (sinf(time + 2.0f) + 1.0f) * 0.5f;
        float b = (sinf(time + 4.0f) + 1.0f) * 0.5f;
        float clear_color[4] = { r * 0.3f, g * 0.3f, b * 0.3f, 1.0f };

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += frame_index * rtv_descriptor_size;
        command_list->ClearRenderTargetView(rtv, clear_color, 0, nullptr);

        // Transition to present state
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        command_list->ResourceBarrier(1, &barrier);

        command_list->Close();

        // Execute command list
        ID3D12CommandList* lists[] = { command_list };
        command_queue->ExecuteCommandLists(1, lists);

        // Present
        win->present();

        // Wait for GPU
        command_queue->Signal(fence, fence_value);
        if (fence->GetCompletedValue() < fence_value) {
            fence->SetEventOnCompletion(fence_value, fence_event);
            WaitForSingleObject(fence_event, INFINITE);
        }
        fence_value++;

        time += 0.016f;
    }

    // Cleanup
    CloseHandle(fence_event);
    fence->Release();
    command_list->Release();
    command_allocator->Release();
    for (UINT i = 0; i < 2; i++) {
        render_targets[i]->Release();
    }

    win->destroy();
    return 0;
}

#else

int main() {
    printf("D3D12 example is only available on Windows.\n");
    return 0;
}

#endif
