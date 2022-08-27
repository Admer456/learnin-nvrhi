
cmake_minimum_required( VERSION 3.16 )

project( NvrhiTest )

## C++17 cuz' adm-utils
set( CMAKE_CXX_STANDARD 17 )

## I have a habit of setting a root variable cuz' I'm lazy to type CMAKE_CURRENT_SOURCE_DIR every time
## In projects like these, which aren't meant to be used as dependencies, I prefix stuff with THE_,
## cuz' it's THE stuff, there won't be any other
set( THE_ROOT ${CMAKE_CURRENT_SOURCE_DIR} )
set_property( GLOBAL PROPERTY USE_FOLDERS ON )

## Set up adm-utils, SDL2, GLM and NVRHI

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
option( NVRHI_WITH_NVAPI OFF )
option( NVRHI_WITH_RTXMU OFF )
## NVRHI-SC will be built so you can compile the shaders here
option( NVRHI_WITH_SHADER_COMPILER ON )
## No DirectX on Linux
if ( UNIX )
	option( NVRHI_WITH_DX11 OFF )
	option( NVRHI_WITH_DX12 OFF )
endif()

if ( NVRHI_WITH_VULKAN )
	## Vulkan itsyelf
	find_package( Vulkan REQUIRED )
endif()

add_subdirectory( external/nvrhi )

if ( NVRHI_WITH_DX11 )
	set_target_properties( nvrhi_d3d11 PROPERTIES FOLDER "Libs" )
endif()
if ( NVRHI_WITH_DX12 )
	set_target_properties( nvrhi_d3d12 PROPERTIES FOLDER "Libs" )
endif()
if ( NVRHI_WITH_VULKAN )
	set_target_properties( nvrhi_vk PROPERTIES FOLDER "Libs" )
endif()
set_target_properties( nvrhi PROPERTIES FOLDER "Libs" )

## The sources
set( THE_SOURCES
	src/Common.hpp
	src/DeviceManager.cpp
	src/DeviceManager.hpp
	src/Main.cpp
	src/Model.cpp
	src/Texture.cpp 
	src/Shader.cpp
	src/System.cpp )

if ( NVRHI_WITH_DX11 )
	set( THE_SOURCES
		${THE_SOURCES}
		src/DeviceManagerDX11.cpp )
endif()

if ( NVRHI_WITH_DX12 )
	set( THE_SOURCES
		${THE_SOURCES}
		src/DeviceManagerDX12.cpp )
endif()

if ( NVRHI_WITH_VULKAN )
	set( THE_SOURCES
		${THE_SOURCES}
		src/DeviceManagerVK.cpp )
endif()

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
	external/json/include
	external/fx-gltf
	external/nvrhi/include
	external/stb )

if ( NVRHI_WITH_VULKAN )
	target_include_directories( NvrhiTest PRIVATE ${Vulkan_INCLUDE_DIR} )
endif()

## Link against SDL2 libs
target_link_libraries( NvrhiTest PRIVATE ${SDL2_LIBRARIES} AdmUtils nvrhi )

set( NVRHITEST_DEFINES "" )
if ( WIN32 )
	## Vulkan headers include winmindef.h which messes with std::min and std::max
	set( NVRHITEST_DEFINES ${NVRHITEST_DEFINES} NOMINMAX VK_USE_PLATFORM_WIN32_KHR=1 )
elseif( UNIX )
	set( NVRHITEST_DEFINES ${NVRHITEST_DEFINES} VK_USE_PLATFORM_XLIB_KHR=1 )
endif()

if ( NVRHI_WITH_DX11 )
	set( NVRHITEST_DEFINES ${NVRHITEST_DEFINES} USE_DX11=1 )
	target_link_libraries( NvrhiTest PRIVATE nvrhi_d3d11 )
endif()
if ( NVRHI_WITH_DX12 )
	set( NVRHITEST_DEFINES ${NVRHITEST_DEFINES} USE_DX12=1 )
	target_link_libraries( NvrhiTest PRIVATE nvrhi_d3d12 )
endif()
if ( NVRHI_WITH_VULKAN )
	set( NVRHITEST_DEFINES ${NVRHITEST_DEFINES} USE_VK=1 )
	target_link_libraries( NvrhiTest PRIVATE nvrhi_vk Vulkan::Vulkan )
endif()

target_compile_definitions( nvrhi PRIVATE ${NVRHITEST_DEFINES} )
target_compile_definitions( NvrhiTest PRIVATE ${NVRHITEST_DEFINES} )

if ( NVRHI_WITH_DX11 )
	target_compile_definitions( nvrhi_d3d11 PRIVATE ${NVRHITEST_DEFINES} )
endif()
if ( NVRHI_WITH_DX12 )
	target_compile_definitions( nvrhi_d3d12 PRIVATE ${NVRHITEST_DEFINES} )
endif()
if ( NVRHI_WITH_VULKAN )
	target_compile_definitions( nvrhi_vk PRIVATE ${NVRHITEST_DEFINES} )
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

function( install_shader rendering_api shader_dir shader output_dir )
	set( shader_path ${shader_dir}/${rendering_api}/${shader}.bin )

	if ( EXISTS ${shader_path} )
		install( FILES ${shader_path}
			DESTINATION ${output_dir}/${rendering_api}/ )
	endif()
endfunction( install_shader )

## CMake isn't ideal for copying shaders, I'll probably do modifications to the shader
## compiler frontend or write a script to do that
function( install_shaders rendering_api )
	set( shader_dir ${THE_ROOT}/assets/shaders )
	set( out_dir ${THE_ROOT}/bin/assets/shaders )

	install_shader( ${rendering_api} ${shader_dir} default_main_ps ${out_dir} )
	install_shader( ${rendering_api} ${shader_dir} default_main_vs ${out_dir} )
	install_shader( ${rendering_api} ${shader_dir} screen_main_ps ${out_dir} )
	install_shader( ${rendering_api} ${shader_dir} screen_main_vs ${out_dir} )
endfunction( install_shaders )

install_shaders( dx11 )
install_shaders( dx12 )
install_shaders( vk )
