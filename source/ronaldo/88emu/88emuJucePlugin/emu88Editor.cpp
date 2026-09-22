#include "emu88Editor.h"

#include "emu88Controller.h"
#include "emu88PluginProcessor.h"

#include "88emuplayer/ui/Emu88EditorBindings.h"
#include "88emuplayer/ui/Emu88EditorLcd.h"

#include "88lib/rom/romloader.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlElemKnob.h"
#include "juceRmlUi/rmlElemValue.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInterfaces.h"
#include "juceRmlUi/rmlMenu.h"

#include "RmlUi/Core/ElementDocument.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <set>
#include <string_view>

namespace emu88JucePlugin
{
	using namespace emu88Player::editor;

	namespace
	{
		constexpr int g_timerHz = 30;

		// The subscription for the panel lasts SysexRemoteControl::g_subscriptionSeconds, renew
		// it well before that
		constexpr uint32_t g_subscriptionRenewTicks = g_timerHz;

		// The editors alive, for work handed to the message thread from elsewhere
		std::mutex& getInstancesMutex()
		{
			static std::mutex m;
			return m;
		}

		std::set<const void*>& getInstances()
		{
			static std::set<const void*> s;
			return s;
		}
	}

	Editor::Editor(AudioPluginAudioProcessor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
		, m_processor(_processor)
		, m_controller(dynamic_cast<Controller&>(_processor.getController()))
	{
		const std::lock_guard lock(getInstancesMutex());
		getInstances().insert(this);
	}

	Editor::~Editor()
	{
		{
			const std::lock_guard lock(getInstancesMutex());
			getInstances().erase(this);
		}

		stopTimer();

		// A switch held while the editor goes away would stay down on the board
		m_pointerButtons = 0;
		m_keyboardButtons = 0;
		sendButtons();

		// The displays paint from the render thread
		if(auto* rml = getRmlComponent())
		{
			juceRmlUi::RmlInterfaces::ScopedAccess access(*rml);
			m_lcd.reset();
			m_lcd2.reset();
		}
	}

	void Editor::create()
	{
		jucePluginEditorLib::Editor::create();

		wirePanel();

		m_onDeviceModelChanged.set(m_processor.evDeviceModelChanged, [this](const emu88Lib::DeviceModel& _model)
		{
			if(juce::MessageManager::getInstance()->isThisTheMessageThread())
			{
				onDeviceModelChanged(_model);
				return;
			}

			// A project restored from the host's thread switches the board there
			juce::MessageManager::callAsync([this, _model]
			{
				{
					const std::lock_guard lock(getInstancesMutex());
					if(getInstances().find(this) == getInstances().end())
						return;
				}
				onDeviceModelChanged(_model);
			});
		});

		// A key or mouse button held while the panel loses the focus would never see its release,
		// and the firmware would keep the switch pressed
		if(auto* rml = getRmlComponent())
		{
			m_onFocusLost.set(rml->evFocusLost, [this](juceRmlUi::RmlComponent* const&)
			{
				releaseButtons();
			});
		}

		m_controller.subscribePanel();

		startTimerHz(g_timerHz);
	}

	std::pair<std::string, std::string> Editor::getDemoRestrictionText() const
	{
		return {};
	}

	void Editor::timerCallback()
	{
		if(++m_ticksSinceSubscription >= g_subscriptionRenewTicks)
		{
			m_ticksSinceSubscription = 0;
			m_controller.subscribePanel();
		}

		updateFromController();
		updateVolumeKnob();
	}

	void Editor::wirePanel()
	{
		juceRmlUi::RmlInterfaces::ScopedAccess access(*getRmlComponent());

		auto* document = getDocument();
		const auto model = m_processor.getDeviceModel();

		m_shownBoardRunning = m_processor.isBoardRunning();

		updateDeviceSkin(model);
		updatePowerVisuals();

		if(auto* lcd = document->GetElementById("hardwareLcd"))
		{
			m_lcd = std::make_unique<emu88Player::HardwareLcd>(*lcd);
			m_lcd->reset(model);
		}

		if(auto* lcd = document->GetElementById("hardwareLcd2"))
		{
			m_lcd2 = std::make_unique<emu88Player::HardwareLcd>(*lcd);
			m_lcd2->reset(model, 1);
		}

		// Whatever the board showed while the editor was closed, until it sends something new. Unless
		// the board has been switched meanwhile, the panel is the old one's then
		if(m_controller.getPanelModel() != model)
			m_controller.clearPanel(model);
		updateFromController();

		if(auto* device = document->GetElementById("btDeviceSelect"))
		{
			juceRmlUi::EventListener::Add(device, Rml::EventId::Click, [this](const Rml::Event& _event)
			{
				openDeviceMenu(_event);
			});
		}

		wireButtons();
		wireValueKnob();
		wireVolumeKnob();

		refreshButtonElements(model);
		refreshLedElements(model);

		juceRmlUi::EventListener::Add(document, Rml::EventId::Mouseup, [this](Rml::Event&)
		{
			m_pointerButtons = 0;
			sendButtons();
			updateButtonVisuals();
		});

		juceRmlUi::EventListener::Add(document, Rml::EventId::Keydown, [this](Rml::Event& _event)
		{
			if(handleKey(_event, true))
				_event.StopPropagation();
		});

		juceRmlUi::EventListener::Add(document, Rml::EventId::Keyup, [this](Rml::Event& _event)
		{
			if(handleKey(_event, false))
				_event.StopPropagation();
		});
	}

	void Editor::wireButtons()
	{
		auto* document = getDocument();

		const auto wireButton = [this, document](const ButtonBinding& _binding)
		{
			// The VALUE encoder is a knob that can be pushed, see wireValueKnob()
			if(!_binding.element || std::string_view(_binding.element) == "bt8850Value")
				return;

			auto* element = document->GetElementById(_binding.element);
			if(!element)
				return;

			const auto button = static_cast<uint32_t>(_binding.button);

			// The release comes from the document, the pointer may have left the switch by then
			juceRmlUi::EventListener::Add(element, Rml::EventId::Mousedown, [this, button](const Rml::Event& _event)
			{
				if(!juceRmlUi::helper::isContextMenu(_event))
					setPointerButton(button, true);
			});
		};

		for(const auto& binding : g_sc88Buttons)
			wireButton(binding);
		for(const auto& binding : g_sc8850Buttons)
			wireButton(binding);
	}

	void Editor::wireValueKnob()
	{
		auto* value = getDocument()->GetElementById("bt8850Value");
		if(!value)
			return;

		// Push + turn is the firmware's coarse mode, every detent steps the value by ten. So a drag
		// must turn the encoder without pushing it. Only a click that did not move pushes, as a pulse
		// long enough for the panel scan, started after the document-wide mouseup has released the
		// pointer buttons
		juceRmlUi::EventListener::Add(value, Rml::EventId::Mousedown, [this](const Rml::Event& _event)
		{
			const auto position = juceRmlUi::helper::getMousePos(_event);
			m_valueKnobDownX = position.x;
			m_valueKnobDownY = position.y;
		});

		juceRmlUi::EventListener::Add(value, Rml::EventId::Mouseup, [this](const Rml::Event& _event)
		{
			if(juceRmlUi::helper::isContextMenu(_event))
				return;

			const auto position = juceRmlUi::helper::getMousePos(_event);
			const auto dx = position.x - m_valueKnobDownX;
			const auto dy = position.y - m_valueKnobDownY;

			if(dx * dx + dy * dy > kValuePushClickRadius * kValuePushClickRadius)
				return;

			constexpr auto valuePush = static_cast<uint32_t>(emu88Lib::Sc8850Button::ValuePush);

			const juce::WeakReference<jucePluginEditorLib::Editor> safeThis(this);

			const auto push = [safeThis](const bool _pressed)
			{
				auto* editor = dynamic_cast<Editor*>(safeThis.get());
				if(!editor || !editor->getRmlComponent())
					return;
				juceRmlUi::RmlInterfaces::ScopedAccess access(*editor->getRmlComponent());
				editor->setPointerButton(valuePush, _pressed);
				editor->getRmlComponent()->enqueueUpdate();
			};

			juce::Timer::callAfterDelay(1, [push] { push(true); });
			juce::Timer::callAfterDelay(1 + kValuePushPulseMs, [push] { push(false); });
		});

		// The endless knob wraps by (max - min), so value 32 is the same detent as value 0: 32
		// detents per turn, one knurl sprite per detent
		constexpr int encoderMinimum = 0;
		constexpr int encoderMaximum = 32;
		constexpr int encoderCentre = 16;

		value->SetAttribute("min", encoderMinimum);
		value->SetAttribute("max", encoderMaximum);
		value->SetAttribute("step", 1);
		value->SetAttribute("default", encoderCentre);

		juceRmlUi::ElemValue::setValue(value, static_cast<float>(encoderCentre), false);

		if(auto* knob = dynamic_cast<juceRmlUi::ElemKnob*>(value))
			knob->setEndless(true);

		juceRmlUi::EventListener::Add(value, Rml::EventId::Change, [this, value, last = encoderCentre](Rml::Event&) mutable
		{
			const auto current = static_cast<int>(std::lround(juceRmlUi::ElemValue::getValue(value)));

			auto delta = current - last;

			constexpr int encoderRange = encoderMaximum - encoderMinimum;

			if(delta > encoderRange / 2)
				delta -= encoderRange;
			else if(delta < -encoderRange / 2)
				delta += encoderRange;

			last = current;

			m_controller.turnPanelEncoder(delta);
		});
	}

	void Editor::wireVolumeKnob()
	{
		m_volumeKnob = getDocument()->GetElementById("outputVolume");
		if(!m_volumeKnob)
			return;

		// The plugin's output gain, which the DSP/Audio settings page shows in dB
		m_volumeKnob->SetAttribute("min", g_volumeMinimum);
		m_volumeKnob->SetAttribute("max", g_volumeMaximum);
		m_volumeKnob->SetAttribute("step", 1);
		m_volumeKnob->SetAttribute("default", g_volumeUnity);

		m_shownOutputGain = -1.0f;
		updateVolumeKnob();

		juceRmlUi::EventListener::Add(m_volumeKnob, Rml::EventId::Change, [this](Rml::Event&)
		{
			const auto value = std::clamp(juceRmlUi::ElemValue::getValue(m_volumeKnob),
				static_cast<float>(g_volumeMinimum), static_cast<float>(g_volumeMaximum));

			m_shownOutputGain = value / static_cast<float>(g_volumeUnity);
			m_processor.setOutputGain(m_shownOutputGain);
		});
	}

	void Editor::updateVolumeKnob()
	{
		if(!m_volumeKnob)
			return;

		// The settings page or a restored project may have changed it
		const auto gain = m_processor.getOutputGain();
		if(gain == m_shownOutputGain)
			return;

		m_shownOutputGain = gain;

		auto* rml = getRmlComponent();
		if(!rml)
			return;

		juceRmlUi::RmlInterfaces::ScopedAccess access(*rml);

		// false = no change event, so showing it never writes it back
		juceRmlUi::ElemValue::setValue(m_volumeKnob, std::clamp(gain * static_cast<float>(g_volumeUnity),
			static_cast<float>(g_volumeMinimum), static_cast<float>(g_volumeMaximum)), false);

		rml->enqueueUpdate();
	}

	void Editor::openDeviceMenu(const Rml::Event& _event)
	{
		juceRmlUi::Menu menu;

		// The user may just have copied the missing dump into the ROM folder
		const auto inventory = emu88Lib::RomLoader::rescan();
		const auto current = m_processor.getDeviceModel();

		for(const auto model : emu88Lib::g_deviceMenuOrder)
		{
			// A hidden device stays in the menu only while it is the one running
			if(!emu88Lib::isDeviceListed(model) && model != current)
				continue;

			const auto available = inventory.isComplete(emu88Lib::RomLoader::toRomDevice(model));

			// The GM modules are offered only once their ROMs are there
			if(emu88Lib::isGmModuleModel(model) && !available && model != current)
				continue;

			const auto label = std::string(emu88Lib::getDeviceProfile(model).displayName) + (available ? "" : " (ROMs missing)");

			// Picking one whose ROMs are missing tells the user which files it needs
			menu.addEntry(label, true, model == current, [this, model]
			{
				selectDeviceModel(model);
			}, available ? "" : "romMissing");
		}

		auto* target = _event.GetTargetElement();
		menu.openPopupWindow(target, target->GetAbsoluteOffset(Rml::BoxArea::Border), target->GetBox().GetSize(Rml::BoxArea::Border));
	}

	void Editor::selectDeviceModel(const emu88Lib::DeviceModel _model)
	{
		releaseButtons();

		// On failure the processor has told the user why and kept the running board. On success
		// evDeviceModelChanged brings the panel over to the new board
		m_processor.setDeviceModel(_model);
	}

	void Editor::onDeviceModelChanged(const emu88Lib::DeviceModel _model)
	{
		auto* rml = getRmlComponent();
		if(!rml)
			return;

		juceRmlUi::RmlInterfaces::ScopedAccess access(*rml);

		// The new board has no switch held, and what the old one showed is gone
		m_pointerButtons = 0;
		m_keyboardButtons = 0;
		m_sentButtons = 0;

		m_controller.clearPanel(_model);
		m_panelRevision = m_controller.getPanel().revision;
		m_shownBoardRunning = m_processor.isBoardRunning();

		updateDeviceSkin(_model);
		updatePowerVisuals();

		if(m_lcd)
			m_lcd->reset(_model);
		if(m_lcd2)
			m_lcd2->reset(_model, 1);

		updateButtonVisuals();
		updateLeds(0);

		rml->enqueueUpdate();
	}

	void Editor::updateDeviceSkin(const emu88Lib::DeviceModel _model)
	{
		auto* document = getDocument();

		const char* panel = "sc88pro_panel.png";

		switch(_model)
		{
		case emu88Lib::DeviceModel::Sc88: panel = "sc88_panel.png"; break;
		case emu88Lib::DeviceModel::Sc88VL: panel = "sc88vl_panel.png"; break;
		case emu88Lib::DeviceModel::Sc88Pro: panel = "sc88pro_panel.png"; break;
		case emu88Lib::DeviceModel::Sc8850: panel = "sc8850_panel.png"; break;
		case emu88Lib::DeviceModel::Sc55Mk2:
		case emu88Lib::DeviceModel::Sc155Mk2: panel = "sc55mk2_panel.png"; break;
		case emu88Lib::DeviceModel::Sc55Mk1:
		case emu88Lib::DeviceModel::Sc155: panel = "sc55_panel.png"; break;
		// Boards without a front panel: artwork with only the device selector and the volume knob
		case emu88Lib::DeviceModel::Cm32p: panel = "cm32p_panel.png"; break;
		case emu88Lib::DeviceModel::Cm32l:
		case emu88Lib::DeviceModel::Cm32ln:
		// The MT-32s borrow the CM-32L's bezel for now: the same display in the same window,
		// their switches and knob on the keyboard
		case emu88Lib::DeviceModel::Mt32Old:
		case emu88Lib::DeviceModel::Mt32New: panel = "cm32l_panel.png"; break;
		case emu88Lib::DeviceModel::Cm64: panel = "cm64_panel.png"; break;
		case emu88Lib::DeviceModel::Sc8820: panel = "sc8820_panel.png"; break;
		case emu88Lib::DeviceModel::Xpgs:
		case emu88Lib::DeviceModel::VeGsPro:
		case emu88Lib::DeviceModel::Nu10b:
		case emu88Lib::DeviceModel::Miig5: panel = "sc88exp_panel.png"; break;
		case emu88Lib::DeviceModel::Sc55St:
		case emu88Lib::DeviceModel::Cm300:
		case emu88Lib::DeviceModel::Scc1a:
		case emu88Lib::DeviceModel::Scb55:
		case emu88Lib::DeviceModel::Rlp3237: panel = "sc55pc_panel.png"; break;
		}

		if(auto* image = document->GetElementById("hardwarePanel"))
			image->SetAttribute("src", panel);

		// The boards whose artwork shows no switches: the skin's faces are hidden, and what the LA
		// boards read comes from the keyboard
		const bool noPanel = !panelArtworkHasSwitches(_model);
		const bool cmBezel = emu88Lib::isCmModel(_model) || emu88Lib::isLaModel(_model);

		// RML's <body> is the document itself, the model classes go there
		document->SetClass("modelSc88", _model == emu88Lib::DeviceModel::Sc88);
		document->SetClass("modelSc88VL", _model == emu88Lib::DeviceModel::Sc88VL);
		document->SetClass("modelSc88Pro", _model == emu88Lib::DeviceModel::Sc88Pro);
		document->SetClass("modelSc8850", _model == emu88Lib::DeviceModel::Sc8850);
		document->SetClass("modelSc55", emu88Lib::isSc55Model(_model) && !noPanel);
		document->SetClass("modelCm", cmBezel);
		// The CM bezels differ in their windows: one 16x2 on the CM-32P, one 20x1 on the CM-32L,
		// and both on the CM-64. The MT-32s wear the CM-32L's for now
		document->SetClass("modelCm32l", cmBezel && _model != emu88Lib::DeviceModel::Cm32p && _model != emu88Lib::DeviceModel::Cm64);
		document->SetClass("modelCm64", _model == emu88Lib::DeviceModel::Cm64);
		document->SetClass("modelSc8820", _model == emu88Lib::DeviceModel::Sc8820);
		document->SetClass("modelNoPanel", noPanel);
		document->SetClass("modelNoDisplay", !emu88Lib::deviceHasLcd(_model));

		if(auto* mapOrEq = document->GetElementById("btSc88Map"))
			mapOrEq->SetAttribute("title", _model == emu88Lib::DeviceModel::Sc88Pro ? "SC-88 MAP (2)" : "EQ (2)");

		refreshButtonElements(_model);

		// Drop the previous model's lit lamps before the element list moves on
		updateLeds(0);
		refreshLedElements(_model);
	}

	void Editor::updatePowerVisuals()
	{
		auto* document = getDocument();

		// There is no power switch here: the glass goes dark, with the skin saying why, only while
		// no board could be started
		document->SetClass("powerOff", !m_shownBoardRunning);
		document->SetClass("lcdUnpowered", false);
	}

	void Editor::updateFromController()
	{
		const auto& panel = m_controller.getPanel();
		const auto running = m_processor.isBoardRunning();

		if(panel.revision == m_panelRevision && running == m_shownBoardRunning)
			return;

		auto* rml = getRmlComponent();
		if(!rml)
			return;

		juceRmlUi::RmlInterfaces::ScopedAccess access(*rml);

		if(running != m_shownBoardRunning)
		{
			m_shownBoardRunning = running;
			updatePowerVisuals();
		}

		if(panel.revision != m_panelRevision)
		{
			m_panelRevision = panel.revision;

			if(m_lcd)
				m_lcd->setSnapshot(panel.screens[0]);
			if(m_lcd2)
				m_lcd2->setSnapshot(panel.screens[1]);

			// Standby: the board runs on, but its firmware has cut the display supply
			getDocument()->SetClass("lcdUnpowered", !panel.screens[0].powered);

			updateLeds(panel.leds);
		}

		rml->enqueueUpdate();
	}

	bool Editor::handleKey(const Rml::Event& _event, const bool _pressed)
	{
		// The settings page has fields to type into, and a key held with a modifier is a shortcut
		// of the host's. A release always goes through, the switch would stay down otherwise
		if(_pressed)
		{
			if(settingsOpened())
				return false;

			if(juceRmlUi::helper::getKeyModCtrl(_event) || juceRmlUi::helper::getKeyModCommand(_event) ||
				juceRmlUi::helper::getKeyModAlt(_event))
				return false;
		}

		const auto key = juceRmlUi::helper::getKeyIdentifier(_event);
		const auto model = m_processor.getDeviceModel();

		if(key == Rml::Input::KI_Q)
		{
			// The POWER switch where the firmware reads it: the board goes in and out of standby
			// itself. Elsewhere Q switches the supply in the player, which a plugin does not have
			if(emu88Lib::getPowerSwitch(model) != emu88Lib::PowerSwitch::Standby)
				return false;
			setKeyboardButton(g_powerSwitchButton, _pressed);
			return true;
		}

		if(emu88Lib::hasPanelKnob(model) && (key == g_mt32KnobUpKey || key == g_mt32KnobDownKey))
		{
			// The knob is a potentiometer: every press moves it a notch and it stays there
			if(_pressed)
				m_controller.turnPanelEncoder(key == g_mt32KnobUpKey ? 1 : -1);
			return true;
		}

		const auto find = [this, key, _pressed](const auto& _bindings)
		{
			for(const auto& binding : _bindings)
			{
				if(binding.key != key)
					continue;
				setKeyboardButton(static_cast<uint32_t>(binding.button), _pressed);
				return true;
			}
			return false;
		};

		if(model == emu88Lib::DeviceModel::Sc8850)
			return find(g_sc8850Buttons);
		if(usesLaBoardButtons(model))
			return find(g_mt32Buttons);
		return find(g_sc88Buttons);
	}

	void Editor::setPointerButton(const uint32_t _button, const bool _pressed)
	{
		const auto mask = uint32_t{1} << _button;
		if(panelButtonsForDevice(m_processor.getDeviceModel(), mask) == 0)
			return;
		if(_pressed)
			m_pointerButtons |= mask;
		else
			m_pointerButtons &= ~mask;
		sendButtons();
		updateButtonVisuals();
	}

	void Editor::setKeyboardButton(const uint32_t _button, const bool _pressed)
	{
		const auto mask = uint32_t{1} << _button;
		if(panelButtonsForDevice(m_processor.getDeviceModel(), mask) == 0)
			return;
		if(_pressed)
			m_keyboardButtons |= mask;
		else
			m_keyboardButtons &= ~mask;
		sendButtons();
		updateButtonVisuals();
	}

	void Editor::releaseButtons()
	{
		m_pointerButtons = 0;
		m_keyboardButtons = 0;
		sendButtons();

		if(auto* rml = getRmlComponent())
		{
			juceRmlUi::RmlInterfaces::ScopedAccess access(*rml);
			updateButtonVisuals();
			rml->enqueueUpdate();
		}
	}

	void Editor::sendButtons()
	{
		const auto buttons = m_pointerButtons | m_keyboardButtons;
		if(buttons == m_sentButtons)
			return;
		m_sentButtons = buttons;
		m_controller.sendPanelButtons(panelButtonsForDevice(m_processor.getDeviceModel(), buttons));
	}

	void Editor::refreshButtonElements(const emu88Lib::DeviceModel _model)
	{
		m_buttonElements.fill(nullptr);

		auto* document = getDocument();

		const auto add = [this, document](const ButtonBinding& _binding)
		{
			if(_binding.element && _binding.button < m_buttonElements.size())
				m_buttonElements[_binding.button] = document->GetElementById(_binding.element);
		};

		if(_model == emu88Lib::DeviceModel::Sc8850)
		{
			for(const auto& binding : g_sc8850Buttons)
				add(binding);
		}
		else if(usesLaBoardButtons(_model))
		{
			for(const auto& binding : g_mt32Buttons)
				add(binding);
		}
		else
		{
			for(const auto& binding : g_sc88Buttons)
				add(binding);
		}

		for(uint32_t button = 0; button < m_buttonElements.size(); ++button)
		{
			auto* element = m_buttonElements[button];
			if(!element)
				continue;
			if(panelButtonsForDevice(_model, uint32_t{1} << button) == 0)
				element->SetAttribute("disabled", "");
			else
				element->RemoveAttribute("disabled");
		}
	}

	void Editor::refreshLedElements(const emu88Lib::DeviceModel _model)
	{
		m_leds.fill(nullptr);

		auto* document = getDocument();

		const auto assign = [this, document](const std::initializer_list<const char*> _ids)
		{
			size_t index = 0;
			for(const auto* id : _ids)
				m_leds[index++] = document->GetElementById(id);
		};

		switch(_model)
		{
		case emu88Lib::DeviceModel::Sc8850:
			// emu88Lib::Sc8850::Led bit order
			assign({"bt8850Edit", "bt8850Drum", "bt8850Effects", "bt8850Shift", "bt8850Solo", "bt8850Mute"});
			break;
		case emu88Lib::DeviceModel::Sc55Mk2:
		case emu88Lib::DeviceModel::Sc55Mk1:
		case emu88Lib::DeviceModel::Sc55St:
		case emu88Lib::DeviceModel::Cm300:
		case emu88Lib::DeviceModel::Scc1a:
		case emu88Lib::DeviceModel::Scb55:
		case emu88Lib::DeviceModel::Rlp3237:
		case emu88Lib::DeviceModel::Sc155:
		case emu88Lib::DeviceModel::Sc155Mk2:
			// The only two lamps on an SC-55-family panel, in board bit order
			assign({"btAll", "btMute"});
			break;
		case emu88Lib::DeviceModel::Cm32p:
		case emu88Lib::DeviceModel::Cm32l:
		case emu88Lib::DeviceModel::Cm32ln:
		case emu88Lib::DeviceModel::Cm64:
		case emu88Lib::DeviceModel::Mt32Old:
		case emu88Lib::DeviceModel::Mt32New:
			// The bezel's only driven lamp, bit 0 of the board's leds(); POWER is painted on
			assign({"ledCmMidi"});
			break;
		default:
			// Gate-array LED port bit order, shared by SC-88, SC-88VL and SC-88Pro
			assign({"btAll", "btMute", "btSc55Map", "btSc88Map", "ledSelectBottom", "ledSelectMiddle",
				"ledSelectTop", "ledEfx"});
			break;
		}
	}

	void Editor::updateButtonVisuals()
	{
		const auto buttons = m_pointerButtons | m_keyboardButtons;

		for(uint32_t button = 0; button < m_buttonElements.size(); ++button)
		{
			if(m_buttonElements[button])
				m_buttonElements[button]->SetClass("pressed", (buttons & (uint32_t{1} << button)) != 0);
		}
	}

	void Editor::updateLeds(const uint16_t _leds)
	{
		for(size_t i = 0; i < m_leds.size(); ++i)
		{
			if(m_leds[i])
				m_leds[i]->SetClass("on", (_leds & (1u << i)) != 0);
		}

		if(auto* efx = m_leds[7])
		{
			const auto model = m_processor.getDeviceModel();
			const bool pro = model == emu88Lib::DeviceModel::Sc88Pro || model == emu88Lib::DeviceModel::VeGsPro;
			efx->SetClass("green", pro && (_leds & 0x80));
			efx->SetClass("red", pro && (_leds & 0x100));
			if(pro)
				efx->SetClass("on", (_leds & 0x180) != 0);
		}
	}
}
