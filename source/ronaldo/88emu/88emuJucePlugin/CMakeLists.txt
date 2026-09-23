cmake_minimum_required(VERSION 3.15)

project(88emuJucePlugin VERSION ${CMAKE_PROJECT_VERSION})

# The front panel is the standalone player's: its display renderer and its switch faces are used
# from there, so that both show the boards the same way. The player's own target is only there
# with the standalone format, so its files are compiled and embedded here directly. The panels
# themselves are in assets/: the player's, with its PLAYLIST column cut out by
# assets/makeNarrowPanels.py, which is also what makes the plugin 80 dp narrower than the player.
set(PLAYER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../88emuplayer)

set(SOURCES
	emu88Controller.cpp emu88Controller.h
	emu88Editor.cpp emu88Editor.h
	emu88PluginEditorState.cpp emu88PluginEditorState.h
	emu88PluginProcessor.cpp emu88PluginProcessor.h
	parameterDescriptions_88emu.json

	skins/emu88Default/emu88Default.rml
	skins/emu88Default/emu88Default.rcss

	assets/makeNarrowPanels.py

	${PLAYER_DIR}/ui/Emu88EditorBindings.h
	${PLAYER_DIR}/ui/Emu88EditorLcd.h
	${PLAYER_DIR}/ui/lcd_font.cpp
	${PLAYER_DIR}/ui/panel.cpp ${PLAYER_DIR}/ui/panel.hpp
)

set(PANEL_ASSETS
	assets/sc88_panel.png
	assets/sc88vl_panel.png
	assets/sc88pro_panel.png
	assets/sc8850_panel.png
	assets/sc55_panel.png
	assets/sc55mk2_panel.png
	assets/sc8820_panel.png
	assets/cm32p_panel.png
	assets/cm32l_panel.png
	assets/cm64_panel.png
	assets/sc88exp_panel.png
	assets/sc55pc_panel.png
	${PLAYER_DIR}/assets/sc8850_assets.png
	${PLAYER_DIR}/assets/button_all.svg
	${PLAYER_DIR}/assets/button_left.svg
	${PLAYER_DIR}/assets/button_right.svg
	${PLAYER_DIR}/assets/button_round.svg
	${PLAYER_DIR}/assets/button_led_on.svg
	${PLAYER_DIR}/assets/button_part_left.svg
	${PLAYER_DIR}/assets/button_part_right.svg
	${PLAYER_DIR}/assets/button_plain.svg
	${PLAYER_DIR}/assets/preview.svg
	${PLAYER_DIR}/assets/selector_led_off.svg
	${PLAYER_DIR}/assets/selector_led_on.svg
	${PLAYER_DIR}/assets/efx_led_off.svg
	${PLAYER_DIR}/assets/efx_led_on.svg
	${PLAYER_DIR}/assets/efx_led_green.svg
	${PLAYER_DIR}/assets/efx_led_amber.svg
	${PLAYER_DIR}/assets/cm_led_on.png
	${PLAYER_DIR}/assets/knob.png
	${PLAYER_DIR}/assets/value_knob.png
)

SET(ASSETS "parameterDescriptions_88emu.json" ${PANEL_ASSETS})

addSkin("Emu88" "emu88Default" "skins/emu88Default" "emu88Default.rml")

buildSkinHeader()

juce_add_binary_data(88emuJucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_SKINS})

createJucePlugin(88emuJucePlugin "Emu88" TRUE "Te88" 88emuJucePlugin_BinaryData 88lib)
