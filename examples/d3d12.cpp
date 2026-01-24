/*
 * d3d12.cpp - Direct3D 12 window example (Windows only)
 */

#include "window.hpp"

#ifdef WINDOW_PLATFORM_WIN32

#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cmath>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

int main() {
    window::Config config;
    config.title = "Direct3D 12 Example";
    config.width = 800;
    config.height = 600;
    config.backend = window::Backend::D3D12;

    window::Result result;
    window::Window* win = window::Window::create(config, &result);

    if (result != window::Result::Success) {
        printf("Failed to create window: %s\n", window::result_to_string(result));
        return 1;
    }

    window::Graphics* gfx = win->graphics();

    printf("Direct3D 12 context created!\n");
    printf("Backend: %s\n", gfx->get_backend_name());
    printf("Device: %s\n", gfx->get_device_name());

    // Get native D3D12 handles
    ID3D12Device* device = static_cast<ID3D12Device*>(gfx->native_device());
    ID3D12CommandQueue* command_queue = static_cast<ID3D12CommandQueue*>(gfx->native_context());
    IDXGISwapChain4* swap_chain = static_cast<IDXGISwapChain4*>(gfx->native_swapchain());

    // Create command allocator and list
    ID3D12CommandAllocator* command_allocator = nullptr;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    IID_PPV_ARGS(&command_allocator));

    ID3D12GraphicsCommandList* command_list = nullptr;
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                              command_allocator, nullptr,
                              IID_PPV_ARGS(&command_list));
    command_list->Close();

    // Create fence for synchronization
    ID3D12Fence* fence = nullptr;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    UINT64 fence_value = 1;
    HANDLE fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // Create RTV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.NumDescriptors = 2;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap));

    UINT rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create render targets
    ID3D12Resource* render_targets[2] = {};
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
        swap_chain->Present(1, 0);

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
    if (fence) fence->Release();
    if (command_list) command_list->Release();
    if (command_allocator) command_allocator->Release();
    if (rtv_heap) rtv_heap->Release();
    for (UINT i = 0; i < 2; i++) {
        if (render_targets[i]) render_targets[i]->Release();
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
