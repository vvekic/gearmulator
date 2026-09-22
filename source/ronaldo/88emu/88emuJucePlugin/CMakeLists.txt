cmake_minimum_required(VERSION 3.15)

project(88emuJucePlugin VERSION ${CMAKE_PROJECT_VERSION})

set(SOURCES
	emu88Controller.cpp emu88Controller.h
	emu88Editor.cpp emu88Editor.h
	emu88PluginEditorState.cpp emu88PluginEditorState.h
	emu88PluginProcessor.cpp emu88PluginProcessor.h
	parameterDescriptions_88emu.json

	skins/emu88Default/emu88Default.rml
	skins/emu88Default/emu88Default.rcss
)

SET(ASSETS "parameterDescriptions_88emu.json")

addSkin("Emu88" "emu88Default" "skins/emu88Default" "emu88Default.rml")

buildSkinHeader()

juce_add_binary_data(88emuJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_SKINS})

createJucePlugin(88emuJucePlugin "Emu88" TRUE "Te88" 88emuJucePlugin_BinaryData 88lib)
