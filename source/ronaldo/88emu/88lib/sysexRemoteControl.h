#pragma once

#include "88lib/displaySnapshot.h"

#include "baseLib/event.h"

#include "synthLib/midiTypes.h"
#include "synthLib/sysexRemoteControl.h"

#include <cstdint>
#include <vector>

namespace emu88Lib
{
	// The front panel over MIDI, for an editor that must not call into the device: the plugin's,
	// whose device may run behind the DSP bridge or be replaced at any time. Custom sysex under
	// the non-commercial id, F0 7D 38 38 <command> <payload> F7, all payload bytes 7 bit.
	//
	// The device only pushes its panel while an editor holds a subscription, so a host that sends
	// everything the device emits to a MIDI port, as the standalone player does, never sees these.
	class SysexRemoteControl : public synthLib::SysexRemoteControl
	{
	public:
		enum class Command : uint8_t
		{
			Subscribe	= 0x01,		// editor => device: keep pushing the panel for g_subscriptionSeconds more
			Buttons		= 0x02,		// editor => device: the switches held, bit n for switch n
			Encoder		= 0x03,		// editor => device: detents to turn the encoder or knob by
			Screen		= 0x10,		// device => editor: one display panel
			Leds		= 0x11		// device => editor: the lamps, bit n for lamp n
		};

		// How long a subscription lasts. An editor renews it well before, and one that has gone
		// away stops the pushing this long after.
		static constexpr float g_subscriptionSeconds = 3.0f;

		baseLib::Event<> evSubscribe;
		baseLib::Event<uint32_t> evButtons;
		baseLib::Event<int32_t> evEncoder;
		baseLib::Event<uint8_t, DisplaySnapshot::Screen> evScreen;	// screen index, screen
		baseLib::Event<uint16_t> evLeds;

		static void createSubscribe(synthLib::SysexBuffer& _dst);
		static void createButtons(synthLib::SysexBuffer& _dst, uint32_t _buttons);
		static void createEncoder(synthLib::SysexBuffer& _dst, int32_t _detents);
		static void createScreen(synthLib::SysexBuffer& _dst, uint8_t _index, const DisplaySnapshot::Screen& _screen);
		static void createLeds(synthLib::SysexBuffer& _dst, uint16_t _leds);

		// Whether two screens look the same. The text is derived from the controller's memory and
		// not sent, so it does not take part.
		static bool isSameScreen(const DisplaySnapshot::Screen& _a, const DisplaySnapshot::Screen& _b);

		bool receive(std::vector<synthLib::SMidiEvent>&, const synthLib::SysexBuffer& _input) override { return receive(_input); }
		bool receive(const synthLib::SMidiEvent& _input) override;
		bool receive(const synthLib::SysexBuffer& _input) override;

	private:
		bool receiveScreen(const synthLib::SysexBuffer& _input, size_t _offset);
	};
}
