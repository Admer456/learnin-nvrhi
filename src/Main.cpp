
#include <iostream>
#include <fstream>
#include <thread>
#include "Precompiled.hpp"

#include "SDL.h"
#include "SDL_syswm.h"

#include "DeviceManager.hpp"
#include <nvrhi/utils.h>
#include <nvrhi/validation.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "gltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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
		uint32_t textureHandle{};
		std::vector<uint32_t> vertexIndices{};

		uint32_t IndexBytes() const
		{
			return vertexIndices.size() * sizeof( uint32_t );
		}

		const uint32_t* GetData() const
		{
			return vertexIndices.data();
		}
	};

	struct DrawMesh
	{
		std::vector<DrawVertex> vertexData{};
		std::vector<DrawSurface> surfaces{};

		uint32_t VertexBytes() const
		{
			return vertexData.size() * sizeof( DrawVertex );
		}

		const DrawVertex* GetData() const
		{
			return vertexData.data();
		}
	};

	struct GltfModel
	{
		struct BufferInfo
		{
			const fx::gltf::Accessor* accessor{ nullptr };
			const uint8_t* data{ nullptr };
			uint32_t dataStride{ 0 };
			uint32_t totalSize{ 0 };

			uint32_t NumElements() const
			{
				return totalSize / CalculateDataTypeSize( *accessor );
			}
		};

		GltfModel()
		{

		}

		void Init( const char* fileName )
		{
			using namespace fx::gltf;

			Document modelFile = LoadFromBinary( fileName );

			const Mesh& gltfMesh = modelFile.meshes[0];
			const Primitive& gltfPrimitive = gltfMesh.primitives[0];

			BufferInfo vertexPositionBuffer{};
			BufferInfo vertexNormalBuffer{};
			BufferInfo vertexTexcoordBuffer{};
			BufferInfo vertexColourBuffer{};
			BufferInfo indexBuffer{};

			std::cout << "Loading model " << fileName << "..." << std::endl;

			for ( const auto& attribute : gltfPrimitive.attributes )
			{
				std::cout << "   * found attribute '" << attribute.first << "' ";
				bool ignored = true;

				if ( attribute.first == "POSITION" )
				{
					vertexPositionBuffer = GetData( modelFile, modelFile.accessors[attribute.second] );
					std::cout << "(" << vertexPositionBuffer.NumElements() << " elements) ";
					ignored = false;
				}
				else if ( attribute.first == "NORMAL" )
				{
					vertexNormalBuffer = GetData( modelFile, modelFile.accessors[attribute.second] );
					std::cout << "(" << vertexNormalBuffer.NumElements() << " elements) ";
					ignored = false;
				}
				else if ( attribute.first == "TEXCOORD_0" )
				{
					vertexTexcoordBuffer = GetData( modelFile, modelFile.accessors[attribute.second] );
					std::cout << "(" << vertexTexcoordBuffer.NumElements() << " elements) ";
					ignored = false;
				}
				else if ( attribute.first == "COLOR_0" )
				{
					vertexColourBuffer = GetData( modelFile, modelFile.accessors[attribute.second] );
					std::cout << "(" << vertexColourBuffer.NumElements() << " elements) ";
					ignored = false;
				}

				std::cout << (ignored ? "(ignored)" : "(read)") << std::endl;
			}

			indexBuffer = GetData( modelFile, modelFile.accessors[gltfPrimitive.indices] );

			// Build a more traditional kinda buffer instead of having the modern separate buffers for separate vertex attributes kinda thang
			const uint32_t numVertices = vertexPositionBuffer.NumElements();
			mesh.vertexData.reserve( numVertices );

			for ( uint32_t i = 0U; i < numVertices; i++ )
			{
				DrawVertex vertex;

				vertex.vertexPosition = *(reinterpret_cast<const glm::vec3*>( vertexPositionBuffer.data ) + i);
				vertex.vertexNormal = *(reinterpret_cast<const glm::vec3*>( vertexNormalBuffer.data ) + i);
				vertex.vertexTextureCoords = *(reinterpret_cast<const glm::vec2*>(vertexTexcoordBuffer.data) + i);
				vertex.vertexTextureCoords *= 4.0f;
				// Vertex colour is RGBA uint16_t
				glm::u16vec4 vc = *(reinterpret_cast<const glm::u16vec4*>(vertexColourBuffer.data) + i);
				vertex.vertexColour.x = vc.x / 65536.0f;
				vertex.vertexColour.y = vc.y / 65536.0f;
				vertex.vertexColour.z = vc.z / 65536.0f;
				vertex.vertexColour.w = vc.w / 65536.0f;
				//vertex.vertexColour = { 1.0f, 1.0f, 1.0f, 1.0f };
				
				mesh.vertexData.push_back( vertex );
			}

			const uint32_t numIndices = indexBuffer.NumElements();
			Model::DrawSurface surface;
			
			for ( uint32_t i = 0U; i < numIndices; i++ )
			{
				switch ( indexBuffer.dataStride )
				{
				case 1: surface.vertexIndices.push_back( *(reinterpret_cast<const uint8_t*>(indexBuffer.data) + i) ); break;
				case 2: surface.vertexIndices.push_back( *(reinterpret_cast<const uint16_t*>(indexBuffer.data) + i) ); break;
				case 4: surface.vertexIndices.push_back( *(reinterpret_cast<const uint32_t*>(indexBuffer.data) + i) ); break;
				case 8: surface.vertexIndices.push_back( *(reinterpret_cast<const uint64_t*>(indexBuffer.data) + i) ); break;
				}
			}
			mesh.surfaces.push_back( std::move( surface ) );

			std::cout << "Total vertex count: " << mesh.vertexData.size() << std::endl
				<< "Total index count: " << mesh.surfaces[0].vertexIndices.size() << std::endl
				<< "Total triangle count: " << mesh.surfaces[0].vertexIndices.size() / 3 << std::endl;
		}

		DrawMesh mesh;

	private:
		static BufferInfo GetData( const fx::gltf::Document& doc, const fx::gltf::Accessor& accessor )
		{
			using namespace fx::gltf;

			const BufferView& bufferView = doc.bufferViews[accessor.bufferView];
			const Buffer& buffer = doc.buffers[bufferView.buffer];

			const uint32_t dataTypeSize = CalculateDataTypeSize( accessor );
			return BufferInfo{ &accessor, &buffer.data[static_cast<uint64_t>(bufferView.byteOffset) + accessor.byteOffset], dataTypeSize, accessor.count * dataTypeSize };
		}

		static uint32_t CalculateDataTypeSize( const fx::gltf::Accessor& accessor ) noexcept
		{
			using namespace fx::gltf;

			uint32_t elementSize = 0;
			switch ( accessor.componentType )
			{
			case Accessor::ComponentType::Byte:
			case Accessor::ComponentType::UnsignedByte:
				elementSize = 1;
				break;
			case Accessor::ComponentType::Short:
			case Accessor::ComponentType::UnsignedShort:
				elementSize = 2;
				break;
			case Accessor::ComponentType::Float:
			case Accessor::ComponentType::UnsignedInt:
				elementSize = 4;
				break;
			}

			switch ( accessor.type )
			{
			case Accessor::Type::Mat2:
				return 4 * elementSize;
				break;
			case Accessor::Type::Mat3:
				return 9 * elementSize;
				break;
			case Accessor::Type::Mat4:
				return 16 * elementSize;
				break;
			case Accessor::Type::Scalar:
				return elementSize;
				break;
			case Accessor::Type::Vec2:
				return 2 * elementSize;
				break;
			case Accessor::Type::Vec3:
				return 3 * elementSize;
				break;
			case Accessor::Type::Vec4:
				return 4 * elementSize;
				break;
			}

			return 0;
		}
	};

	// Fullscreen quad used to render framebuffers
	namespace ScreenQuad
	{
		// Format:
		// vec2 pos
		// vec2 texcoord
		const std::vector<float> Vertices =
		{
			-1.0f, -1.0f,
			0.0f, 0.0f,

			1.0f, -1.0f,
			1.0f, 0.0f,

			1.0f, 1.0f,
			1.0f, 1.0f,

			-1.0f, 1.0f,
			0.0f, 1.0f
		};

		const std::vector<uint32_t> Indices =
		{
			0, 1, 2,
			2, 3, 0
		};
	}

	namespace Pentagon
	{
		const std::vector<DrawVertex> Vertices =
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

		const std::vector<uint32_t> Indices =
		{
			0, 1, 2,
			0, 2, 3,
			0, 3, 4
		};
	}
}

