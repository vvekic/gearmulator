#include "emu88Editor.h"

#include "emu88Controller.h"
#include "emu88PluginProcessor.h"

#include "jucePluginEditorLib/pluginDataModel.h"

#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/Event.h"

namespace emu88JucePlugin
{
	Editor::Editor(AudioPluginAudioProcessor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
		, m_processor(_processor)
		, m_controller(dynamic_cast<Controller&>(_processor.getController()))
	{
	}

	Editor::~Editor() = default;

	void Editor::create()
	{
		jucePluginEditorLib::Editor::create();

		m_deviceModelSelector = findChild<juceRmlUi::ElemComboBox>("deviceModelSelector", false);

		if(m_deviceModelSelector)
		{
			updateDeviceModelSelector();

			// Change is only dispatched for a selection made by the user, not when we set the value
			juceRmlUi::EventListener::Add(m_deviceModelSelector, Rml::EventId::Change, [this](Rml::Event&)
			{
				onDeviceModelSelected();
			});

			// A turn of the mouse wheel over the selector would boot one board after the other.
			// Catch the wheel on the way down, before the selector gets to see it
			if(auto* parent = m_deviceModelSelector->GetParentNode())
			{
				juceRmlUi::EventListener::Add(parent, Rml::EventId::Mousescroll, [this](Rml::Event& _event)
				{
					for(const auto* e = _event.GetTargetElement(); e; e = e->GetParentNode())
					{
						if(e != m_deviceModelSelector)
							continue;
						_event.StopPropagation();
						return;
					}
				}, true);
			}
		}

		m_onDeviceModelChanged.set(m_processor.evDeviceModelChanged, [this](const emu88Lib::DeviceModel&)
		{
			updateDeviceModelSelector();
			updateDeviceState();
		});

		addClick("btRestart", [this](Rml::Event&)
		{
			// Rescans the ROMs, which may change what the selector offers even if the board fails
			m_processor.restartDevice();
			updateDeviceModelSelector();
			updateDeviceState();
		});

		addClick("btGmReset", [this](Rml::Event&)
		{
			m_controller.sendGmReset();
		});

		addClick("btGsReset", [this](Rml::Event&)
		{
			m_controller.sendGsReset();
		});

		addClick("btAllNotesOff", [this](Rml::Event&)
		{
			m_controller.sendAllNotesOff();
		});

		addClick("btRomFolder", [this](Rml::Event&)
		{
			const juce::File folder(juce::String::fromUTF8(getProcessor().getPublicRomFolder().c_str()));
			(void)folder.createDirectory();
			folder.revealToUser();
		});

		addClick("btSettings", [this](Rml::Event&)
		{
			showSettings(true);
		});
	}

	void Editor::initPluginDataModel(jucePluginEditorLib::PluginDataModel& _model)
	{
		jucePluginEditorLib::Editor::initPluginDataModel(_model);

		_model.set("deviceModel", getDeviceModelName());
		_model.set("deviceStatus", getDeviceStatus());
		_model.set("romFolder", getProcessor().getPublicRomFolder());
	}

	std::pair<std::string, std::string> Editor::getDemoRestrictionText() const
	{
		return {};
	}

	void Editor::onDeviceModelSelected()
	{
		const auto model = static_cast<emu88Lib::DeviceModel>(static_cast<int>(m_deviceModelSelector->getValue()));

		// On failure the processor has told the user why and kept the running board, show that
		// one again. On success, evDeviceModelChanged has updated everything already
		if(!m_processor.setDeviceModel(model))
			updateDeviceModelSelector();
	}

	void Editor::updateDeviceModelSelector() const
	{
		if(!m_deviceModelSelector)
			return;

		const auto current = m_processor.getDeviceModel();

		std::vector<juceRmlUi::ElemComboBox::Entry> entries;
		size_t currentIndex = 0;

		for(const auto model : emu88Lib::g_deviceMenuOrder)
		{
			if(!emu88Lib::isDeviceListed(model))
				continue;

			std::string name = emu88Lib::getDeviceProfile(model).displayName;

			if(!AudioPluginAudioProcessor::isModelAvailable(model))
				name += " (ROMs missing)";

			if(model == current)
				currentIndex = entries.size();

			entries.push_back({name, static_cast<int>(model)});
		}

		m_deviceModelSelector->setEntries(entries);

		// Unlike setValue, this refreshes the text even if the value stays the same and only the
		// entry changed, which is what happens when a rescan finds the ROMs of the current board
		m_deviceModelSelector->setSelectedIndex(currentIndex, false);
	}

	void Editor::updateDeviceState()
	{
		const auto& model = getPluginDataModel();
		if(!model)
			return;

		model->set("deviceModel", getDeviceModelName());
		model->set("deviceStatus", getDeviceStatus());
	}

	std::string Editor::getDeviceModelName() const
	{
		return emu88Lib::getDeviceProfile(m_processor.getDeviceModel()).displayName;
	}

	std::string Editor::getDeviceStatus() const
	{
		if(m_processor.isBoardRunning())
			return "Running";
		return "ROM set incomplete. Copy the ROMs into the ROM folder, then press Restart.";
	}
}
