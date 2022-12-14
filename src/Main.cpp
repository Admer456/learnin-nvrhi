// SPDX-License-Identifier: MIT

#include <thread>
#include <string_view>
using namespace std::string_literals;
using namespace std::string_view_literals;

#include "Common.hpp"
#include "DeviceManager.hpp"

#include "SDL.h"

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
			adm::Mat4 transform;

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

	// Data that changes per frame
	struct ConstantBufferData
	{
		adm::Mat4 viewMatrix;
		adm::Mat4 projectionMatrix;
		float time;
	};
	// Data that changes per render entity
	struct ConstantBufferDataEntity
	{
		adm::Mat4 entityMatrix;
	};
	// There is also data that changes per render surface,
	// i.e. the texture(s), look at Common.hpp::Model::RenderSurface

	constexpr float MaxViewDistance = 100.0f;
	constexpr float deg2rad = (3.14159f) / 180.0f;

	ConstantBufferData TransformData
	{
		// View matrix
		adm::Mat4::Identity,
		//adm::Mat4::View( adm::Vec3{ 0.0f, 0.0f, 0.0f }, adm::Vec3{ -45.0f, 45.0f, 0.0f } ),
		// Projection matrix
		adm::Mat4::Perspective( 105.0f * deg2rad, 16.0f / 9.0f, 0.01f, MaxViewDistance ),
		//adm::Mat4::Orthographic( -10.0f, 10.0f, 10.0f, -10.0f, 0.01f, MaxViewDistance ),
		// Time
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
		// Enable these if you have Vulkan or DirectX 12 validation layers and your device supports them
		//dcp.enableDebugRuntime = true;
		//dcp.enableNvrhiValidationLayer = true;
		// SDL2 is pretty tricky regarding the swap chain format, but I think I figured it out
		dcp.swapChainFormat = nvrhi::Format::BGRA8_UNORM;
		dcp.swapChainSampleCount = 1; // MSAA
		dcp.swapChainBufferCount = 3; // double buffering or, in this case, triple buffering
		dcp.refreshRate = 60; // this has no effect since V-sync is off

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

		// Load the shaders from a SPIR-V/DXIL/DXBC binary that we'll produce with NVRHI-SC
		// A way that is IMO better would be to modify NVRHI-SC to output .dxil, .dxbc and .spv instead of .bin for everything
		// I can always for the shader compiler frontend, so yeah, we'll see
		const auto loadShaders = [&graphicsApi]( const char* vertexBinaryFile, const char* pixelBinaryFile, 
			nvrhi::ShaderHandle& outVertexShader, nvrhi::ShaderHandle& outPixelShader )
		{
			std::string vertexBinaryPath, pixelBinaryPath;
			Shader::ShaderBinary vertexBinary, pixelBinary;

			switch ( graphicsApi )
			{
			case nvrhi::GraphicsAPI::D3D11:
				vertexBinaryPath = "assets/shaders/dx11/"s + vertexBinaryFile;
				pixelBinaryPath  = "assets/shaders/dx11/"s + pixelBinaryFile;
				break;
			case nvrhi::GraphicsAPI::D3D12:
				vertexBinaryPath = "assets/shaders/dx12/"s + vertexBinaryFile;
				pixelBinaryPath  = "assets/shaders/dx12/"s + pixelBinaryFile;
				break;
			case nvrhi::GraphicsAPI::VULKAN:
				vertexBinaryPath = "assets/shaders/vk/"s + vertexBinaryFile;
				pixelBinaryPath  = "assets/shaders/vk/"s + pixelBinaryFile;
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
		// If you're coming from OpenGL, this is similar to glVertexAttribPointer, but way nicer to work with IMO
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
			.setOffset( sizeof( adm::Vec3 ) )
			.setElementStride( sizeof( Model::DrawVertex ) ),

			nvrhi::VertexAttributeDesc()
			.setName( "TEXCOORD" )
			.setFormat( nvrhi::Format::RG32_FLOAT )
			.setOffset( sizeof( adm::Vec3 ) + sizeof( adm::Vec3 ) )
			.setElementStride( sizeof( Model::DrawVertex ) ),

			nvrhi::VertexAttributeDesc()
			.setName( "COLOR" )
			.setFormat( nvrhi::Format::RGBA32_FLOAT )
			.setOffset( sizeof( adm::Vec3 ) + sizeof( adm::Vec3 ) + sizeof( adm::Vec2 ) )
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
		// 
		// 1) create a sampler that will determine how textures are filtered (nearest, bilinear etc.)
		// 2) create a colour and depth texture for our framebuffer, so we can render our scene with depth testing
		// 2.1) the two textures will also be used as inputs for the ScreenQuad shader, so we can do post-processing
		// 3) create the framebuffers
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

		Scene::MainFramebufferColourImage = Device->createTexture( colourAttachmentDesc );
		if ( !Check( Scene::MainFramebufferColourImage, "Failed to create Scene::MainFramebufferColourImage" ) )
			return false;

		auto depthAttachmentDesc = colourAttachmentDesc
			.setFormat( (graphicsApi == nvrhi::GraphicsAPI::D3D11) ? nvrhi::Format::D24S8 : nvrhi::Format::D32 )
			.setDimension( nvrhi::TextureDimension::Texture2D )
			.setInitialState( DepthBufferStates )
			.setDebugName( "Depth attachment image" );

		Scene::MainFramebufferDepthImage = Device->createTexture( depthAttachmentDesc );
		if ( !Check( Scene::MainFramebufferDepthImage, "Failed to create Scene::MainFramebufferDepthImage" ) )
			return false;

		// ==========================================================================================================
		// FRAMEBUFFER CREATION
		// ==========================================================================================================
		auto mainFramebufferDesc = nvrhi::FramebufferDesc()
			.addColorAttachment( Scene::MainFramebufferColourImage )
			.setDepthAttachment( Scene::MainFramebufferDepthImage );

		Scene::MainFramebuffer = Device->createFramebuffer( mainFramebufferDesc );
		if ( !Check( Scene::MainFramebuffer, "Failed to create Scene::MainFramebuffer" ) )
			return false;

		const auto printFramebufferInfo = []( const nvrhi::FramebufferInfo& fbInfo, const char* name )
		{
			std::cout << "Framebuffer: " << name << std::endl
				<< "  * Size:           " << fbInfo.width << "x" << fbInfo.height << std::endl
				<< "  * Sample count:   " << fbInfo.sampleCount << std::endl
				<< "  * Sample quality: " << fbInfo.sampleQuality << std::endl
				<< "  * Colour format:  " << nvrhi::utils::FormatToString( fbInfo.colorFormats[0] ) << std::endl
				<< "  * Depth format:   " << nvrhi::utils::FormatToString( fbInfo.depthFormat ) << std::endl;
		};

		printFramebufferInfo( Scene::MainFramebuffer->getFramebufferInfo(), "Main framebuffer" );
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
		// 
		// Layout bindings describe what kinds of parameters are passed to the shader
		// ==========================================================================================================
		nvrhi::BindingLayoutDesc layoutDesc;
		layoutDesc.registerSpace = 0U;
		layoutDesc.visibility = nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel;
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
		}; // We can use CreateBindingSetAndLayout because we know the set in advance here
		nvrhi::utils::CreateBindingSetAndLayout( Device, nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel, 0, setDesc, ScreenQuad::BindingLayout, ScreenQuad::BindingSet );

		// ==========================================================================================================
		// PIPELINE CREATION
		// 
		// The pipeline basically glues everything together: the shaders you wanna use, what kinds of parameters
		// to pass to them, the vertex layout, configure things like depth testing, alpha blending etc.
		// 
		// Here we have 2 pipelines, one for the screen quad (depth testing disabled because the backbuffer does
		// not have a depth buffer), which draws into the backbuffer, and one for the scene, which draws into our
		// own framebuffer which has depth testing and all. This setup allows for easy post-processing
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

		// If you get errors in DX12 here, you are likely missing dxil.dll. You should have dxc.exe, dxcompiler.dll AND dxil.dll,
		// as the 3rd one will perform shader validation/signature, and DX12 doesn't like unsigned shaders by default (you'd need to modify NVRHI to allow that)
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
			Scene::BindingLayoutEntity
		};

		Scene::Pipeline = Device->createGraphicsPipeline( pipelineDesc, Scene::MainFramebuffer );
		if ( !Check( Scene::Pipeline, "Could not create Scene::Pipeline" ) )
			return false;

		return true;
	}

	void LoadEntities()
	{
		const auto createEntity = []( const char* modelPath, adm::Vec3 position, adm::Mat4 orientation )
		{
			RenderEntities.push_back( {} );
			auto& re = RenderEntities.back();

			re.renderModelIndex = Model::LoadRenderModelFromGltf( modelPath );
			//re.transform = glm::translate( glm::identity<adm::Mat4>(), position ) * orientation;
			// We'll need a Mat4::Translation one day
			re.transform = orientation;
		};

		// Create default texture
		Texture::FindOrCreateMaterial( nullptr );

		createEntity( "assets/TestEnvironment.glb", { 0.0f, 0.0f, 0.0f }, adm::Mat4::Identity );
		createEntity( "assets/MossPatch.glb", { 0.0f, 0.0f, 0.0f }, adm::Mat4::Identity );
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
			CommandList->writeBuffer( Scene::ConstantBufferEntity, &renderEntity.transform, sizeof( adm::Mat4 ) );

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

	// Adapted from glm::eulerAnglesXYZ by trying out different combinations until I got what I wanted
	// Positive pitch will make the forward axis go up
	// Positive yaw will make forward and right spin counter-clockwise (if you want it the other way, put -angles.y
	// Positive roll will make the up axis rotate clockwise about the forward axis
	void CalculateDirections( const adm::Vec3& angles, adm::Vec3& forward, adm::Vec3& right, adm::Vec3& up )
	{
		const float cosPitch = std::cos( -angles.x * deg2rad );
		const float cosYaw = std::cos( angles.y * deg2rad );
		const float cosRoll = std::cos( angles.z * deg2rad );

		const float sinPitch = std::sin( -angles.x * deg2rad );
		const float sinYaw = std::sin( angles.y * deg2rad );
		const float sinRoll = std::sin( angles.z * deg2rad );

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
	
		right = forward.Cross( up );
	}

	// Adapted from glm::lookAt
	adm::Mat4 CalculateViewMatrix( const adm::Vec3& position, const adm::Vec3& angles )
	{
		//return adm::Mat4::Identity;
		return adm::Mat4::View( position, angles );
	}

	void Update( float deltaTime )
	{
		static adm::Vec3 viewPosition{};
		static adm::Vec3 viewAngles{ 0.0f, 0.0f, 0.0f };

		adm::Vec3 viewForward, viewRight, viewUp;
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

		// Weirdly enough this won't actually result in 90fps, but something like 83 or 85
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
	
	// Linux has no DirectX obviously
	if constexpr ( adm::Platform == adm::Platforms::Windows )
	{
		std::stringstream ss;
		bool unknownParams = false;

		ss << "Unrecognised parameter(s): " << std::endl;
		for ( int i = 1; i < argc; i++ )
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
