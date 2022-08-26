
#include "Common.hpp"

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
		file.read( reinterpret_cast<char*>(data), fileSize );
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
