#include "88lib/sysexRemoteControl.h"

#include <algorithm>
#include <iterator>

namespace emu88Lib
{
	namespace
	{
		// 0x7D is reserved by the MMA for non-commercial use; "88" keeps us clear of anyone else's.
		constexpr uint8_t g_header[] = {0xf0, 0x7d, 0x38, 0x38};
		constexpr size_t g_headerSize = std::size(g_header) + 1;	// + command
		constexpr size_t g_footerSize = 1;							// 0xf7

		// Screen: index, type, flags, then the payload of the type
		constexpr size_t g_screenHeaderSize = 3;
		constexpr uint8_t g_flagDisplayOn = 0x01;
		constexpr uint8_t g_flagPowered = 0x02;
		// A graphic screen's dots travel seven to a byte. The largest panel is the SC-8850's 160 x 64.
		constexpr size_t g_dotsPerByte = 7;
		constexpr uint32_t g_maxDots = 256 * 128;

		constexpr size_t g_characterPayloadSize = (std::tuple_size_v<decltype(DisplaySnapshot::Screen::ddRam)> +
			std::tuple_size_v<decltype(DisplaySnapshot::Screen::cgRam)>) * 2;

		void createHeader(synthLib::SysexBuffer& _dst, SysexRemoteControl::Command _command)
		{
			_dst.assign(std::begin(g_header), std::end(g_header));
			_dst.push_back(static_cast<uint8_t>(_command));
		}

		void writeNibbles(synthLib::SysexBuffer& _dst, const uint8_t _value)
		{
			_dst.push_back(_value >> 4);
			_dst.push_back(_value & 0xf);
		}

		uint8_t readNibbles(const synthLib::SysexBuffer& _src, size_t& _offset)
		{
			const auto hi = _src[_offset++];
			const auto lo = _src[_offset++];
			return static_cast<uint8_t>(((hi & 0xf) << 4) | (lo & 0xf));
		}

		void write14(synthLib::SysexBuffer& _dst, const uint32_t _value)
		{
			_dst.push_back(static_cast<uint8_t>((_value >> 7) & 0x7f));
			_dst.push_back(static_cast<uint8_t>(_value & 0x7f));
		}

		uint32_t read14(const synthLib::SysexBuffer& _src, size_t& _offset)
		{
			const uint32_t hi = _src[_offset++];
			const uint32_t lo = _src[_offset++];
			return (hi << 7) | lo;
		}

		size_t packedDotBytes(const size_t _dots)
		{
			return (_dots + g_dotsPerByte - 1) / g_dotsPerByte;
		}
	}

	void SysexRemoteControl::createSubscribe(synthLib::SysexBuffer& _dst)
	{
		createHeader(_dst, Command::Subscribe);
		_dst.push_back(0xf7);
	}

	void SysexRemoteControl::createButtons(synthLib::SysexBuffer& _dst, const uint32_t _buttons)
	{
		createHeader(_dst, Command::Buttons);
		_dst.push_back(static_cast<uint8_t>((_buttons >> 28) & 0x0f));
		_dst.push_back(static_cast<uint8_t>((_buttons >> 21) & 0x7f));
		_dst.push_back(static_cast<uint8_t>((_buttons >> 14) & 0x7f));
		_dst.push_back(static_cast<uint8_t>((_buttons >> 7) & 0x7f));
		_dst.push_back(static_cast<uint8_t>(_buttons & 0x7f));
		_dst.push_back(0xf7);
	}

	void SysexRemoteControl::createEncoder(synthLib::SysexBuffer& _dst, const int32_t _detents)
	{
		createHeader(_dst, Command::Encoder);
		write14(_dst, static_cast<uint32_t>(std::clamp(_detents, -8192, 8191) + 8192));
		_dst.push_back(0xf7);
	}

	void SysexRemoteControl::createScreen(synthLib::SysexBuffer& _dst, const uint8_t _index, const DisplaySnapshot::Screen& _screen)
	{
		createHeader(_dst, Command::Screen);

		auto type = _screen.type;

		// A grid we cannot describe goes out as no screen at all, which leaves the panel blank
		const auto dots = static_cast<uint32_t>(_screen.width) * _screen.height;
		if(type == DisplaySnapshot::Type::Graphic && (!dots || dots > g_maxDots || _screen.mono.size() != dots))
			type = DisplaySnapshot::Type::None;

		_dst.push_back(_index & 0x7f);
		_dst.push_back(static_cast<uint8_t>(type));
		_dst.push_back(static_cast<uint8_t>((_screen.displayOn ? g_flagDisplayOn : 0) | (_screen.powered ? g_flagPowered : 0)));

		switch(type)
		{
		case DisplaySnapshot::Type::Character:
			_dst.reserve(_dst.size() + g_characterPayloadSize + g_footerSize);
			for(const auto c : _screen.ddRam)
				writeNibbles(_dst, c);
			for(const auto c : _screen.cgRam)
				writeNibbles(_dst, c);
			break;
		case DisplaySnapshot::Type::Graphic:
			{
				write14(_dst, _screen.width);
				write14(_dst, _screen.height);

				_dst.reserve(_dst.size() + packedDotBytes(dots) + g_footerSize);

				for(size_t i = 0; i < dots; i += g_dotsPerByte)
				{
					uint8_t packed = 0;
					for(size_t b = 0; b < g_dotsPerByte && i + b < dots; ++b)
					{
						if(_screen.mono[i + b])
							packed |= static_cast<uint8_t>(1u << b);
					}
					_dst.push_back(packed);
				}
			}
			break;
		case DisplaySnapshot::Type::None:
			break;
		}

		_dst.push_back(0xf7);
	}

