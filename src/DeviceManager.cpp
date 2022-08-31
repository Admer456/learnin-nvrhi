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

// Adapted from Donut's DeviceManager
// https://github.com/NVIDIAGameWorks/donut/blob/main/src/app/DeviceManager.cpp

#include "DeviceManager.hpp"
#include <nvrhi/utils.h>

#include <cstdio>
#include <iomanip>
#include <thread>
#include <sstream>
using namespace std::string_literals;

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

void DeviceManager::Message( const char* message, nvrhi::MessageSeverity severity )
{
    if ( nullptr != m_DeviceParams.messageCallback )
    {
        auto*& printer = m_DeviceParams.messageCallback;
        printer->message( severity, message );
    }
}

void DeviceManager::Error( const char* message )
{
    return Message( message, nvrhi::MessageSeverity::Error );
}

void DeviceManager::Fatal( const char* message )
{
    return Message( message, nvrhi::MessageSeverity::Fatal );
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

namespace std
{
    std::string to_string( nvrhi::GraphicsAPI api )
    {
        switch ( api )
        {
        case nvrhi::GraphicsAPI::D3D11: return "D3D11";
        case nvrhi::GraphicsAPI::D3D12: return "D3D12";
        case nvrhi::GraphicsAPI::VULKAN: return "VULKAN";
        }

        return "unknown";
    }
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
        std::cout << "DeviceManager::Create: Unsupported graphics API: " << std::to_string( api ) << std::endl;
        return nullptr;
    }
}
