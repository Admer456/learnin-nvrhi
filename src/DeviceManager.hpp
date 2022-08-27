/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// Adapted from Donut's DeviceManager:
// https://github.com/NVIDIAGameWorks/donut/blob/main/include/donut/app/DeviceManager.h

#pragma once

#if USE_DX11 || USE_DX12
#include <DXGI.h>
#endif

#if USE_DX11
#include <d3d11.h>
#endif

#if USE_DX12
#include <d3d12.h>
#endif

#if USE_VK
#include <nvrhi/vulkan.h>
#endif

#include <nvrhi/nvrhi.h>

#include <list>
#include <functional>

namespace nvrhi::app
{
    namespace log
    {
        void error( const char* string );
        void message( nvrhi::MessageSeverity severity, const char* string );
    }

    struct FormatInfo
    {
        nvrhi::Format format;
        uint32_t redBits;
        uint32_t greenBits;
        uint32_t blueBits;
        uint32_t alphaBits;
        uint32_t depthBits;
        uint32_t stencilBits;
    };

    // You'll need to set up your window's format bits according to this
    constexpr FormatInfo FormatInfos[]
    {
        { nvrhi::Format::UNKNOWN,            0,  0,  0,  0,  0,  0, },
        { nvrhi::Format::R8_UINT,            8,  0,  0,  0,  0,  0, },
        { nvrhi::Format::RG8_UINT,           8,  8,  0,  0,  0,  0, },
        { nvrhi::Format::RG8_UNORM,          8,  8,  0,  0,  0,  0, },
        { nvrhi::Format::R16_UINT,          16,  0,  0,  0,  0,  0, },
        { nvrhi::Format::R16_UNORM,         16,  0,  0,  0,  0,  0, },
        { nvrhi::Format::R16_FLOAT,         16,  0,  0,  0,  0,  0, },
        { nvrhi::Format::RGBA8_UNORM,        8,  8,  8,  8,  0,  0, },
        { nvrhi::Format::RGBA8_SNORM,        8,  8,  8,  8,  0,  0, },
        { nvrhi::Format::BGRA8_UNORM,        8,  8,  8,  8,  0,  0, },
        { nvrhi::Format::SRGBA8_UNORM,       8,  8,  8,  8,  0,  0, },
        { nvrhi::Format::SBGRA8_UNORM,       8,  8,  8,  8,  0,  0, },
        { nvrhi::Format::R10G10B10A2_UNORM, 10, 10, 10,  2,  0,  0, },
        { nvrhi::Format::R11G11B10_FLOAT,   11, 11, 10,  0,  0,  0, },
        { nvrhi::Format::RG16_UINT,         16, 16,  0,  0,  0,  0, },
        { nvrhi::Format::RG16_FLOAT,        16, 16,  0,  0,  0,  0, },
        { nvrhi::Format::R32_UINT,          32,  0,  0,  0,  0,  0, },
        { nvrhi::Format::R32_FLOAT,         32,  0,  0,  0,  0,  0, },
        { nvrhi::Format::RGBA16_FLOAT,      16, 16, 16, 16,  0,  0, },
        { nvrhi::Format::RGBA16_UNORM,      16, 16, 16, 16,  0,  0, },
        { nvrhi::Format::RGBA16_SNORM,      16, 16, 16, 16,  0,  0, },
        { nvrhi::Format::RG32_UINT,         32, 32,  0,  0,  0,  0, },
        { nvrhi::Format::RG32_FLOAT,        32, 32,  0,  0,  0,  0, },
        { nvrhi::Format::RGB32_UINT,        32, 32, 32,  0,  0,  0, },
        { nvrhi::Format::RGB32_FLOAT,       32, 32, 32,  0,  0,  0, },
        { nvrhi::Format::RGBA32_UINT,       32, 32, 32, 32,  0,  0, },
        { nvrhi::Format::RGBA32_FLOAT,      32, 32, 32, 32,  0,  0, },
    };

