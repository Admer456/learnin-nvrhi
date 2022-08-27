// SPDX-License-Identifier: MIT

#include "Common.hpp"
#include "gltf.h"

namespace Model
{
	uint32_t DrawSurface::IndexBytes() const
	{
		return vertexIndices.size() * sizeof( uint32_t );
	}

	const uint32_t* DrawSurface::GetIndexData() const
	{
		return vertexIndices.data();
	}

	uint32_t DrawSurface::VertexBytes() const
	{
		return vertexData.size() * sizeof( DrawVertex );
	}

	const DrawVertex* DrawSurface::GetVertexData() const
	{
		return vertexData.data();
	}

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

					surface.vertexData.push_back( vertex );
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

	std::vector<RenderModel> RenderModels;

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
}