namespace Texture
{
	struct TextureData
	{
		void Init( const char* fileName )
		{
			int x, y, comps;
			data = stbi_load( fileName, &x, &y, &comps, 4 );
		
			width = x;
			height = y;
			components = 4;
			bytesPerComponent = 1;
		}

		// TODO: handle compression?
		uint32_t GetNvrhiRowBytes() const
		{
			return width * components * bytesPerComponent;
		}

		nvrhi::Format GetNvrhiFormat() const
		{
			using namespace nvrhi;

			switch ( components )
			{
			case 1: return Format::R8_UNORM;
			case 2: return Format::RG8_UNORM;
			}

			// RGB8_UNORM does not exist in NVRHI, should probably PR that...
			
			return Format::RGBA8_UNORM;
		}

		uint16_t width{};
		uint16_t height{};
		uint8_t* data{};

		// RGB vs. RGBA
		uint8_t components{};
		// 8 bpp vs. 16 bpp
		uint8_t bytesPerComponent{};
	};
}

namespace Renderer
{
	using ShaderBinary = std::vector<uint8_t>;

	namespace Shader
	{
		bool LoadShaderBinary( const char* fileName, ShaderBinary& outShaderBinary )
		{
			outShaderBinary.clear();

			std::ifstream file( fileName, std::ios::binary | std::ios::ate );
			if ( !file )
			{
				std::cout << "Renderer::Shader::LoadShaderBinary: Cannot load " << fileName << " (doesn't exist)" << std::endl;
				return false;
			}

			size_t fileSize = file.tellg();
			file.seekg( 0U );

			outShaderBinary.reserve( fileSize );

			// If only this could be done in one line...
			uint8_t* data = new uint8_t[fileSize];
			file.read( reinterpret_cast<char*>( data ), fileSize );
			outShaderBinary.insert( outShaderBinary.begin(), data, data + fileSize );
			delete[] data;

			file.close();

			if ( !outShaderBinary.size() || outShaderBinary.size() != fileSize )
			{
				std::cout << "Renderer::Shader::LoadShaderBinary: Cannot load " << fileName << " for unknown reasons" << std::endl;
				return false;
			}

			return true;
		}
	}

