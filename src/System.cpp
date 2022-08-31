// SPDX-License-Identifier: MIT

#include "Common.hpp"
#include "DeviceManager.hpp"

#include "SDL_video.h"
#include "SDL_syswm.h"

namespace System
{
	bool GetWindowFormat( SDL_Window* window, nvrhi::Format format )
	{
		static const std::pair<uint32_t, nvrhi::Format> sdlFormatPairs[]
		{
			{ SDL_PIXELFORMAT_RGBA8888, nvrhi::Format::RGBA8_UNORM },
			{ SDL_PIXELFORMAT_BGRA8888, nvrhi::Format::BGRA8_UNORM },
			{ SDL_PIXELFORMAT_RGBA8888, nvrhi::Format::SRGBA8_UNORM },
			{ SDL_PIXELFORMAT_BGRA8888, nvrhi::Format::SBGRA8_UNORM },
		};

		for ( const auto& formatPair : sdlFormatPairs )
		{
			if ( format == formatPair.second )
			{
				std::cout << "Found window format: " << SDL_GetPixelFormatName( formatPair.first ) << std::endl;

				SDL_DisplayMode dm;
				SDL_GetWindowDisplayMode( window, &dm );
				dm.format = formatPair.first;
				if ( dm.refresh_rate < 60 )
				{
					std::cout << "Refresh rate was set to " << dm.refresh_rate << " Hz, setting to 60..." << std::endl;
					dm.refresh_rate = 60;
				}

				if ( SDL_SetWindowDisplayMode( window, &dm ) )
				{
					std::cout << "Error setting the display mode: " << SDL_GetError() << std::endl;
					return false;
				}

				// Might be a bit of a hack, but, we shall see:tm:
				SDL_HideWindow( window );
				SDL_ShowWindow( window );

				return true;
			}
		}

		std::cout << "Unknown window format" << std::endl;
		return false;
	}

	void PopulateWindowData( SDL_Window* window, nvrhi::app::WindowSurfaceData& outData )
	{
		SDL_SysWMinfo info;
		SDL_VERSION( &info.version );
		SDL_GetWindowWMInfo( window, &info );

#if VK_USE_PLATFORM_WIN32_KHR
		outData.hInstance = info.info.win.hinstance;
		outData.hWindow = info.info.win.window;
#elif VK_USE_PLATFORM_XLIB_KHR
		outData.display = info.info.x11.display;
		outData.window = info.info.x11.window;
#endif
	}

	// This is truly not necessary, could just use SDL_Vulkan_GetInstanceExtensions, 
	// but I'm doing this for the sake of GoldSRC Half-Life support
	void GetVulkanExtensionsForSDL( std::vector<std::string>&frameworkExtensions )
	{
#if VK_USE_PLATFORM_WIN32_KHR
		constexpr const char* VulkanExtensions_Win32[]
		{
			VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME
		};

		for ( const char* ext : VulkanExtensions_Win32 )
			frameworkExtensions.push_back( ext );
#elif VK_USE_PLATFORM_XLIB_KHR
		constexpr const char* VulkanExtensions_Linux[]
		{
			VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME
		};

		for ( const char* ext : VulkanExtensions_Linux )
			frameworkExtensions.push_back( ext );
#endif
	}
}
