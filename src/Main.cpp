
#include <thread>
#include <string_view>
using namespace std::string_literals;
using namespace std::string_view_literals;

#include "Common.hpp"

#include "SDL.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Renderer
{
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

	class MessageCallbackImpl final : public nvrhi::IMessageCallback
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

	bool Init( SDL_Window* window, int windowWidth, int windowHeight, nvrhi::GraphicsAPI graphicsApi )
	{
		using nvrhi::MessageSeverity;

		// ==========================================================================================================
		// DEVICE CREATION
		// ==========================================================================================================
		MessageCallbackImpl& NvrhiMessageCallback = adm::Singleton<MessageCallbackImpl>::GetInstance();
		NvrhiMessageCallback.message( MessageSeverity::Info, "Initialising NVRHI..." );

		DeviceManager = nvrhi::app::DeviceManager::Create( graphicsApi );
		if ( nullptr == DeviceManager )
		{
			NvrhiMessageCallback.message( MessageSeverity::Fatal, "Couldn't create DeviceManager" );
			return false;
		}

		nvrhi::app::DeviceCreationParameters dcp;

		dcp.messageCallback = &NvrhiMessageCallback;
		dcp.backBufferWidth = windowWidth;
		dcp.backBufferHeight = windowHeight;
		//dcp.enableDebugRuntime = true;
		//dcp.enableNvrhiValidationLayer = true;
		dcp.swapChainFormat = nvrhi::Format::BGRA8_UNORM;
		dcp.swapChainSampleCount = 1;
		dcp.swapChainBufferCount = 3;
		dcp.refreshRate = 60;

		System::GetVulkanExtensionsForSDL( dcp.requiredVulkanInstanceExtensions );
		System::PopulateWindowData( window, dcp.windowSurfaceData );
		System::GetWindowFormat( window, dcp.swapChainFormat );
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
		const auto loadShaders = [&graphicsApi]( const char* vertexBinaryFile, const char* pixelBinaryFile, 
			nvrhi::ShaderHandle& outVertexShader, nvrhi::ShaderHandle& outPixelShader )
		{
			std::string vertexBinaryPath, pixelBinaryPath;
			Shader::ShaderBinary vertexBinary, pixelBinary;

			switch ( graphicsApi )
			{
			case nvrhi::GraphicsAPI::D3D11:
				vertexBinaryPath = "assets/shaders/dx11/"s + vertexBinaryFile;
				pixelBinaryPath =  "assets/shaders/dx11/"s + pixelBinaryFile;
				break;
			case nvrhi::GraphicsAPI::D3D12:
				vertexBinaryPath = "assets/shaders/dx12/"s + vertexBinaryFile;
				pixelBinaryPath =  "assets/shaders/dx12/"s + pixelBinaryFile;
				break;
			case nvrhi::GraphicsAPI::VULKAN:
				vertexBinaryPath = "assets/shaders/vk/"s + vertexBinaryFile;
				pixelBinaryPath =  "assets/shaders/vk/"s + pixelBinaryFile;
				break;
			}

			if ( !Shader::LoadShaderBinary( vertexBinaryPath.c_str(), vertexBinary) )
			{
				std::cout << "Couldn't load shader '" << vertexBinaryFile << "'" << std::endl;
				return false;
			}

			if ( !Shader::LoadShaderBinary( pixelBinaryPath.c_str(), pixelBinary) )
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

		if ( !loadShaders( "default_main_vs.bin", "default_main_ps.bin", Scene::VertexShader, Scene::PixelShader ) )
		{
			std::cout << "Failed to load the scene shaders" << std::endl;
			return false;
		}

		if ( !loadShaders( "screen_main_vs.bin", "screen_main_ps.bin", ScreenQuad::VertexShader, ScreenQuad::PixelShader ) )
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
		const RStates DepthBufferStates = RStates::DepthWrite;

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
		pipelineDesc.renderState.depthStencilState.stencilEnable = false;
		pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
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
		pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Front;
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

	bool Init( const char* windowTitle, int windowWidth, int windowHeight, nvrhi::GraphicsAPI graphicsApi )
	{
		SDL_Init( SDL_INIT_VIDEO | SDL_INIT_EVENTS );
		
		Window = SDL_CreateWindow( windowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight, SDL_WINDOW_VULKAN );
	
		if ( !Renderer::Init( Window, windowWidth, windowHeight, graphicsApi ) )
		{
			std::cout << "System::Init: couldn't initialise Renderer" << std::endl;
			return false;
		}

		Renderer::LoadEntities();

		return true;
	}

	constexpr size_t MaxFrames = 100U;
	std::list<float> frameratesCapped;
	std::list<float> frameratesUncapped;

	void UpdateFramerates( float capped, float uncapped )
	{
		static int counter = 0;
		
		if ( frameratesCapped.size() >= MaxFrames )
		{
			frameratesCapped.pop_front();
		}
		if ( frameratesUncapped.size() >= MaxFrames )
		{
			frameratesUncapped.pop_front();
		}

		frameratesCapped.push_back( capped );
		frameratesUncapped.push_back( uncapped );
	
		float averageCapped = 0.0f;
		float averageUncapped = 0.0f;

		for ( const auto& framerate : frameratesCapped )
			averageCapped += framerate;

		for ( const auto& framerate : frameratesUncapped )
			averageUncapped += framerate;

		averageCapped /= frameratesCapped.size();
		averageUncapped /= frameratesUncapped.size();

		if ( ++counter == 30 )
		{
			std::cout << "Capped fps:   " << std::setw( 4 ) << int( averageCapped ) << std::endl
				      << "Uncapped fps: " << std::setw( 4 ) << int( averageUncapped ) << std::endl;
			counter = 0;
		}
	}

	void Update( bool& outShouldQuit )
	{
		static float time = 0.0f;
		static float deltaTime = 1.0f / 60.0f;
		
		adm::TimerPreciseDouble t;
		
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

		Renderer::Update( deltaTime );
		Renderer::Render();

		double deltaT = t.GetElapsed( adm::TimeUnits::Seconds );

		double sleepFor = (1.0 / 90.0) - deltaT;
		if ( sleepFor > 0.0 )
		{
			uint32_t microseconds = uint32_t( sleepFor * 1000.0 * 1000.0 );
			std::this_thread::sleep_for( std::chrono::microseconds( microseconds ) );
		}

		deltaTime = t.GetElapsed( adm::TimeUnits::Seconds );
		UpdateFramerates( 1.0f / deltaTime, 1.0f / deltaT );
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
	nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::VULKAN;
	std::stringstream ss;
	bool unknownParams = false;

	// Linux has no DirectX obviously
	if constexpr ( adm::Platform == adm::Platforms::Windows )
	{
		ss << "Unrecognised parameter(s): " << std::endl;
		for ( int i = 0; i < argc; i++ )
		{
			if ( argv[i] == "-dx12"sv )
			{
				api = nvrhi::GraphicsAPI::D3D12;
				std::cout << "Using DirectX 12" << std::endl;
			}
			else if ( argv[i] == "-dx11"sv )
			{
				api = nvrhi::GraphicsAPI::D3D11;
				std::cout << "Using DirectX 11" << std::endl;
			}
			else if ( argv[i] == "-vk"sv )
			{
				api = nvrhi::GraphicsAPI::VULKAN;
				std::cout << "Vulkan is already enabled by default" << std::endl;
			}
			else
			{
				ss << "    " << argv[i] << std::endl;
				unknownParams = true;
			}
		}
		if ( unknownParams )
		{
			std::cout << ss.str() << std::endl;
		}
	}

	if ( !System::Init( "nBidia pls fox", 1600, 900, api ) )
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