	nvrhi::app::DeviceManager* DeviceManager;
	nvrhi::IDevice* Device;

	// Pipeline state
	nvrhi::GraphicsPipelineHandle Pipeline;
	nvrhi::InputLayoutHandle InputLayout;
	nvrhi::ShaderHandle VertexShader;
	nvrhi::ShaderHandle PixelShader;

	// Framebuffers
	nvrhi::TextureHandle MainFramebufferColourImage;
	nvrhi::TextureHandle MainFramebufferDepthImage;
	nvrhi::FramebufferHandle MainFramebuffer;

	// Data
	nvrhi::BufferHandle VertexBuffer;
	nvrhi::BufferHandle IndexBuffer;

	nvrhi::TextureHandle DiffuseTexture;
	nvrhi::SamplerHandle DiffuseTextureSampler;

	nvrhi::BufferHandle ConstantBuffer;

	nvrhi::BindingLayoutHandle BindingLayout;
	nvrhi::BindingSetHandle BindingSet;

	// Render commands
	nvrhi::CommandListHandle CommandList;
	nvrhi::CommandListHandle TransferList;

	class MessageCallbackImpl final : public nvrhi::IMessageCallback, public adm::Singleton<MessageCallbackImpl>
	{
	public:
		void message( nvrhi::MessageSeverity severity, const char* messageText )
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

			std::cout << "NVRHI::" << severityString << " " << messageText << std::endl << std::endl;

