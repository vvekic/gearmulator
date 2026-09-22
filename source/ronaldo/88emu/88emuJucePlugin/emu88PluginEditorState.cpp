#include "emu88PluginEditorState.h"

#include "emu88Editor.h"
#include "emu88PluginProcessor.h"

#include "skins.h"

namespace emu88JucePlugin
{
	PluginEditorState::PluginEditorState(AudioPluginAudioProcessor& _processor)
		: jucePluginEditorLib::PluginEditorState(_processor, _processor.getController(), g_includedSkins)
		, m_emu88Processor(_processor)
	{
		loadDefaultSkin();
	}

	jucePluginEditorLib::Editor* PluginEditorState::createEditor(const jucePluginEditorLib::Skin& _skin)
	{
		return new Editor(m_emu88Processor, _skin);
	}
}
