cmake_minimum_required(VERSION 3.15)

add_subdirectory(88lib)

if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)
	add_subdirectory(88emuJucePlugin)
endif()

if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN AND USE_Standalone)
	add_subdirectory(88emuplayer)
endif()
