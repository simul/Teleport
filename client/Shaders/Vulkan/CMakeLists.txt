cmake_minimum_required(VERSION 2.9)

file(GLOB shaders "../*.sfx" )
file(GLOB shader_includes "../*.sl" )
file(GLOB global_shader_includes "${SIMUL_PLATFORM_DIR}/CrossPlatform/Shaders/*.sl")
set( JSON_FILE_VULKAN "${SIMUL_PLATFORM_DIR}/Vulkan/Sfx/Vulkan.json" )
 
add_sfx_shader_project( VulkanShaders ${JSON_FILE_VULKAN} INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/Sfx" "${SIMUL_PLATFORM_DIR}/CrossPlatform/Shaders" "${SIMUL_PLATFORM_DIR}/Vulkan/Sfx" SOURCES ${shaders} ${shader_includes} ${global_shader_includes} ${JSON_FILE_VULKAN} OUTPUT "${CMAKE_BINARY_DIR}/firstparty/Platform/Vulkan/shaderbin" DEFINES FOLDER Shaders )