			if ( severity == nvrhi::MessageSeverity::Fatal )
			{
				std::cout << "Fatal error encountered, look above ^" << std::endl;
				std::cout << "=====================================" << std::endl;
			}
		}
	};

	static MessageCallbackImpl NvrhiMessageCallback;

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
	void GetVulkanExtensionsNeededBySDL( std::vector<std::string>& frameworkExtensions )
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

	struct ConstantBufferData
	{
		glm::mat4 viewMatrix;
		glm::mat4 projectionMatrix;
		float time;
	};

	Model::GltfModel gltfModel;
	Texture::TextureData TextureFile;
	ConstantBufferData TransformData
	{
		glm::lookAt( glm::vec3( -0.8f, -0.5f, 0.333f ), glm::vec3( 0.0f, 0.0f, -0.15f ), glm::vec3( 0.0f, 0.0f, 1.0f ) ),
		glm::perspective( glm::radians( 105.0f ), 16.0f / 9.0f, 0.01f, 100.0f ),
		0.0f
	};

	bool Init( SDL_Window* window, int windowWidth, int windowHeight )
	{
		using nvrhi::MessageSeverity;

		// ==========================================================================================================
		// DEVICE CREATION
		// ==========================================================================================================
		NvrhiMessageCallback.message( MessageSeverity::Info, "Initialising NVRHI..." );

		DeviceManager = nvrhi::app::DeviceManager::Create( nvrhi::GraphicsAPI::VULKAN );
		if ( nullptr == DeviceManager )
		{
			NvrhiMessageCallback.message( MessageSeverity::Fatal, "Couldn't create DeviceManager" );
			return false;
		}

		nvrhi::app::DeviceCreationParameters dcp;

		dcp.messageCallback = &NvrhiMessageCallback;
		dcp.backBufferWidth = windowWidth;
		dcp.backBufferHeight = windowHeight;
		dcp.enableDebugRuntime = true;
		dcp.enableNvrhiValidationLayer = true;
		dcp.swapChainFormat = nvrhi::Format::BGRA8_UNORM;
		dcp.swapChainSampleCount = 1;
		dcp.swapChainBufferCount = 3;
		GetVulkanExtensionsNeededBySDL( dcp.requiredVulkanInstanceExtensions );
		PopulateWindowData( window, dcp.windowSurfaceData );
		GetWindowFormat( window, dcp.swapChainFormat );
		// Todo: maybe some more settings, other defaults are fine tho'

		if ( !DeviceManager->CreateWindowDeviceAndSwapChain( dcp ) )
		{
			NvrhiMessageCallback.message( MessageSeverity::Fatal, "Couldn't initialise device and/or swapchain" );
			return false;
		}

		// Get a device & command list
		Device = DeviceManager->GetDevice();
		CommandList = Device->createCommandList();
		TransferList = Device->createCommandList( /*nvrhi::CommandListParameters().setQueueType(nvrhi::CommandQueue::Copy )*/ );

		// ==========================================================================================================
		// SHADER LOADING
		// ==========================================================================================================
		// Load the shaders from a SPIR-V binary that we'll produce with glslangValidator
		ShaderBinary vertexShaderBinary, pixelShaderBinary;
		if ( !Shader::LoadShaderBinary( "assets/shaders/default_main_vs.bin", vertexShaderBinary ) )
		{
			std::cout << "Couldn't load shader 'assets/shaders/default_main_vs.bin'" << std::endl;
			return false;
		}

		if ( !Shader::LoadShaderBinary( "assets/shaders/default_main_ps.bin", pixelShaderBinary ) )
		{
			std::cout << "Couldn't load shader 'assets/shaders/default_main_ps.bin'" << std::endl;
			return false;
		}

		std::cout << "Vertex shader size: " << vertexShaderBinary.size() << std::endl;
		std::cout << "Pixel shader size: " << pixelShaderBinary.size() << std::endl;

		if ( vertexShaderBinary.capacity() < 100U )
		{
			std::cout << "Vertex shader size looks wrong..." << std::endl;
			return false;
		}

		nvrhi::ShaderDesc shaderDesc;
		shaderDesc.shaderType = nvrhi::ShaderType::Vertex;
		shaderDesc.debugName = "My vertex shader";
		shaderDesc.entryName = "main_vs";

		VertexShader = Device->createShader( shaderDesc, vertexShaderBinary.data(), vertexShaderBinary.size() );

		if ( nullptr == VertexShader )
		{
			std::cout << "Failed to create vertex shader" << std::endl;
			return false;
		}

		shaderDesc.shaderType = nvrhi::ShaderType::Pixel;
		shaderDesc.debugName = "My pixel shader";
		shaderDesc.entryName = "main_ps";

		PixelShader = Device->createShader( shaderDesc, pixelShaderBinary.data(), pixelShaderBinary.size() );

		if ( nullptr == PixelShader )
		{
			std::cout << "Failed to create pixel shader" << std::endl;
			return false;
		}

		// ==========================================================================================================
		// GEOMETRY LOADING
		// ==========================================================================================================
		gltfModel.Init( "assets/splotch.glb" );

		// Set up vertex attributes, i.e. describe how our vertex data will be interpreted
		nvrhi::VertexAttributeDesc vertexAttributes[]
		{
			nvrhi::VertexAttributeDesc()
			.setName( "POSITION" )
			.setFormat( nvrhi::Format::RGB32_FLOAT )
			.setOffset( 0 )
			.setElementStride( sizeof( Model::DrawVertex ) ),

			nvrhi::VertexAttributeDesc()
			.setName( "NORMAL" )
			.setFormat( nvrhi::Format::RGB32_FLOAT )
			.setOffset( sizeof( glm::vec3 ) )
			.setElementStride( sizeof( Model::DrawVertex ) ),

			nvrhi::VertexAttributeDesc()
			.setName( "TEXCOORD" )
			.setFormat( nvrhi::Format::RG32_FLOAT )
			.setOffset( sizeof( glm::vec3 ) + sizeof( glm::vec3 ) )
			.setElementStride( sizeof( Model::DrawVertex ) ),

			nvrhi::VertexAttributeDesc()
			.setName( "COLOR" )
			.setFormat( nvrhi::Format::RGBA32_FLOAT )
			.setOffset( sizeof( glm::vec3 ) + sizeof( glm::vec3 ) + sizeof( glm::vec2 ) )
			.setElementStride( sizeof( Model::DrawVertex ) ),
		};

		InputLayout = Device->createInputLayout( vertexAttributes, std::size( vertexAttributes ), VertexShader );

		// Vertex buffer stuff
		nvrhi::BufferDesc bufferDesc;
		bufferDesc.byteSize = gltfModel.mesh.VertexBytes();
		bufferDesc.debugName = "My vertex buffer";
		bufferDesc.isVertexBuffer = true;
		bufferDesc.initialState = nvrhi::ResourceStates::CopyDest;

		VertexBuffer = Device->createBuffer( bufferDesc );

		if ( nullptr == VertexBuffer )
		{
			std::cout << "Could not create vertemx bumfer" << std::endl;
			return false;
		}

		// Index buffer stuff
		bufferDesc.byteSize = gltfModel.mesh.surfaces[0].IndexBytes();
		bufferDesc.debugName = "My index buffer";
		bufferDesc.isVertexBuffer = false;
		bufferDesc.isIndexBuffer = true;
		bufferDesc.initialState = nvrhi::ResourceStates::CopyDest;

		IndexBuffer = Device->createBuffer( bufferDesc );

		if ( nullptr == IndexBuffer )
		{
			std::cout << "Could not create shmindecks bunber" << std::endl;
			return false;
		}

		// ==========================================================================================================
		// CONSTANT BUFFER CREATION
		// ==========================================================================================================
		bufferDesc = nvrhi::utils::CreateVolatileConstantBufferDesc( sizeof( ConstantBufferData ), "My constant buffer", 16U);
		ConstantBuffer = Device->createBuffer( bufferDesc );

		// ==========================================================================================================
		// TEXTURE CREATION
		// ==========================================================================================================
		TextureFile.Init( "assets/256floor.png" );
		
		auto& textureDesc = nvrhi::TextureDesc()
			.setWidth( TextureFile.width )
			.setHeight( TextureFile.height )
			.setFormat( TextureFile.GetNvrhiFormat() );
		
		DiffuseTexture = Device->createTexture( textureDesc );
		
		auto& textureSampler = nvrhi::SamplerDesc()
			.setAllFilters( true )
			.setMaxAnisotropy( 16.0f )
			.setAllAddressModes( nvrhi::SamplerAddressMode::Wrap );
		
		DiffuseTextureSampler = Device->createSampler( textureSampler );

		auto& colourAttachmentDesc = nvrhi::TextureDesc()
			.setWidth( dcp.backBufferWidth )
			.setHeight( dcp.backBufferHeight )
			.setFormat( dcp.swapChainFormat )
			.setDimension( nvrhi::TextureDimension::Texture2D )
			.setInitialState( nvrhi::ResourceStates::Present | nvrhi::ResourceStates::RenderTarget )
			.setIsRenderTarget( true )
			.setDebugName( "Colour attachment image" );

		auto& depthAttachmentDesc = nvrhi::TextureDesc()
			.setWidth( dcp.backBufferWidth )
			.setHeight( dcp.backBufferHeight )
			.setFormat( nvrhi::Format::D32 ) // 32-bit depth buffer, no stencil
			.setDimension( nvrhi::TextureDimension::Texture2D )
			.setInitialState( nvrhi::ResourceStates::Present | nvrhi::ResourceStates::RenderTarget )
			.setIsRenderTarget( true )
			.setDebugName( "Depth attachment image" );

		MainFramebufferColourImage = Device->createTexture( colourAttachmentDesc );
		MainFramebufferDepthImage = Device->createTexture( depthAttachmentDesc );

		auto& mainFramebufferDesc = nvrhi::FramebufferDesc()
			.addColorAttachment( MainFramebufferColourImage )
			.setDepthAttachment( MainFramebufferDepthImage );

		MainFramebuffer = Device->createFramebuffer( mainFramebufferDesc );

		// ==========================================================================================================
		// DATA TRANSFER
		// ==========================================================================================================
		// Commands to copy this stuff to the GPU
		TransferList->open();

		TransferList->beginTrackingBufferState( VertexBuffer, nvrhi::ResourceStates::CopyDest );
		TransferList->writeBuffer( VertexBuffer, gltfModel.mesh.GetData(), gltfModel.mesh.VertexBytes() );
		TransferList->setPermanentBufferState( VertexBuffer, nvrhi::ResourceStates::VertexBuffer );

		TransferList->beginTrackingBufferState( IndexBuffer, nvrhi::ResourceStates::CopyDest );
		TransferList->writeBuffer( IndexBuffer, gltfModel.mesh.surfaces[0].GetData(), gltfModel.mesh.surfaces[0].IndexBytes() );
		TransferList->setPermanentBufferState( IndexBuffer, nvrhi::ResourceStates::IndexBuffer );

		// Constant buffers are written to at runtime

		TransferList->beginTrackingTextureState( DiffuseTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common );
		TransferList->writeTexture( DiffuseTexture, 0, 0, TextureFile.data, TextureFile.GetNvrhiRowBytes() );
		TransferList->setPermanentTextureState( DiffuseTexture, nvrhi::ResourceStates::ShaderResource );

		TransferList->close();

		// YEE HAW
		Device->executeCommandList( TransferList );

		// ==========================================================================================================
		// LAYOUT BINDINGS
		// ==========================================================================================================
		nvrhi::BindingSetDesc setDesc;
		setDesc.bindings =
		{
			nvrhi::BindingSetItem::ConstantBuffer( 0, ConstantBuffer ),
			nvrhi::BindingSetItem::Texture_SRV( 0, DiffuseTexture ),
			nvrhi::BindingSetItem::Sampler( 0, DiffuseTextureSampler )
		};
		nvrhi::utils::CreateBindingSetAndLayout( Device, nvrhi::ShaderType::All, 0, setDesc, BindingLayout, BindingSet );

		// ==========================================================================================================
		// PIPELINE CREATION
		// ==========================================================================================================
		nvrhi::GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.VS = VertexShader;
		pipelineDesc.PS = PixelShader;
		pipelineDesc.inputLayout = InputLayout;
		pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
		pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
		pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;
		pipelineDesc.renderState.rasterState.setCullNone();
		pipelineDesc.addBindingLayout( BindingLayout );

		Pipeline = Device->createGraphicsPipeline( pipelineDesc, DeviceManager->GetCurrentFramebuffer() );

		if ( nullptr == Pipeline )
		{
			std::cout << "Could not create pipeline" << std::endl;
			return false;
		}

		return true;
	}

	void Render()
	{
		// BeginFrame will do some multi-threading stuff to essentially
		// wait til the GPU's done rendering & presenting the last frame
		DeviceManager->BeginFrame();

		// Open the command buffa
		CommandList->open();

		// Let's tell the GPU it should fill the framebuffer with some dark greenish blue
		nvrhi::utils::ClearColorAttachment( CommandList, DeviceManager->GetCurrentFramebuffer(), 0, nvrhi::Color{0.01f, 0.05f, 0.05f, 1.0f});
		//nvrhi::utils::ClearDepthStencilAttachment( CommandList, MainFramebuffer, 0.0f, 0U );

		//CommandList->clearTextureFloat( MainFramebufferColourImage, nvrhi::AllSubresources, nvrhi::Color{ 0.01f, 0.05f, 0.05f, 1.0f } );
		//CommandList->clearDepthStencilTexture( MainFramebufferDepthImage, nvrhi::AllSubresources, true, 0.0f, false, 0U );

		// Update view & projection matrices
		TransformData.time += 0.016f;
		CommandList->writeBuffer( ConstantBuffer, &TransformData, sizeof( TransformData ) );

		// Set up the current graphics state
		auto& graphicsState = nvrhi::GraphicsState()
			//.addBindingSet( BindingSet_Vertex )
			.addBindingSet( BindingSet )
			.addVertexBuffer( { VertexBuffer, 0, 0 } )
			.setIndexBuffer( { IndexBuffer, nvrhi::Format::R32_UINT, 0 } )
			.setPipeline( Pipeline )
			.setFramebuffer( DeviceManager->GetCurrentFramebuffer() );
		// Without this, stuff won't render as the viewport will be 0,0
		graphicsState.viewport.addViewportAndScissorRect( nvrhi::Viewport( 1600.0f, 900.0f ) );
		CommandList->setGraphicsState( graphicsState );

		// Draw the thing
		auto& args = nvrhi::DrawArguments()
			.setVertexCount( gltfModel.mesh.surfaces[0].vertexIndices.size() ); // Vertex count is actually index count in this case

		CommandList->drawIndexed( args );

		// We've recorded all the commands we wanna send to the GPU, we're done here
		CommandList->close();

		// Send the commands to the GPU and execute immediately
		// This will NOT block the current thread unlike OpenGL, that's why there is a semaphore etc.
		// inside DeviceManager::BeginFrame
		Device->executeCommandList( CommandList );

		// Display the backbuffer onto da screen
		DeviceManager->Present();

		// NVRHI does some garbage collection for any resource that is no longer in use
		Device->runGarbageCollection();
	}

	void Shutdown()
	{
		CommandList = nullptr;
		TransferList = nullptr;

		VertexBuffer = nullptr;
		IndexBuffer = nullptr;
		DiffuseTexture = nullptr;
		DiffuseTextureSampler = nullptr;
		ConstantBuffer = nullptr;

		MainFramebuffer = nullptr;
		MainFramebufferColourImage = nullptr;
		MainFramebufferDepthImage = nullptr;

		VertexShader = nullptr;
		PixelShader = nullptr;
		
		BindingLayout = nullptr;
		BindingSet = nullptr;

		InputLayout = nullptr;
		Pipeline = nullptr;

		Device->waitForIdle();

		if ( nullptr != DeviceManager )
		{
			DeviceManager->Shutdown();
		}
	}
}

