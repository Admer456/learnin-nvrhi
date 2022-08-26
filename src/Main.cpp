
#include <iostream>
#include <fstream>
#include <thread>
#include <filesystem>

#include "Precompiled.hpp"

#include "SDL.h"
#include "SDL_syswm.h"

#include "DeviceManager.hpp"
#include <nvrhi/utils.h>
#include <nvrhi/validation.h>

#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "gltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

bool Check( void* ptr, const char* message )
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
		void Init( const char* fileName )
		{
			int x, y, comps;
		
			// Try out BMP, JPG, JPEG, TGA and PNG
			constexpr const char* ImageTypes[] =
			{
				".bmp", ".jpg", ".jpeg", ".tga", ".png"
			};
			
			data = stbi_load( fileName, &x, &y, &comps, 4 );

			if ( nullptr == data )
			{
				auto path = std::filesystem::path( fileName );
				if ( path.has_extension() )
				{
					path = path.parent_path()/path.stem();
				}
				else
				{
					path = path.parent_path()/path.filename();
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

		TextureData() = default;

		TextureData( TextureData&& texture ) noexcept
		{
			*this = std::move( texture );
		}

		~TextureData()
		{
			if ( nullptr != data )
			{
				delete[] data;
				data = nullptr;
			}
		}

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
					uint8_t* pixel = &textureData.data[y*stride + x*4];
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

		uint32_t IndexBytes() const
		{
			return vertexIndices.size() * sizeof( uint32_t );
		}

		const uint32_t* GetIndexData() const
		{
			return vertexIndices.data();
		}

		uint32_t VertexBytes() const
		{
			return vertexData.size() * sizeof( DrawVertex );
		}

		const DrawVertex* GetVertexData() const
		{
			return vertexData.data();
		}
	};

	struct DrawMesh
	{
		std::vector<DrawSurface> surfaces{};
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

		bool Init( const char* fileName )
		{
			using namespace fx::gltf;

			try
			{
				modelFile = LoadFromBinary( fileName );
			}
			catch ( std::system_error& error )
			{
				std::cout << "Error while loading model '" << fileName << "', " << error.what() << std::endl;
				return false;
			}

			std::cout << "Loading model " << fileName << "..." << std::endl;

			const Mesh& gltfMesh = modelFile.meshes[0];
			for ( const auto& gltfPrimitive : gltfMesh.primitives )
			{
				BufferInfo vertexPositionBuffer{};
				BufferInfo vertexNormalBuffer{};
				BufferInfo vertexTexcoordBuffer{};
				BufferInfo vertexColourBuffer{};
				BufferInfo indexBuffer{};

				std::string materialName = gltfPrimitive.material == -1 ? "default" : modelFile.materials[gltfPrimitive.material].name;

				std::cout << "  Primitive: " << (&gltfPrimitive - gltfMesh.primitives.data()) << std::endl;
				std::cout << "  Material name: " << materialName << std::endl;

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

				Model::DrawSurface surface;
				surface.materialName = materialName;

				// Build a more traditional kinda buffer instead of having the modern separate buffers for separate vertex attributes kinda thang
				const uint32_t numVertices = vertexPositionBuffer.NumElements();
				surface.vertexData.reserve( numVertices );

				for ( uint32_t i = 0U; i < numVertices; i++ )
				{
					DrawVertex vertex;

					vertex.vertexPosition = *(reinterpret_cast<const glm::vec3*>(vertexPositionBuffer.data) + i);
					vertex.vertexNormal = *(reinterpret_cast<const glm::vec3*>(vertexNormalBuffer.data) + i);
					vertex.vertexTextureCoords = *(reinterpret_cast<const glm::vec2*>(vertexTexcoordBuffer.data) + i);
					// Vertex colour is RGBA uint16_t
					if ( nullptr != vertexColourBuffer.data )
					{
						glm::u16vec4 vc = *(reinterpret_cast<const glm::u16vec4*>(vertexColourBuffer.data) + i);
						vertex.vertexColour.x = vc.x / 65536.0f;
						vertex.vertexColour.y = vc.y / 65536.0f;
						vertex.vertexColour.z = vc.z / 65536.0f;
						vertex.vertexColour.w = vc.w / 65536.0f;
					}
					else
					{
						vertex.vertexColour = { 1.0f, 1.0f, 1.0f, 1.0f };
					}

					surface.vertexData.push_back(vertex);
				}

				const uint32_t numIndices = indexBuffer.NumElements();
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

				std::cout << "Total vertex count: " << mesh.surfaces.back().vertexData.size() << std::endl
					<< "Total index count: " << mesh.surfaces.back().vertexIndices.size() << std::endl
					<< "Total triangle count: " << mesh.surfaces.back().vertexIndices.size() / 3 << std::endl;
			}

			return true;
		}

		DrawMesh mesh;
		fx::gltf::Document modelFile;

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

	std::vector<RenderModel> RenderModels;

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

	int32_t LoadRenderModelFromGltf( const char* fileName )
	{
		GltfModel modelFile;
		if ( !modelFile.Init( fileName ) )
		{
			return -1;
		}

		RenderModels.push_back( {} );
		RenderModel& rm = RenderModels.back();
		rm.name = fileName;

		for ( const auto& surface : modelFile.mesh.surfaces )
		{
			rm.surfaces.push_back( {} );
			RenderSurface& rs = rm.surfaces.back();
			rs.textureObjectHandle = Texture::FindOrCreateMaterial( surface.materialName.c_str() );
			//rs.textureObjectHandle = Texture::FindOrCreateMaterial( "assets/256floor.png" );
			rs.vertexBuffer = CreateBufferWithData( surface.vertexData, true, fileName );
			rs.indexBuffer = CreateBufferWithData( surface.vertexIndices, false, fileName );
			rs.numIndices = surface.vertexIndices.size();
			rs.numVertices = surface.vertexData.size();
			
			std::cout << "Submodel " << surface.materialName << std::endl
				<< "  " << rs.numIndices << " indices" << std::endl
				<< "  " << rs.numVertices << " vertices" << std::endl;

			// Default case ekek
			if ( rs.textureObjectHandle == -1 )
			{
				std::cout << "Cannot find texture: " << surface.materialName << std::endl;
				rs.textureObjectHandle = 0;
			}
	
			nvrhi::BindingSetDesc setDesc;
			setDesc.bindings =
			{
				nvrhi::BindingSetItem::Texture_SRV( 0, Texture::TextureObjects[rs.textureObjectHandle] ),
			};

			rs.bindingSet = Renderer::Device->createBindingSet( setDesc, ::Renderer::Scene::BindingLayoutEntity );
		}

		return RenderModels.size() - 1;
	}

	// Fullscreen quad used to render framebuffers
	namespace ScreenQuad
	{
		// Format:
		// vec2 pos
		// vec2 texcoord
		const std::vector<float> Vertices =
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

	// Fullscreen framebuffer rendering
	namespace ScreenQuad
	{
		// Pipeline state
		nvrhi::GraphicsPipelineHandle Pipeline;
		nvrhi::InputLayoutHandle InputLayout;
		nvrhi::ShaderHandle VertexShader;
		nvrhi::ShaderHandle PixelShader;

		// Data
		nvrhi::BufferHandle VertexBuffer;
		nvrhi::BufferHandle IndexBuffer;

		nvrhi::BindingLayoutHandle BindingLayout;
		nvrhi::BindingSetHandle BindingSet;
	}

	namespace Scene
	{
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
		//nvrhi::BufferHandle VertexBuffer;
		//nvrhi::BufferHandle IndexBuffer;

		//nvrhi::TextureHandle DiffuseTexture;
		nvrhi::SamplerHandle DiffuseTextureSampler;

		nvrhi::BufferHandle ConstantBufferGlobal;
		nvrhi::BufferHandle ConstantBufferEntity;

		nvrhi::BindingLayoutHandle BindingLayoutGlobal;
		nvrhi::BindingLayoutHandle BindingLayoutEntity;
		nvrhi::BindingSetHandle BindingSet;
	}

	// Render commands
	nvrhi::CommandListHandle CommandList;
	nvrhi::CommandListHandle TransferList;

	namespace Logic
	{
		struct RenderEntity
		{
			int32_t renderModelIndex{ -1 };
			glm::mat4 transform;

			// Todo: expand these with surface indices

			const adm::Vector<Model::RenderSurface>& GetRenderSurfaces() const
			{
				return GetRenderModel().surfaces;
			}

			Model::RenderModel& GetRenderModel() const
			{
				return Model::RenderModels[renderModelIndex];
			}
		};
	}

	std::vector<Logic::RenderEntity> RenderEntities;

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

	struct ConstantBufferDataEntity
	{
		glm::mat4 entityMatrix;
	};

	constexpr float MaxViewDistance = 100.0f;

	ConstantBufferData TransformData
	{
		glm::lookAt( glm::vec3( -1.8f, -1.5f, 1.733f ), glm::vec3( 0.0f, 0.0f, 1.0f ), glm::vec3( 0.0f, 0.0f, 1.0f ) ),
		glm::perspectiveZO( glm::radians( 105.0f ), 16.0f / 9.0f, 0.01f, MaxViewDistance ),
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

		// Load the shaders from a SPIR-V binary that we'll produce with NVRHI-SC
		const auto loadShaders = []( const char* vertexBinaryFile, const char* pixelBinaryFile, 
			nvrhi::ShaderHandle& outVertexShader, nvrhi::ShaderHandle& outPixelShader )
		{
			ShaderBinary vertexBinary, pixelBinary;

			if ( !Shader::LoadShaderBinary( vertexBinaryFile, vertexBinary ) )
			{
				std::cout << "Couldn't load shader '" << vertexBinaryFile << "'" << std::endl;
				return false;
			}

			if ( !Shader::LoadShaderBinary( pixelBinaryFile, pixelBinary ) )
			{
				std::cout << "Couldn't load shader '" << pixelBinaryFile << "'" << std::endl;
				return false;
			}
			
			std::cout << "Vertex shader size: " << vertexBinary.size() << std::endl
				<< "Pixel shader size: " << pixelBinary.size() << std::endl;

			nvrhi::ShaderDesc shaderDesc;
			shaderDesc.shaderType = nvrhi::ShaderType::Vertex;
			shaderDesc.debugName = vertexBinaryFile;
			shaderDesc.entryName = "main_vs";

			outVertexShader = Device->createShader( shaderDesc, vertexBinary.data(), vertexBinary.size() );
			if ( !Check( outVertexShader, "Failed to create vertex shader" ) )
			{
				return false;
			}

			shaderDesc.shaderType = nvrhi::ShaderType::Pixel;
			shaderDesc.debugName = pixelBinaryFile;
			shaderDesc.entryName = "main_ps";

			outPixelShader = Device->createShader( shaderDesc, pixelBinary.data(), pixelBinary.size() );
			if ( !Check( outPixelShader, "Failed to create pixel shader" ) )
			{
				return false;
			}

			return true;
		};

		if ( !loadShaders( "assets/shaders/default_main_vs.bin", "assets/shaders/default_main_ps.bin", Scene::VertexShader, Scene::PixelShader ) )
		{
			std::cout << "Failed to load the scene shaders" << std::endl;
			return false;
		}

		if ( !loadShaders( "assets/shaders/screen_main_vs.bin", "assets/shaders/screen_main_ps.bin", ScreenQuad::VertexShader, ScreenQuad::PixelShader ) )
		{
			std::cout << "Failed to load the screen shaders" << std::endl;
			return false;
		}

		// ==========================================================================================================
		// GEOMETRY LOADING
		// Set up vertex attributes, i.e. describe how our vertex data will be interpreted
		// ==========================================================================================================
		nvrhi::VertexAttributeDesc screenVertexAttributes[]
		{
			nvrhi::VertexAttributeDesc()
			.setName( "POSITION" )
			.setFormat( nvrhi::Format::RG32_FLOAT )
			.setOffset( 0 )
			.setElementStride( 4 * sizeof( float ) ),

			nvrhi::VertexAttributeDesc()
			.setName( "TEXCOORD" )
			.setFormat( nvrhi::Format::RG32_FLOAT )
			.setOffset( 2 * sizeof( float ) )
			.setElementStride( 4 * sizeof( float) )
		};
		ScreenQuad::InputLayout = Device->createInputLayout( screenVertexAttributes, std::size( screenVertexAttributes ), ScreenQuad::VertexShader );

		nvrhi::VertexAttributeDesc sceneVertexAttributes[]
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
		Scene::InputLayout = Device->createInputLayout( sceneVertexAttributes, std::size( sceneVertexAttributes ), Scene::VertexShader );

		// Vertex buffer stuff
		nvrhi::BufferDesc bufferDesc;
		bufferDesc.byteSize = Model::ScreenQuad::Vertices.size() * sizeof( float );
		bufferDesc.initialState = nvrhi::ResourceStates::CopyDest;
		bufferDesc.debugName = "Screenquad vertex buffer";
		bufferDesc.isVertexBuffer = true;
		ScreenQuad::VertexBuffer = Device->createBuffer( bufferDesc );

		if ( !Check( ScreenQuad::VertexBuffer, "Failed to create ScreenQuad::VertexBuffer" ) )
			return false;

		// Index buffer stuff
		bufferDesc.byteSize = Model::ScreenQuad::Indices.size() * sizeof( uint32_t );
		bufferDesc.debugName = "Screenquad index buffer";
		bufferDesc.isVertexBuffer = false;
		bufferDesc.isIndexBuffer = true;
		ScreenQuad::IndexBuffer = Device->createBuffer( bufferDesc );

		if ( !Check( ScreenQuad::IndexBuffer, "Failed to create ScreenQuad::IndexBuffer" ) )
			return false;

		// ==========================================================================================================
		// CONSTANT BUFFER CREATION
		// ==========================================================================================================
		bufferDesc = nvrhi::utils::CreateVolatileConstantBufferDesc( sizeof( ConstantBufferData ), "Global constant buffer", 16U );
		Scene::ConstantBufferGlobal = Device->createBuffer( bufferDesc );

		if ( !Check( Scene::ConstantBufferGlobal, "Failed to create Scene::ConstantBufferGlobal" ) )
			return false;

		bufferDesc = nvrhi::utils::CreateVolatileConstantBufferDesc( sizeof( ConstantBufferDataEntity ), "Per-entity constant buffer", 16U );
		Scene::ConstantBufferEntity = Device->createBuffer( bufferDesc );

		// ==========================================================================================================
		// TEXTURE CREATION
		// ==========================================================================================================

		// Sampler
		auto& textureSampler = nvrhi::SamplerDesc()
			.setAllFilters( true )
			.setMaxAnisotropy( 16.0f )
			.setAllAddressModes( nvrhi::SamplerAddressMode::Wrap );
		
		Scene::DiffuseTextureSampler = Device->createSampler( textureSampler );

		if ( !Check( Scene::DiffuseTextureSampler, "Failed to create Scene::DiffuseTextureSampler" ) )
			return false;

		using RStates = nvrhi::ResourceStates;

		const RStates ColourBufferStates = RStates::RenderTarget;
		const RStates DepthBufferStates = RStates::DepthWrite | RStates::DepthRead;

		// Colour and depth attachment for the framebuffer
		auto colourAttachmentDesc = nvrhi::TextureDesc()
			.setWidth( dcp.backBufferWidth )
			.setHeight( dcp.backBufferHeight )
			.setFormat( dcp.swapChainFormat )
			.setDimension( nvrhi::TextureDimension::Texture2D )
			.setKeepInitialState( true )
			.setInitialState( ColourBufferStates )
			.setIsRenderTarget( true )
			.setDebugName( "Colour attachment image" );

		auto depthAttachmentDesc = nvrhi::TextureDesc()
			.setWidth( dcp.backBufferWidth )
			.setHeight( dcp.backBufferHeight )
			.setFormat( nvrhi::Format::D32 )
			.setIsTypeless( true )
			.setDimension( nvrhi::TextureDimension::Texture2D )
			.setKeepInitialState( true )
			.setInitialState( DepthBufferStates )
			.setIsRenderTarget( true )
			.setDebugName( "Depth attachment image" );

		Scene::MainFramebufferColourImage = Device->createTexture( colourAttachmentDesc );
		Scene::MainFramebufferDepthImage = Device->createTexture( depthAttachmentDesc );

		if ( !Check( Scene::MainFramebufferColourImage, "Failed to create Scene::MainFramebufferColourImage" ) )
			return false;
		if ( !Check( Scene::MainFramebufferDepthImage, "Failed to create Scene::MainFramebufferDepthImage" ) )
			return false;

		auto mainFramebufferDesc = nvrhi::FramebufferDesc()
			.addColorAttachment( Scene::MainFramebufferColourImage )
			.setDepthAttachment( Scene::MainFramebufferDepthImage );

		Scene::MainFramebuffer = Device->createFramebuffer( mainFramebufferDesc );

		if ( !Check( Scene::MainFramebuffer, "Failed to create Scene::MainFramebuffer" ) )
			return false;

		const auto& framebufferInfo = Scene::MainFramebuffer->getFramebufferInfo();
		
		const auto printFramebufferInfo = []( const nvrhi::FramebufferInfo& fbInfo, const char* name )
		{
			std::cout << "Framebuffer: " << name << std::endl
				<< "  * Size:           " << fbInfo.width << "x" << fbInfo.height << std::endl
				<< "  * Sample count:   " << fbInfo.sampleCount << std::endl
				<< "  * Sample quality: " << fbInfo.sampleQuality << std::endl
				<< "  * Colour format:  " << nvrhi::utils::FormatToString( fbInfo.colorFormats[0] ) << std::endl
				<< "  * Depth format:   " << nvrhi::utils::FormatToString( fbInfo.depthFormat ) << std::endl;
		};

		printFramebufferInfo( framebufferInfo, "Main framebuffer" );
		printFramebufferInfo( DeviceManager->GetCurrentFramebuffer()->getFramebufferInfo(), "Backbuffer" );

		// ==========================================================================================================
		// DATA TRANSFER
		// ==========================================================================================================
		// Commands to copy this stuff to the GPU
		TransferList->open();

		// Screenquad resources
		TransferList->beginTrackingBufferState( ScreenQuad::VertexBuffer, RStates::CopyDest );
		TransferList->writeBuffer( ScreenQuad::VertexBuffer, Model::ScreenQuad::Vertices.data(), Model::ScreenQuad::Vertices.size() * sizeof( float ) );
		TransferList->setPermanentBufferState( ScreenQuad::VertexBuffer, RStates::VertexBuffer );

		TransferList->beginTrackingBufferState( ScreenQuad::IndexBuffer, RStates::CopyDest );
		TransferList->writeBuffer( ScreenQuad::IndexBuffer, Model::ScreenQuad::Indices.data(), Model::ScreenQuad::Indices.size() * sizeof( uint32_t ) );
		TransferList->setPermanentBufferState( ScreenQuad::IndexBuffer, RStates::IndexBuffer );

		// Constant buffers are written to at runtime

		TransferList->close();

		// YEE HAW
		Device->executeCommandList( TransferList );

		// ==========================================================================================================
		// LAYOUT BINDINGS
		// ==========================================================================================================
		nvrhi::BindingLayoutDesc layoutDesc;
		layoutDesc.registerSpace = 0U;
		layoutDesc.visibility = nvrhi::ShaderType::All;
		// Per-frame bindings
		layoutDesc.bindings =
		{
			nvrhi::BindingLayoutItem::VolatileConstantBuffer( 0 ),
			nvrhi::BindingLayoutItem::VolatileConstantBuffer( 1 ),
			nvrhi::BindingLayoutItem::Sampler( 0 ),
		};
		Scene::BindingLayoutGlobal = Device->createBindingLayout( layoutDesc );

		// Per-entity bindings
		layoutDesc.bindings =
		{
			nvrhi::BindingLayoutItem::Texture_SRV( 0 ),
		};
		Scene::BindingLayoutEntity = Device->createBindingLayout( layoutDesc );

		nvrhi::BindingSetDesc setDesc;
		setDesc.bindings =
		{
			nvrhi::BindingSetItem::ConstantBuffer( 0, Scene::ConstantBufferGlobal ),
			nvrhi::BindingSetItem::ConstantBuffer( 1, Renderer::Scene::ConstantBufferEntity ),
			nvrhi::BindingSetItem::Sampler( 0, Renderer::Scene::DiffuseTextureSampler ),
			// Diffuse texture will be filled in by render entities
		};
		Scene::BindingSet = Device->createBindingSet( setDesc, Scene::BindingLayoutGlobal );

		// For the screen quad shader, we only need to bind the framebuffer's colour attachment
		setDesc.bindings =
		{
			nvrhi::BindingSetItem::Texture_SRV( 0, Scene::MainFramebufferColourImage ),
			nvrhi::BindingSetItem::Texture_SRV( 1, Scene::MainFramebufferDepthImage ),
			nvrhi::BindingSetItem::Sampler( 0, Scene::DiffuseTextureSampler )
		};
		nvrhi::utils::CreateBindingSetAndLayout( Device, nvrhi::ShaderType::All, 0, setDesc, ScreenQuad::BindingLayout, ScreenQuad::BindingSet );

		// ==========================================================================================================
		// PIPELINE CREATION
		// ==========================================================================================================

		// Screen pipeline
		nvrhi::GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.VS = ScreenQuad::VertexShader;
		pipelineDesc.PS = ScreenQuad::PixelShader;
		pipelineDesc.inputLayout = ScreenQuad::InputLayout;
		pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
		pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
		pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;
		pipelineDesc.renderState.rasterState.setCullNone();
		pipelineDesc.bindingLayouts = { ScreenQuad::BindingLayout };

		ScreenQuad::Pipeline = Device->createGraphicsPipeline( pipelineDesc, DeviceManager->GetCurrentFramebuffer() );

		if ( !Check( ScreenQuad::Pipeline, "Could not create ScreenQuad::Pipeline" ) )
			return false;

		// Scene pipeline
		pipelineDesc.VS = Scene::VertexShader;
		pipelineDesc.PS = Scene::PixelShader;
		pipelineDesc.inputLayout = Scene::InputLayout;
		pipelineDesc.renderState.depthStencilState.depthTestEnable = true;
		pipelineDesc.renderState.depthStencilState.depthWriteEnable = true;
		pipelineDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
		pipelineDesc.bindingLayouts = 
		{
			Scene::BindingLayoutGlobal,
			Scene::BindingLayoutEntity,
		};

		Scene::Pipeline = Device->createGraphicsPipeline( pipelineDesc, Scene::MainFramebuffer );

		if ( !Check( Scene::Pipeline, "Could not create Scene::Pipeline" ) )
			return false;

		return true;
	}

	void LoadEntities()
	{
		const auto createEntity = []( const char* modelPath, glm::vec3 position, glm::mat4 orientation )
		{
			RenderEntities.push_back( {} );
			auto& re = RenderEntities.back();

			re.renderModelIndex = Model::LoadRenderModelFromGltf( modelPath );
			re.transform = glm::translate( glm::identity<glm::mat4>(), position ) * orientation;
		};

		// Create default texture
		Texture::FindOrCreateMaterial( nullptr );

		createEntity( "assets/TestEnvironment.glb", { 0.0f, 0.0f, 0.0f }, glm::identity<glm::mat4>() );
		createEntity( "assets/MossPatch.glb", { 0.0f, 0.0f, 0.0f }, glm::identity<glm::mat4>() );
		
		//createEntity( "assets/protogen_25d.glb", { -1.0f, 0.0f, 0.0f }, glm::eulerAngleXYZ( glm::radians( 100.0f ), glm::radians( 0.0f ), glm::radians( 0.0f ) ) );
		//createEntity( "assets/skunk.glb", { 0.0f, -1.0f, 0.0f }, glm::eulerAngleXYZ( glm::radians( 105.0f ), glm::radians( -90.0f ), glm::radians( 15.0f ) ) );
	}

	void RenderScreenQuad()
	{
		// Clear the screen with black
		nvrhi::utils::ClearColorAttachment( CommandList, DeviceManager->GetCurrentFramebuffer(), 0, nvrhi::Color{ 0.0f, 0.0f, 0.0f, 1.0f } );
		
		// Set up the current graphics state
		auto graphicsState = nvrhi::GraphicsState()
			.addBindingSet( ScreenQuad::BindingSet )
			.addVertexBuffer( { ScreenQuad::VertexBuffer, 0, 0 } )
			.setIndexBuffer( { ScreenQuad::IndexBuffer, nvrhi::Format::R32_UINT, 0 } )
			.setPipeline( ScreenQuad::Pipeline )
			.setFramebuffer( DeviceManager->GetCurrentFramebuffer() );
		// Without this, stuff won't render as the viewport will be 0,0
		graphicsState.viewport.addViewportAndScissorRect( nvrhi::Viewport( 1600.0f, 900.0f ) );
		CommandList->setGraphicsState( graphicsState );

		// Draw the thing
		auto& args = nvrhi::DrawArguments()
			.setVertexCount( Model::ScreenQuad::Indices.size() ); // Vertex count is actually index count in this case
		CommandList->drawIndexed( args );
	}

	void RenderSceneIntoFramebuffer()
	{
		// Let's tell the GPU it should fill the main buffer with some dark greenish blue
		CommandList->clearTextureFloat( Scene::MainFramebufferColourImage, nvrhi::AllSubresources, nvrhi::Color{ 0.01f, 0.05f, 0.05f, 1.0f } );
		// Also clear the depth buffer
		CommandList->clearDepthStencilTexture( Scene::MainFramebufferDepthImage, nvrhi::AllSubresources, true, 1.0f, false, 0U );

		// Update view & projection matrices
		TransformData.time += 0.016f;
		CommandList->writeBuffer( Scene::ConstantBufferGlobal, &TransformData, sizeof( TransformData ) );

		// Set up the current graphics state
		auto graphicsState = nvrhi::GraphicsState()
			.setPipeline( Scene::Pipeline )
			.setFramebuffer( Scene::MainFramebuffer );
		// Without this, stuff won't render as the viewport will be 0,0
		graphicsState.viewport.addViewportAndScissorRect( nvrhi::Viewport( 1600.0f, 900.0f ) );

		// Draw all entities
		for ( const auto& renderEntity : RenderEntities )
		{
			// Update per-entity transform data
			CommandList->writeBuffer( Scene::ConstantBufferEntity, &renderEntity.transform, sizeof( glm::mat4 ) );

			// Draw all surfaces
			for ( const auto& renderSurface : renderEntity.GetRenderSurfaces() )
			{
				// Combine the global binding set (viewproj matrix + time + sampler)
				// with the per-entity binding set (diffuse texture)
				graphicsState.bindings =
				{
					Scene::BindingSet,
					renderSurface.bindingSet,
				};
				// It is possible to use multiple vertex buffers (one for positions, one for normals etc.), 
				// but we're only using one here
				graphicsState.vertexBuffers = { { renderSurface.vertexBuffer, 0, 0 } };
				graphicsState.indexBuffer = { renderSurface.indexBuffer, nvrhi::Format::R32_UINT, 0 };

				CommandList->setGraphicsState( graphicsState );

				// Draw the thing
				auto& args = nvrhi::DrawArguments()
					.setVertexCount( renderSurface.numIndices ); // Vertex count is actually index count in this case
				CommandList->drawIndexed( args );
			}
		}
	}

	void CalculateDirections( const glm::vec3& angles, glm::vec3& forward, glm::vec3& right, glm::vec3& up )
	{
		const float cosPitch = std::cos( glm::radians( -angles.x ) );
		const float cosYaw = std::cos( glm::radians( angles.y ) );
		const float cosRoll = std::cos( glm::radians( angles.z ) );

		const float sinPitch = std::sin( glm::radians( -angles.x ) );
		const float sinYaw = std::sin( glm::radians( angles.y ) );
		const float sinRoll = std::sin( glm::radians( angles.z ) );

		forward =
		{
			cosYaw * cosPitch,
			sinYaw * cosPitch,
			-sinPitch
		};
		
		up =
		{
			-sinYaw * -sinRoll + cosYaw * sinPitch * cosRoll,
			cosYaw * -sinRoll + sinYaw * sinPitch * cosRoll,
			(cosPitch * cosRoll)
		};
	
		right = glm::cross( forward, up );
	}

	glm::mat4 CalculateViewMatrix( const glm::vec3& position, const glm::vec3& angles )
	{
		constexpr int forward = 2;
		constexpr int right = 0;
		constexpr int up = 1;
		constexpr int pos = 3;

		constexpr int x = 0;
		constexpr int y = 1;
		constexpr int z = 2;
		constexpr int w = 3;

		glm::vec3 vForward, vRight, vUp;
		CalculateDirections( angles, vForward, vRight, vUp );
		vRight *= -1.0f;

		return glm::lookAt( position, position + vForward, vUp );

		glm::mat4 m( 1.0f );
		m[x][forward] = -vForward.x;
		m[y][forward] = -vForward.y;
		m[z][forward] = -vForward.z;

		m[x][right] = vRight.x;
		m[y][right] = vRight.y;
		m[z][right] = vRight.z;
		
		m[x][up] = vUp.x;
		m[y][up] = vUp.y;
		m[z][up] = vUp.z;

		m[pos][forward] = glm::dot( vForward, position );
		m[pos][right] = -glm::dot( vRight, position );
		m[pos][up] = -glm::dot( vUp, position );

		return m;
	}

	void Update( float deltaTime )
	{
		static glm::vec3 viewPosition{};
		static glm::vec3 viewAngles{ 0.0f, 0.0f, 0.0f };

		glm::vec3 viewForward, viewRight, viewUp;
		CalculateDirections( viewAngles, viewForward, viewRight, viewUp );

		// Update view angles
		{
			int mx, my;
			int mstate = SDL_GetRelativeMouseState( &mx, &my );

			if ( mstate & SDL_BUTTON_RMASK )
			{
				viewAngles.y -= mx * 0.2f;
				viewAngles.x -= my * 0.2f;

				std::cout << "m " << mx << " " << my << std::endl;
				std::cout << "v " << viewAngles.x << " " << viewAngles.y << " " << viewAngles.z << std::endl;
			}
		}

		// Update view position
		{
			const auto* keys = SDL_GetKeyboardState( nullptr );
			if ( keys[SDL_SCANCODE_W] )
			{
				viewPosition += viewForward * deltaTime * 5.0f;
			}
			if ( keys[SDL_SCANCODE_S] )
			{
				viewPosition -= viewForward * deltaTime * 5.0f;
			}
			if ( keys[SDL_SCANCODE_D] )
			{
				viewPosition += viewRight * deltaTime * 5.0f;
			}
			if ( keys[SDL_SCANCODE_A] )
			{
				viewPosition -= viewRight * deltaTime * 5.0f;
			}
			if ( keys[SDL_SCANCODE_SPACE] )
			{
				viewPosition += viewUp * deltaTime * 5.0f;
			}
			if ( keys[SDL_SCANCODE_LCTRL] )
			{
				viewPosition -= viewUp * deltaTime * 5.0f;
			}

			float rollTarget = 0.0f;
			if ( keys[SDL_SCANCODE_Q] )
			{
				rollTarget -= 45.0f;
			}
			if ( keys[SDL_SCANCODE_E] )
			{
				rollTarget += 45.0f;
			}
			viewAngles.z = adm::Fade( viewAngles.z, rollTarget, 0.1f, deltaTime );
		}

		// Calculate view matrix
		TransformData.viewMatrix = CalculateViewMatrix( viewPosition, viewAngles );
	}

	void Render()
	{
		// BeginFrame will do some multi-threading stuff to essentially
		// wait til the GPU's done rendering & presenting the last frame
		DeviceManager->BeginFrame();

		// Open the command buffa
		CommandList->open();

		// Render the scene with projection'n'everything into a framebuffer
		RenderSceneIntoFramebuffer();

		// Render said framebuffer as a quad on the screen, because
		// backbuffer does not have a depth attachment
		// This is actually one way to implement framebuffer blitting
		// Should make it more generic
		RenderScreenQuad();

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

		for ( auto& textureObject : Texture::TextureObjects )
		{
			textureObject = nullptr;
		}

		Model::RenderModels.clear();
		RenderEntities.clear();

		ScreenQuad::VertexBuffer = nullptr;
		ScreenQuad::IndexBuffer = nullptr;

		ScreenQuad::VertexShader = nullptr;
		ScreenQuad::PixelShader = nullptr;

		Scene::DiffuseTextureSampler = nullptr;
		Scene::ConstantBufferEntity = nullptr;
		Scene::ConstantBufferGlobal = nullptr;

		ScreenQuad::BindingLayout = nullptr;
		ScreenQuad::BindingSet = nullptr;

		ScreenQuad::InputLayout = nullptr;
		ScreenQuad::Pipeline = nullptr;

		Scene::MainFramebuffer = nullptr;
		Scene::MainFramebufferColourImage = nullptr;
		Scene::MainFramebufferDepthImage = nullptr;

		Scene::VertexShader = nullptr;
		Scene::PixelShader = nullptr;
		
		Scene::BindingLayoutGlobal = nullptr;
		Scene::BindingLayoutEntity = nullptr;
		Scene::BindingSet = nullptr;

		Scene::InputLayout = nullptr;
		Scene::Pipeline = nullptr;

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

		Renderer::LoadEntities();

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

		Renderer::Update( 0.016f );
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