    struct WindowSurfaceData
    {
#if VK_USE_PLATFORM_WIN32_KHR
        HINSTANCE hInstance{};
        HWND hWindow{};
#elif VK_USE_PLATFORM_XLIB_KHR
        Display* display{ nullptr };
        Window window{};
#endif
    };

    struct DeviceCreationParameters
    {
        nvrhi::IMessageCallback* messageCallback = nullptr;

        std::vector<std::string> frameworkExtensions;

        WindowSurfaceData windowSurfaceData;

        bool startMaximized = false;
        bool startFullscreen = false;
        bool allowModeSwitch = true;

        uint32_t backBufferWidth = 1280;
        uint32_t backBufferHeight = 720;
        uint32_t refreshRate = 0;
        uint32_t swapChainBufferCount = 3;
        nvrhi::Format swapChainFormat = nvrhi::Format::SRGBA8_UNORM;
        uint32_t swapChainSampleCount = 1;
        uint32_t swapChainSampleQuality = 0;
        uint32_t maxFramesInFlight = 2;
        bool enableDebugRuntime = false;
        bool enableNvrhiValidationLayer = false;
        bool vsyncEnabled = false;
        bool enableRayTracingExtensions = false; // for vulkan
        bool enableComputeQueue = false;
        bool enableCopyQueue = false;

        // Severity of the information log messages from the device manager, like the device name or enabled extensions.
        nvrhi::MessageSeverity infoLogSeverity = nvrhi::MessageSeverity::Info;

#if USE_DX11 || USE_DX12
        // Adapter to create the device on. Setting this to non-null overrides adapterNameSubstring.
        // If device creation fails on the specified adapter, it will *not* try any other adapters.
        IDXGIAdapter* adapter = nullptr;
#endif

        // For use in the case of multiple adapters; only effective if 'adapter' is null. If this is non-null, device creation will try to match
        // the given string against an adapter name.  If the specified string exists as a sub-string of the
        // adapter name, the device and window will be created on that adapter.  Case sensitive.
        std::wstring adapterNameSubstring = L"";

#if USE_DX11 || USE_DX12
        DXGI_USAGE swapChainUsage = DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_RENDER_TARGET_OUTPUT;
        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
#endif

#if USE_VK
        std::vector<std::string> requiredVulkanInstanceExtensions;
        std::vector<std::string> requiredVulkanDeviceExtensions;
        std::vector<std::string> requiredVulkanLayers;
        std::vector<std::string> optionalVulkanInstanceExtensions;
        std::vector<std::string> optionalVulkanDeviceExtensions;
        std::vector<std::string> optionalVulkanLayers;
        std::vector<size_t> ignoredVulkanValidationMessageLocations;
        std::function<void(vk::DeviceCreateInfo&)> deviceCreateInfoCallback;
#endif
    };

    class IRenderPass;

    class DeviceManager
    {
    public:
        static DeviceManager* Create(nvrhi::GraphicsAPI api);

        bool CreateWindowDeviceAndSwapChain(const DeviceCreationParameters& params);

        void UpdateWindowSize( int width, int height );

        // returns the size of the window in screen coordinates
        void GetWindowDimensions(int& width, int& height);
        // returns the screen coordinate to pixel coordinate scale factor
        void GetDPIScaleInfo(float& x, float& y) const
        {
            x = m_DPIScaleFactorX;
            y = m_DPIScaleFactorY;
        }

    protected:
        bool m_windowVisible = false;

        DeviceCreationParameters m_DeviceParams;
        void* m_Window = nullptr;
        // set to true if running on NV GPU
        bool m_IsNvidia = false;
        // timestamp in seconds for the previous frame
        double m_PreviousFrameTimestamp = 0.0;
        // current DPI scale info (updated when window moves)
        float m_DPIScaleFactorX = 1.f;
        float m_DPIScaleFactorY = 1.f;
        bool m_RequestedVSync = false;

