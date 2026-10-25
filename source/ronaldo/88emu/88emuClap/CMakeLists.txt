cmake_minimum_required(VERSION 3.15)

project(88emuClap VERSION ${CMAKE_PROJECT_VERSION})

# One CLAP instrument per device, without an editor, each a .clap file of its own. They find their
# ROMs in the folder the EMU88_ROM_PATH environment variable names. See emu88Clap.cpp.
#
# The names are those of emu88Lib::DeviceModel. The device is compiled in as DeviceModel::<name>,
# so a name that is not in the enum fails to build. The file is Emu88-<name>.clap and the CLAP id
# com.theusualsuspects.emu88.<name, lower case>, which hosts store in projects: never rename one.
set(EMU88_CLAP_DEVICES
	Sc88 Sc88VL Sc88Pro Sc8850
	Sc55Mk2 Sc55Mk1 Sc55St
	Cm300 Scb55 Rlp3237
	Sc155 Sc155Mk2 Xpgs Sc8820 Cm32p
	VeGsPro Scc1a Cm64 Cm32l
	Nu10b Miig5
	Mt32Old Mt32New Cm32ln
	Mv30 Sk88Pro Sc880
)

# The CLAP headers come with clap-juce-extensions, as a submodule of it
set(CLAP_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/source/3rdparty/clap-juce-extensions/clap-libs/clap/include)

if(NOT EXISTS ${CLAP_INCLUDE_DIR}/clap/clap.h)
	message(WARNING "88emuClap: no CLAP headers in ${CLAP_INCLUDE_DIR}, the per-device CLAP plugins are not built. Check out the submodules recursively.")
	return()
endif()

set(component Emu88Headless-CLAP)

add_custom_target(88emuClap_All)
set_property(TARGET 88emuClap_All PROPERTY FOLDER 88emuClap)

foreach(device IN LISTS EMU88_CLAP_DEVICES)
	set(target 88emuClap_${device})
	string(TOLOWER ${device} deviceLower)

	add_library(${target} MODULE emu88Clap.cpp)
	target_link_libraries(${target} PRIVATE 88lib)
	target_include_directories(${target} PRIVATE ${CLAP_INCLUDE_DIR})
	target_compile_definitions(${target} PRIVATE
		EMU88_CLAP_MODEL=${device}
		EMU88_CLAP_ID="com.theusualsuspects.emu88.${deviceLower}"
		EMU88_CLAP_VERSION="${CMAKE_PROJECT_VERSION}"
	)

	set_target_properties(${target} PROPERTIES
		OUTPUT_NAME Emu88-${device}
		PREFIX ""
		SUFFIX ".clap"
		FOLDER 88emuClap
	)

	# clap_entry is the one symbol a host needs. 88lib exports its C interface by default, which
	# would otherwise leave every .clap carrying a copy of it for the others to bind to.
	if(APPLE)
		set_target_properties(${target} PROPERTIES
			BUNDLE TRUE
			BUNDLE_EXTENSION clap
			SUFFIX ""
			MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
			MACOSX_BUNDLE_GUI_IDENTIFIER com.theusualsuspects.emu88.${deviceLower}
			MACOSX_BUNDLE_BUNDLE_NAME Emu88-${device}
			MACOSX_BUNDLE_BUNDLE_VERSION ${CMAKE_PROJECT_VERSION}
			MACOSX_BUNDLE_SHORT_VERSION_STRING ${CMAKE_PROJECT_VERSION}
		)
		target_link_options(${target} PRIVATE LINKER:-exported_symbol,_clap_entry)
	elseif(UNIX)
		target_link_options(${target} PRIVATE LINKER:--exclude-libs,ALL)
	endif()

	if(MSVC OR APPLE)
		install(TARGETS ${target} DESTINATION . COMPONENT ${component})
	else()
		install(TARGETS ${target} LIBRARY DESTINATION lib/clap/ COMPONENT ${component})
	endif()

	add_dependencies(88emuClap_All ${target})
endforeach()

if(APPLE)
	installMacSetupScript(. ${component})
endif()