	void SysexRemoteControl::createLeds(synthLib::SysexBuffer& _dst, const uint16_t _leds)
	{
		createHeader(_dst, Command::Leds);
		_dst.push_back(static_cast<uint8_t>((_leds >> 14) & 0x03));
		_dst.push_back(static_cast<uint8_t>((_leds >> 7) & 0x7f));
		_dst.push_back(static_cast<uint8_t>(_leds & 0x7f));
		_dst.push_back(0xf7);
	}

	bool SysexRemoteControl::isSameScreen(const DisplaySnapshot::Screen& _a, const DisplaySnapshot::Screen& _b)
	{
		return _a.type == _b.type && _a.displayOn == _b.displayOn && _a.powered == _b.powered &&
			_a.width == _b.width && _a.height == _b.height &&
			_a.ddRam == _b.ddRam && _a.cgRam == _b.cgRam && _a.mono == _b.mono;
	}

	bool SysexRemoteControl::receive(const synthLib::SMidiEvent& _input)
	{
		if(_input.sysex.empty())
			return false;
		return receive(_input.sysex);
	}

	bool SysexRemoteControl::receive(const synthLib::SysexBuffer& _input)
	{
		if(_input.size() < g_headerSize + g_footerSize)
			return false;

		if(!std::equal(std::begin(g_header), std::end(g_header), _input.begin()))
			return false;

		if(_input.back() != 0xf7)
			return false;

		size_t i = g_headerSize;
		const auto payloadSize = _input.size() - g_headerSize - g_footerSize;

		switch(static_cast<Command>(_input[g_headerSize - 1]))
		{
		case Command::Subscribe:
			if(payloadSize != 0)
				return false;
			evSubscribe();
			return true;
		case Command::Buttons:
			{
				if(payloadSize != 5)
					return false;
				uint32_t buttons = _input[i++] & 0x0f;
				for(size_t b = 0; b < 4; ++b)
					buttons = (buttons << 7) | (_input[i++] & 0x7f);
				evButtons(buttons);
			}
			return true;
		case Command::Encoder:
			if(payloadSize != 2)
				return false;
			evEncoder(static_cast<int32_t>(read14(_input, i)) - 8192);
			return true;
		case Command::Screen:
			return receiveScreen(_input, i);
		case Command::Leds:
			{
				if(payloadSize != 3)
					return false;
				uint32_t leds = _input[i++] & 0x03;
				leds = (leds << 7) | (_input[i++] & 0x7f);
				leds = (leds << 7) | (_input[i++] & 0x7f);
				evLeds(static_cast<uint16_t>(leds));
			}
			return true;
		}
		return false;
	}

	bool SysexRemoteControl::receiveScreen(const synthLib::SysexBuffer& _input, size_t _offset)
	{
		const auto end = _input.size() - g_footerSize;

		if(end < _offset + g_screenHeaderSize)
			return false;

		const auto index = _input[_offset++];
		const auto type = _input[_offset++];
		const auto flags = _input[_offset++];

		if(index >= std::tuple_size_v<decltype(DisplaySnapshot::screens)>)
			return false;

		DisplaySnapshot::Screen screen;
		screen.type = static_cast<DisplaySnapshot::Type>(type);
		screen.displayOn = (flags & g_flagDisplayOn) != 0;
		screen.powered = (flags & g_flagPowered) != 0;

		switch(screen.type)
		{
		case DisplaySnapshot::Type::None:
			if(_offset != end)
				return false;
			break;
		case DisplaySnapshot::Type::Character:
			if(end - _offset != g_characterPayloadSize)
				return false;
			for(auto& c : screen.ddRam)
				c = readNibbles(_input, _offset);
			for(auto& c : screen.cgRam)
				c = readNibbles(_input, _offset);
			break;
		case DisplaySnapshot::Type::Graphic:
			{
				if(end - _offset < 4)
					return false;
				const auto width = read14(_input, _offset);
				const auto height = read14(_input, _offset);
				const auto dots = width * height;
				if(!dots || dots > g_maxDots || end - _offset != packedDotBytes(dots))
					return false;
				screen.width = static_cast<uint16_t>(width);
				screen.height = static_cast<uint16_t>(height);
				screen.mono.resize(dots);
				for(size_t d = 0; d < dots; ++d)
					screen.mono[d] = (_input[_offset + d / g_dotsPerByte] >> (d % g_dotsPerByte)) & 1;
			}
			break;
		default:
			return false;
		}

		evScreen(index, screen);
		return true;
	}
}
