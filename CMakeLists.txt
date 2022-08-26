
cmake_minimum_required( VERSION 3.16 )

project( NvrhiTest )

## C++17 cuz' adm-utils
set( CMAKE_CXX_STANDARD 17 )

## I have a habit of setting a root variable cuz' I'm lazy to type CMAKE_CURRENT_SOURCE_DIR every time
## In projects like these, which aren't meant to be used as dependencies, I prefix stuff with THE_,
## cuz' it's THE stuff, there won't be any other
set( THE_ROOT ${CMAKE_CURRENT_SOURCE_DIR} )
set_property( GLOBAL PROPERTY USE_FOLDERS ON )

## Set up adm-utils, SDL2, GLM and Vookoo

## adm-utils
add_subdirectory( external/adm-utils )
set_target_properties( AdmUtils PROPERTIES
	FOLDER "Libs" )

## SDL2
if( UNIX )
	find_package( SDL2 REQUIRED )
elseif( WIN32 )
	## Note that I've only included 64-bit SDL2 binaries
	set( SDL2_INCLUDE_DIRS
		${THE_ROOT}/external/SDL2/include )

	set( SDL2_LIBRARIES
		${THE_ROOT}/external/SDL2/lib/SDL2.lib
		${THE_ROOT}/external/SDL2/lib/SDL2main.lib )
else()
	message( FATAL_ERROR "This platform is not supported" )
endif()

## GLM
set( GLM_INCLUDE_DIRS
	${THE_ROOT}/external/glm )

## NVRHI
option( NVRHI_BUILD_SHARED OFF )
option( NVRHI_WITH_SHADER_COMPILER OFF )
option( NVRHI_WITH_DX11 OFF )
option( NVRHI_WITH_DX12 OFF )
add_subdirectory( external/nvrhi )
set_target_properties( nvrhi nvrhi_vk PROPERTIES
	FOLDER "Libs" )

## Vulkan itsyelf
find_package( Vulkan REQUIRED )

## The sources
set( THE_SOURCES
	src/Common.hpp
	src/DeviceManager.cpp
	src/DeviceManager.hpp
	src/DeviceManagerVK.cpp
	src/Main.cpp
	src/Model.cpp
	src/Texture.cpp 
	src/Shader.cpp
	src/System.cpp )

## Folder organisation
source_group( TREE ${THE_ROOT} FILES ${THE_SOURCES} )

## The .exe
add_executable( NvrhiTest ${THE_SOURCES} )

## Include dirs
target_include_directories( NvrhiTest PRIVATE
	${THE_ROOT}
	${ADMUTIL_INCLUDE_DIRECTORY}
	${SDL2_INCLUDE_DIRS}
	${GLM_INCLUDE_DIRS}
	${Vulkan_INCLUDE_DIR}
	external/json/include
	external/fx-gltf
	external/nvrhi/include
	external/stb )

## Link against SDL2 libs
target_link_libraries( NvrhiTest PRIVATE ${SDL2_LIBRARIES} AdmUtils nvrhi nvrhi_vk Vulkan::Vulkan )

## Use Vulkan
target_compile_definitions( nvrhi PRIVATE USE_VK=1 )
target_compile_definitions( nvrhi_vk PRIVATE USE_VK=1 )
target_compile_definitions( NvrhiTest PRIVATE USE_VK=1 )

## Vulkan headers include winmindef.h which messes with std::min and std::max
if ( WIN32 )
	target_compile_definitions( nvrhi PRIVATE NOMINMAX VK_USE_PLATFORM_WIN32_KHR=1 )
	target_compile_definitions( nvrhi_vk PRIVATE NOMINMAX VK_USE_PLATFORM_WIN32_KHR=1 )
	target_compile_definitions( NvrhiTest PRIVATE NOMINMAX VK_USE_PLATFORM_WIN32_KHR=1 )
elseif ( UNIX )
	target_compile_definitions( nvrhi PRIVATE VK_USE_PLATFORM_XLIB_KHR=1 )
	target_compile_definitions( nvrhi_vk PRIVATE VK_USE_PLATFORM_XLIB_KHR=1 )
	target_compile_definitions( NvrhiTest PRIVATE VK_USE_PLATFORM_XLIB_KHR=1 )
endif()

## Output here
install( TARGETS NvrhiTest
	RUNTIME DESTINATION ${THE_ROOT}/bin/
	LIBRARY DESTINATION ${THE_ROOT}/bin/ )

## On Windows, copy SDL2.dll and the .pdb
if( WIN32 )
	install( FILES ${THE_ROOT}/external/SDL2/lib/SDL2.dll
		DESTINATION ${THE_ROOT}/bin/ )

	install( FILES $<TARGET_PDB_FILE:NvrhiTest> 
		DESTINATION ${THE_ROOT}/bin/ OPTIONAL )
endif()

install( FILES 
	${THE_ROOT}/assets/default.png
	${THE_ROOT}/assets/MossPatch.glb
	${THE_ROOT}/assets/splotch.glb
	${THE_ROOT}/assets/pentagon.glb
	${THE_ROOT}/assets/TestEnvironment.glb
	${THE_ROOT}/assets/Trongle.glb
	${THE_ROOT}/assets/256floor.png
	${THE_ROOT}/assets/moss.png
	DESTINATION ${THE_ROOT}/bin/assets/ )

## Copy over shaders too
install( FILES 
	${THE_ROOT}/assets/shaders/default_main_ps.bin
	${THE_ROOT}/assets/shaders/default_main_vs.bin
	${THE_ROOT}/assets/shaders/screen_main_vs.bin
	${THE_ROOT}/assets/shaders/screen_main_ps.bin
	DESTINATION ${THE_ROOT}/bin/assets/shaders/ )