        double m_AverageFrameTime = 0.0;
        double m_AverageTimeUpdateInterval = 0.5;
        double m_FrameTimeSum = 0.0;
        int m_NumberOfAccumulatedFrames = 0;

        uint32_t m_FrameIndex = 0;

        std::vector<nvrhi::FramebufferHandle> m_SwapChainFramebuffers;

        DeviceManager() = default;

        void BackBufferResizing();
        void BackBufferResized();

        // device-specific methods
        virtual bool CreateDeviceAndSwapChain() = 0;
        virtual void DestroyDeviceAndSwapChain() = 0;
        virtual void ResizeSwapChain() = 0;
    public:
        virtual void BeginFrame() = 0;
        virtual void Present() = 0;

        [[nodiscard]] virtual nvrhi::IDevice *GetDevice() const = 0;
        [[nodiscard]] virtual const char *GetRendererString() const = 0;
        [[nodiscard]] virtual nvrhi::GraphicsAPI GetGraphicsAPI() const = 0;

        const DeviceCreationParameters& GetDeviceParams();
        [[nodiscard]] double GetAverageFrameTimeSeconds() const { return m_AverageFrameTime; }
        [[nodiscard]] double GetPreviousFrameTimestamp() const { return m_PreviousFrameTimestamp; }
        void SetFrameTimeUpdateInterval(double seconds) { m_AverageTimeUpdateInterval = seconds; }
        [[nodiscard]] bool IsVsyncEnabled() const { return m_DeviceParams.vsyncEnabled; }
        virtual void SetVsyncEnabled(bool enabled) { m_RequestedVSync = enabled; /* will be processed later */ }
        virtual void ReportLiveObjects() {}

        [[nodiscard]] void* GetWindow() const { return m_Window; }
        [[nodiscard]] uint32_t GetFrameIndex() const { return m_FrameIndex; }

        virtual nvrhi::ITexture* GetCurrentBackBuffer() = 0;
        virtual nvrhi::ITexture* GetBackBuffer(uint32_t index) = 0;
        virtual uint32_t GetCurrentBackBufferIndex() = 0;
        virtual uint32_t GetBackBufferCount() = 0;
        nvrhi::IFramebuffer* GetCurrentFramebuffer();
        nvrhi::IFramebuffer* GetFramebuffer(uint32_t index);

        void Shutdown();
        virtual ~DeviceManager() = default;

        virtual bool IsVulkanInstanceExtensionEnabled(const char* extensionName) const { return false; }
        virtual bool IsVulkanDeviceExtensionEnabled(const char* extensionName) const { return false; }
        virtual bool IsVulkanLayerEnabled(const char* layerName) const { return false; }
        virtual void GetEnabledVulkanInstanceExtensions(std::vector<std::string>& extensions) const { }
        virtual void GetEnabledVulkanDeviceExtensions(std::vector<std::string>& extensions) const { }
        virtual void GetEnabledVulkanLayers(std::vector<std::string>& layers) const { }

    private:
        static DeviceManager* CreateD3D11();
        static DeviceManager* CreateD3D12();
        static DeviceManager* CreateVK();
    };

    class IRenderPass
    {
    private:
        DeviceManager* m_DeviceManager;

    public:
        explicit IRenderPass(DeviceManager* deviceManager)
            : m_DeviceManager(deviceManager)
        { }

        virtual ~IRenderPass() = default;

        virtual void Render(nvrhi::IFramebuffer* framebuffer) { }
        virtual void Animate(float fElapsedTimeSeconds) { }
        virtual void BackBufferResizing() { }
        virtual void BackBufferResized(const uint32_t width, const uint32_t height, const uint32_t sampleCount) { }

        [[nodiscard]] DeviceManager* GetDeviceManager() const { return m_DeviceManager; }
        [[nodiscard]] nvrhi::IDevice* GetDevice() const { return m_DeviceManager->GetDevice(); }
        [[nodiscard]] uint32_t GetFrameIndex() const { return m_DeviceManager->GetFrameIndex(); }
    };
}
