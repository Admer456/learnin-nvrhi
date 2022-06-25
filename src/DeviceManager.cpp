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

/*
License for glfw

Copyright (c) 2002-2006 Marcus Geelnard

Copyright (c) 2006-2019 Camilla Lowy

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would
   be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
   distribution.
*/

#include "DeviceManager.hpp"
#include <nvrhi/utils.h>

#include <cstdio>
#include <iomanip>
#include <thread>
#include <sstream>

#if USE_DX11
#include <d3d11.h>
#endif

#if USE_DX12
#include <d3d12.h>
#endif

#ifdef _WINDOWS
#include <ShellScalingApi.h>
#pragma comment(lib, "shcore.lib")
#endif

#include "Text/Format.hpp"
#include <iostream>

using namespace nvrhi::app;

void log::error( const char* string )
{
    return log::message( nvrhi::MessageSeverity::Error, string );
}

void log::message( nvrhi::MessageSeverity severity, const char* string )
{
    const char* severityString = [&severity]()
    {
        using nvrhi::MessageSeverity;
        switch ( severity )
        {
        case MessageSeverity::Info: return "[INFO]";
        case MessageSeverity::Warning: return "[WARNING]";
        case MessageSeverity::Error: return "[ERROR]";
        case MessageSeverity::Fatal: return "[### FATAL ERROR ###]";
        default: return "[unknown]";
        }
    }();

    std::cout << "NVRHI::" << severityString << " " << string << std::endl;

    if ( severity == nvrhi::MessageSeverity::Fatal )
    {
        std::cout << "Fatal error encountered, look above ^" << std::endl;
        std::cout << "=====================================" << std::endl;
    }
}

bool DeviceManager::CreateWindowDeviceAndSwapChain(const DeviceCreationParameters& params)
{
    this->m_DeviceParams = params;
    m_RequestedVSync = params.vsyncEnabled;

    if (!CreateDeviceAndSwapChain())
        return false;

    // reset the back buffer size state to enforce a resize event
    m_DeviceParams.backBufferWidth = 0;
    m_DeviceParams.backBufferHeight = 0;

    UpdateWindowSize( params.backBufferWidth, params.backBufferHeight );

    return true;
}

void DeviceManager::BackBufferResizing()
{
    m_SwapChainFramebuffers.clear();
}

void DeviceManager::BackBufferResized()
{
    uint32_t backBufferCount = GetBackBufferCount();
    m_SwapChainFramebuffers.resize(backBufferCount);
    for (uint32_t index = 0; index < backBufferCount; index++)
    {
        m_SwapChainFramebuffers[index] = GetDevice()->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(GetBackBuffer(index)));
    }
}

void DeviceManager::GetWindowDimensions(int& width, int& height)
{
    width = m_DeviceParams.backBufferWidth;
    height = m_DeviceParams.backBufferHeight;
}

const DeviceCreationParameters& DeviceManager::GetDeviceParams()
{
    return m_DeviceParams;
}

void DeviceManager::UpdateWindowSize( int width, int height )
{
    if (width == 0 || height == 0)
    {
        // window is minimized
        m_windowVisible = false;
        return;
    }

    m_windowVisible = true;

    if (int(m_DeviceParams.backBufferWidth) != width || 
        int(m_DeviceParams.backBufferHeight) != height ||
        (m_DeviceParams.vsyncEnabled != m_RequestedVSync && GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN))
    {
        // window is not minimized, and the size has changed

        BackBufferResizing();

        m_DeviceParams.backBufferWidth = width;
        m_DeviceParams.backBufferHeight = height;
        m_DeviceParams.vsyncEnabled = m_RequestedVSync;

        ResizeSwapChain();
        BackBufferResized();
    }

    m_DeviceParams.vsyncEnabled = m_RequestedVSync;
}

void DeviceManager::Shutdown()
{
    m_SwapChainFramebuffers.clear();

    DestroyDeviceAndSwapChain();
}

nvrhi::IFramebuffer* nvrhi::app::DeviceManager::GetCurrentFramebuffer()
{
    return GetFramebuffer(GetCurrentBackBufferIndex());
}

nvrhi::IFramebuffer* nvrhi::app::DeviceManager::GetFramebuffer(uint32_t index)
{
    if (index < m_SwapChainFramebuffers.size())
        return m_SwapChainFramebuffers[index];

    return nullptr;
}

nvrhi::app::DeviceManager* nvrhi::app::DeviceManager::Create(nvrhi::GraphicsAPI api)
{
    switch (api)
    {
#if USE_DX11
    case nvrhi::GraphicsAPI::D3D11:
        return CreateD3D11();
#endif
#if USE_DX12
    case nvrhi::GraphicsAPI::D3D12:
        return CreateD3D12();
#endif
#if USE_VK
    case nvrhi::GraphicsAPI::VULKAN:
        return CreateVK();
#endif
    default:
        log::error(adm::format("DeviceManager::Create: Unsupported Graphics API (%d)", api));
        return nullptr;
    }
}