namespace System
{
	SDL_Window* Window = nullptr;

	bool Init( const char* windowTitle, int windowWidth, int windowHeight )
	{
		SDL_Init( SDL_INIT_VIDEO | SDL_INIT_EVENTS );
		
		Window = SDL_CreateWindow( windowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight, SDL_WINDOW_VULKAN );
	
		if ( !Renderer::Init( Window, windowWidth, windowHeight ) )
		{
			std::cout << "System::Init: couldn't initialise Renderer" << std::endl;
			return false;
		}

		return true;
	}

	void Update( bool& outShouldQuit )
	{
		{
			SDL_Event ev;
			while ( SDL_PollEvent( &ev ) )
			{
				if ( ev.type == SDL_QUIT )
				{
					outShouldQuit = true;
					return;
				}
			}
		}

		Renderer::Render();

		std::this_thread::sleep_for( std::chrono::milliseconds( 16 ) );
	}

	int Shutdown( const char* reason = nullptr )
	{
		Renderer::Shutdown();

		SDL_DestroyWindow( Window );

		SDL_Quit();

		if ( nullptr == reason )
		{
			std::cout << "Shutting down, no issues" << std::endl;
			return 0;
		}

		std::cout << "Shutting down, reason: " << reason << std::endl;
		return 1;
	}
}

int main( int argc, char** argv )
{
	if ( !System::Init( "nBidia pls fox", 1600, 900 ) )
	{
		return System::Shutdown( "Couldn't initialise" );
	}

	bool quit = false;
	while ( !quit )
	{
		System::Update( quit );
	}

	return System::Shutdown();
}
