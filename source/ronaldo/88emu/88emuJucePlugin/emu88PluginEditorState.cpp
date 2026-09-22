#include "emu88PluginEditorState.h"

#include "emu88Controller.h"
#include "emu88Editor.h"
#include "emu88PluginProcessor.h"

#include "88lib/deviceModel.h"

#include "juceRmlUi/rmlMenu.h"

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

	void PluginEditorState::initContextMenu(juceRmlUi::Menu& _menu)
	{
		auto& controller = dynamic_cast<Controller&>(m_emu88Processor.getController());

		// The MT-32 and CM boards know one reset, their own; the GM and GS ones do nothing there
		if(emu88Lib::isRolandLaFamily(m_emu88Processor.getDeviceModel()))
		{
			_menu.addEntry("Send All Parameters Reset", [&controller] { controller.sendRolandLaReset(); });
		}
		else
		{
			_menu.addEntry("Send GM Reset", [&controller] { controller.sendGmReset(); });
			_menu.addEntry("Send GM2 Reset", [&controller] { controller.sendGm2Reset(); });
			_menu.addEntry("Send GS Reset", [&controller] { controller.sendGsReset(); });
		}

		_menu.addEntry("Send All Notes Off", [&controller] { controller.sendAllNotesOff(); });

		_menu.addEntry("Restart Device", [this]
		{
			// Booting takes a moment, let the menu close first
			juce::MessageManager::callAsync([this]
			{
				m_emu88Processor.restartDevice();
			});
		});

		_menu.addSeparator();

		_menu.addEntry("Open ROM Folder...", [this]
		{
			const juce::File folder(juce::String::fromUTF8(m_emu88Processor.getPublicRomFolder().c_str()));
			(void)folder.createDirectory();
			folder.revealToUser();
		});
	}
}
