// SPDX-License-Identifier: MIT

#include "Common.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Texture
{
	void TextureData::Init( const char* fileName )
	{
		int x, y, comps;

		// Try out BMP, JPG, JPEG, TGA and PNG
		constexpr const char* ImageTypes[] =
		{
			".bmp", ".jpg", ".jpeg", ".tga", ".png" };

		data = stbi_load( fileName, &x, &y, &comps, 4 );

		if ( nullptr == data )
		{
			auto path = std::filesystem::path( fileName );
			if ( path.has_extension() )
			{
				path = path.parent_path() / path.stem();
			}
			else
			{
				path = path.parent_path() / path.filename();
			}

			std::string pathStr = path.string();

			for ( const auto& imageType : ImageTypes )
			{
				std::string imagePath = pathStr + imageType;
				if ( data = stbi_load( imagePath.c_str(), &x, &y, &comps, 4 ) )
				{
					break;
				}
			}
		}

		if ( nullptr == data )
		{
			return;
		}

		width = x;
		height = y;
		components = 4;
		bytesPerComponent = 1;
	}

	TextureData::TextureData( TextureData&& texture ) noexcept
	{
		*this = std::move( texture );
	}

	TextureData::~TextureData()
	{
		if ( nullptr != data )
		{
			delete[] data;
			data = nullptr;
		}
	}

	// TODO: handle compression?
	uint32_t TextureData::GetNvrhiRowBytes() const
	{
		return width * components * bytesPerComponent;
	}

	nvrhi::Format TextureData::GetNvrhiFormat() const
	{
		using namespace nvrhi;

		switch ( components )
		{
		case 1:
			return Format::R8_UNORM;
		case 2:
			return Format::RG8_UNORM;
		}

		// RGB8_UNORM does not exist in NVRHI, should probably PR that...

		return Format::RGBA8_UNORM;
	}

	nvrhi::static_vector<TextureData, 32U> TextureDatas;
	nvrhi::static_vector<nvrhi::TextureHandle, 32U> TextureObjects;

	int32_t FindOrCreateMaterial( const char* materialName )
	{
		TextureData textureData;

		if ( nullptr != materialName )
		{
			textureData.Init( materialName );

			if ( !textureData )
			{
				return -1;
			}
		}
		else
		{
			textureData.width = 16;
			textureData.height = 16;
			textureData.components = 4;
			textureData.bytesPerComponent = 1;
			const int stride = 16 * 4;

			textureData.data = new uint8_t[16 * 16 * 4];

			for ( int y = 0; y < 16; y++ )
			{
				for ( int x = 0; x < 16; x++ )
				{
					uint8_t* pixel = &textureData.data[y * stride + x * 4];
					pixel[0] = 50;
					pixel[1] = 60;
					pixel[2] = 50;
					pixel[3] = 255;

					if ( !(y % 4) || !(x % 4) )
					{
						pixel[0] = 240;
						pixel[1] = 240;
						pixel[2] = 240;
					}
					else
					{
						pixel[0] -= 40.0f * std::sin( x / 5.0f );
						pixel[1] += 50.0f * std::sin( y / 5.0f );
						pixel[2] += 50.0f * std::sin( (x + y) / 5.0f );
					}
				}
			}
		}

		// Diffuse texture
		auto& textureDesc = nvrhi::TextureDesc()
			.setDimension( nvrhi::TextureDimension::Texture2D )
			//.setKeepInitialState( true )
			//.setInitialState( nvrhi::ResourceStates::Common | nvrhi::ResourceStates::ShaderResource )
			.setWidth( textureData.width )
			.setHeight( textureData.height )
			.setFormat( textureData.GetNvrhiFormat() );

		textureDesc.debugName = nullptr == materialName ? "default" : materialName;

		auto textureObject = Renderer::Device->createTexture( textureDesc );

		Renderer::CommandList->open();
		Renderer::CommandList->beginTrackingTextureState( textureObject, nvrhi::AllSubresources, nvrhi::ResourceStates::Common );
		Renderer::CommandList->writeTexture( textureObject, 0, 0, textureData.data, textureData.GetNvrhiRowBytes() );
		Renderer::CommandList->setPermanentTextureState( textureObject, nvrhi::ResourceStates::ShaderResource );
		Renderer::CommandList->close();

		Renderer::Device->executeCommandList( Renderer::CommandList );

		TextureDatas.push_back( std::move( textureData ) );
		TextureObjects.push_back( std::move( textureObject ) );

		return TextureObjects.size() - 1;
	}
}
