cmake_minimum_required(VERSION 3.15)

add_subdirectory(88lib)

if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN AND USE_Standalone)
	add_subdirectory(88emuplayer)
endif()

# Not JUCE: one CLAP instrument per device, without an editor
option(${CMAKE_PROJECT_NAME}_BUILD_88EMU_CLAP "Build a CLAP plugin without an editor for every 88emu device" on)
if(${CMAKE_PROJECT_NAME}_BUILD_88EMU_CLAP AND NOT EMSCRIPTEN AND NOT ANDROID)
	add_subdirectory(88emuClap)
endif()
