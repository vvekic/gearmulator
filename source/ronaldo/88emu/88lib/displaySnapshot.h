#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace emu88Lib
{
	// What the front panel shows: the displays and the lamps. The device publishes it for an editor
	// in the same process, and SysexRemoteControl carries it to one that is not.
	struct DisplaySnapshot
	{
		enum class Type : uint8_t { None, Character, Graphic };

		// One display panel. A Character screen hands over the controller's memory for the
		// SC-88 panel renderer; a Graphic one is already a dot grid, width x height, row major.
		struct Screen
		{
			Type type = Type::None;
			std::array<uint8_t, 80> ddRam{};
			std::array<uint8_t, 64> cgRam{};
			std::vector<uint8_t> mono;
			// The visible characters of a character display, one string per line, in the
			// controller's character set (ASCII for the printable range). Empty on a
			// graphic display.
			std::vector<std::string> text;
			uint16_t width = 0;
			uint16_t height = 0;
			bool displayOn = false;
			// Whether the panel has its supply. The boards with a standby switch cut it there, which
			// leaves the glass dark, where a display that is merely off still shows its backlight.
			// displayOn is false as well then.
			bool powered = true;
		};

		// Only a board with two panels fills the second - see deviceHasSecondLcd(), which is
		// the CM-64 and its two service displays.
		std::array<Screen, 2> screens;
		uint16_t leds = 0;
		uint64_t revision = 0;
	};
}
