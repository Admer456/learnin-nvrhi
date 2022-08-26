
#pragma once

#include "Precompiled.hpp"

#include <iostream>

#include "DeviceManager.hpp"
#include <nvrhi/utils.h>

#include <glm/glm.hpp>

struct SDL_Window;

inline bool Check( void* ptr, const char* message )
{
	if ( nullptr == ptr )
	{
		std::cout << "FATAL ERROR: " << message << std::endl;
		return false;
	}

	return true;
}

namespace Renderer
{
	extern nvrhi::IDevice* Device;
	extern nvrhi::CommandListHandle CommandList;
	
	namespace Scene
	{
		extern nvrhi::BufferHandle ConstantBufferEntity;
		extern nvrhi::SamplerHandle DiffuseTextureSampler;
		extern nvrhi::BindingLayoutHandle BindingLayoutGlobal;
		extern nvrhi::BindingLayoutHandle BindingLayoutEntity;
	}
}

namespace Texture
{
	struct TextureData
	{
		void Init( const char* fileName );

		TextureData() = default;
		TextureData( TextureData&& texture ) noexcept;
		~TextureData();

		TextureData& operator=( TextureData&& texture ) noexcept
		{
			width = texture.width;
			height = texture.height;
			data = texture.data;
			components = texture.components;
			bytesPerComponent = texture.bytesPerComponent;

			texture.data = nullptr;

			return *this;
		}

		uint32_t GetNvrhiRowBytes() const;
		nvrhi::Format GetNvrhiFormat() const;

		operator bool()
		{
			return nullptr != data;
		}

		uint16_t width{};
		uint16_t height{};
		uint8_t* data{};

		// RGB vs. RGBA
		uint8_t components{};
		// 8 bpp vs. 16 bpp
		uint8_t bytesPerComponent{};
	};

	extern nvrhi::static_vector<TextureData, 32U> TextureDatas;
	extern nvrhi::static_vector<nvrhi::TextureHandle, 32U> TextureObjects;
	
	int32_t FindOrCreateMaterial( const char* materialName );
}

namespace Model
{
	struct DrawVertex
	{
		glm::vec3 vertexPosition;
		glm::vec3 vertexNormal;
		glm::vec2 vertexTextureCoords;
		glm::vec4 vertexColour;
	};

	struct DrawSurface
	{
		std::string materialName{};

		std::vector<DrawVertex> vertexData{};
		std::vector<uint32_t> vertexIndices{};

		uint32_t IndexBytes() const;
		const uint32_t* GetIndexData() const;
		uint32_t VertexBytes() const;
		const DrawVertex* GetVertexData() const;
	};

	struct DrawMesh
	{
		std::vector<DrawSurface> surfaces{};
	};

	struct RenderSurface
	{
		RenderSurface() = default;
		RenderSurface( RenderSurface&& surface ) noexcept = default;
		RenderSurface& operator=( RenderSurface&& surface ) noexcept = default;

		// This would normally be a reference to a material
		int32_t textureObjectHandle{};
		int32_t numIndices{};
		int32_t numVertices{};
		// Contains a reference to a texture object
		nvrhi::BindingSetHandle bindingSet;
		nvrhi::BufferHandle vertexBuffer;
		nvrhi::BufferHandle indexBuffer;
	};

	struct RenderModel
	{
		RenderModel() = default;
		RenderModel( RenderModel&& surface ) noexcept = default;
		RenderModel& operator=( RenderModel&& surface ) noexcept = default;

		// Typically the filename
		std::string name;
		std::vector<RenderSurface> surfaces;
	};

	extern std::vector<RenderModel> RenderModels;

	template<typename bufferDataType>
	nvrhi::BufferHandle CreateBufferWithData( const std::vector<bufferDataType>& data, bool isVertexBuffer, const char* debugName = nullptr )
	{
		if ( nullptr == debugName )
		{
			debugName = isVertexBuffer ? "My vertex buffer" : "My index buffer";
		}

		nvrhi::BufferDesc bufferDesc;
		bufferDesc.byteSize = data.size() * sizeof( bufferDataType );
		bufferDesc.debugName = debugName;
		bufferDesc.isVertexBuffer = isVertexBuffer;
		bufferDesc.isIndexBuffer = !isVertexBuffer;
		bufferDesc.initialState = nvrhi::ResourceStates::CopyDest;
		auto bufferObject = Renderer::Device->createBuffer( bufferDesc );

		::Renderer::CommandList->open();
		::Renderer::CommandList->beginTrackingBufferState( bufferObject, nvrhi::ResourceStates::CopyDest );
		::Renderer::CommandList->writeBuffer( bufferObject, data.data(), bufferDesc.byteSize );
		::Renderer::CommandList->setPermanentBufferState( bufferObject, isVertexBuffer ? nvrhi::ResourceStates::VertexBuffer : nvrhi::ResourceStates::IndexBuffer );
		::Renderer::CommandList->close();

		Renderer::Device->executeCommandList( ::Renderer::CommandList );

		return bufferObject;
	}

	int32_t LoadRenderModelFromGltf( const char* fileName );

	// Fullscreen quad used to render framebuffers
	namespace ScreenQuad
	{
		// Format:
		// vec2 pos
		// vec2 texcoord
		inline const std::vector<float> Vertices =
		{
			-1.0f, -1.0f,
			0.0f, 1.0f,

			1.0f, -1.0f,
			1.0f, 1.0f,

			1.0f, 1.0f,
			1.0f, 0.0f,

			-1.0f, 1.0f,
			0.0f, 0.0f
		};

		inline const std::vector<uint32_t> Indices =
		{
			0, 1, 2,
			2, 3, 0
		};
	}

	namespace Pentagon
	{
		inline const std::vector<DrawVertex> Vertices =
		{
			{
				{ 0.0f, 0.5f, 0.0f },
				{ 0.0f, 0.0f, 1.0f },
				{ 0.0f, 0.5f },
				{ 1.0f, 0.0f, 0.0f, 1.0f }
			},
			{
				{ 0.5f, 0.2f, 0.0f },
				{ 0.0f, 0.0f, 1.0f },
				{ 0.5f, 0.2f },
				{ 0.0f, 1.0f, 0.0f, 1.0f }
			},
			{
				{ 0.3f, -0.4f, 0.0f },
				{ 0.0f, 0.0f, 1.0f },
				{ 0.3f, -0.4f },
				{ 0.0f, 0.0f, 1.0f, 1.0f }
			},
			{
				{ -0.3f, -0.4f, 0.0f },
				{ 0.0f, 0.0f, 1.0f  },
				{ -0.3f, -0.4f },
				{ 1.0f, 0.0f, 1.0f, 1.0f }
			},
			{
				{ -0.5f, 0.2f, 0.0f },
				{ 0.0f, 0.0f, 1.0f },
				{ -0.5f, 0.2f },
				{ 1.0f, 0.6f, 0.0f, 1.0f }
			}
		};

		inline const std::vector<uint32_t> Indices =
		{
			0, 1, 2,
			0, 2, 3,
			0, 3, 4
		};
	}
}

namespace Shader
{
	using ShaderBinary = std::vector<uint8_t>;
	bool LoadShaderBinary( const char* fileName, ShaderBinary& outShaderBinary );
}

namespace System
{
	bool GetWindowFormat( SDL_Window* window, nvrhi::Format format );
	void PopulateWindowData( SDL_Window* window, nvrhi::app::WindowSurfaceData& outData );
	void GetVulkanExtensionsForSDL( std::vector<std::string>& frameworkExtensions );
}
