// A CLAP instrument for one 88emu device, without an editor.
//
// The device is fixed when this file is compiled (EMU88_CLAP_MODEL, one .clap file per device, see
// CMakeLists.txt), and its ROMs are found in the folder the EMU88_ROM_PATH environment variable names.
// Everything goes through the C interface of 88lib, which finds the ROMs, boots the device and
// resamples to the host's rate.

#include "88lib/c_interface.h"
#include "88lib/deviceModel.h"

#include <clap/clap.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace
{
	constexpr auto g_device = static_cast<emu88_device_id>(emu88Lib::DeviceModel::EMU88_CLAP_MODEL);
	constexpr const char* g_romPathVariable = "EMU88_ROM_PATH";

	const char* const g_features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};

	// Filled in by clap_entry.init, the name is the device's own display name
	std::string g_name;
	clap_plugin_descriptor_t g_descriptor{};

	// The ROM search paths are shared by the whole process, so the first instance sets them and
	// every later one gets the same answer. Empty when the folder was taken.
	const std::string& romPathError()
	{
		static std::once_flag once;
		static std::string error;

		std::call_once(once, []
		{
			const char* path = std::getenv(g_romPathVariable);

			if(!path || !*path)
				error = std::string(g_romPathVariable) + " is not set. It has to name the folder the ROMs are in.";
			else if(emu88_set_rom_path(path) != EMU88_RC_OK)
				error = std::string(g_romPathVariable) + " names a folder that does not exist: " + path;
		});

		return error;
	}

	std::string describeRoms()
	{
		std::vector<char> text(emu88_describe_device_roms(g_device, nullptr, 0) + 1);
		emu88_describe_device_roms(g_device, text.data(), text.size());
		return text.data();
	}

	class Plugin
	{
	public:
		explicit Plugin(const clap_host_t* _host) : m_host(_host)
		{
			m_plugin.desc = &g_descriptor;
			m_plugin.plugin_data = this;
			m_plugin.init = [](const clap_plugin_t* _p) { return self(_p)->init(); };
			m_plugin.destroy = [](const clap_plugin_t* _p) { delete self(_p); };
			m_plugin.activate = [](const clap_plugin_t* _p, const double _rate, const uint32_t, const uint32_t _maxFrames) { return self(_p)->activate(_rate, _maxFrames); };
			m_plugin.deactivate = [](const clap_plugin_t*) {};
			m_plugin.start_processing = [](const clap_plugin_t*) { return true; };
			m_plugin.stop_processing = [](const clap_plugin_t*) {};
			m_plugin.reset = [](const clap_plugin_t* _p) { emu88_play_silence(self(_p)->m_context); };
			m_plugin.process = [](const clap_plugin_t* _p, const clap_process_t* _process) { return self(_p)->process(_process); };
			m_plugin.get_extension = [](const clap_plugin_t*, const char* _id) { return getExtension(_id); };
			m_plugin.on_main_thread = [](const clap_plugin_t*) {};
		}

		~Plugin()
		{
			emu88_free_context(m_context);
		}

		Plugin(const Plugin&) = delete;
		Plugin& operator=(const Plugin&) = delete;

		const clap_plugin_t* clapPlugin() const { return &m_plugin; }

	private:
		static Plugin* self(const clap_plugin_t* _plugin)
		{
			return static_cast<Plugin*>(_plugin->plugin_data);
		}

		bool init()
		{
			m_log = static_cast<const clap_host_log_t*>(m_host->get_extension(m_host, CLAP_EXT_LOG));

			if(const auto& error = romPathError(); !error.empty())
			{
				log(CLAP_LOG_ERROR, g_name + ": " + error);
				return false;
			}

			if(!emu88_is_device_available(g_device))
			{
				log(CLAP_LOG_ERROR, g_name + ": the ROMs are not in " + std::getenv(g_romPathVariable) + "\n" + describeRoms());
				return false;
			}

			m_context = emu88_create_context();
			return emu88_select_device(m_context, g_device) == EMU88_RC_OK;
		}

		bool activate(const double _sampleRate, const uint32_t _maxFrames)
		{
			emu88_set_stereo_output_samplerate(m_context, _sampleRate);
			m_buffer.resize(static_cast<size_t>(_maxFrames) * 2);

			// The firmware boots on the first activation only and keeps running through later ones, so
			// a change of the host's rate does not cost a reboot. Booting blocks for a moment.
			if(emu88_is_open(m_context))
				return true;

			if(const auto rc = emu88_open_synth(m_context); rc != EMU88_RC_OK)
			{
				log(CLAP_LOG_ERROR, g_name + ": the device did not start, error " + std::to_string(rc));
				return false;
			}
			return true;
		}

		clap_process_status process(const clap_process_t* _process)
		{
			const auto frames = _process->frames_count;
			const auto* events = _process->in_events;
			const auto eventCount = events->size(events);

			if(_process->audio_outputs_count)
				_process->audio_outputs[0].constant_mask = 0;

			// MIDI reaches the device with the next frame rendered, so the block is cut at every event to
			// keep it sample accurate
			uint32_t eventIndex = 0;
			uint32_t pos = 0;

			while(pos < frames)
			{
				uint32_t next = frames;

				for(; eventIndex < eventCount; ++eventIndex)
				{
					const auto* header = events->get(events, eventIndex);
					if(header->time > pos)
					{
						next = std::min(header->time, frames);
						break;
					}
					handleEvent(header);
				}

				render(_process, pos, next - pos);
				pos = next;
			}

			// An empty block still delivers its events
			for(; eventIndex < eventCount; ++eventIndex)
				handleEvent(events->get(events, eventIndex));

			return CLAP_PROCESS_CONTINUE;
		}

		void handleEvent(const clap_event_header_t* _header) const
		{
			if(_header->space_id != CLAP_CORE_EVENT_SPACE_ID)
				return;

			switch(_header->type)
			{
			case CLAP_EVENT_MIDI:
				{
					const auto* ev = reinterpret_cast<const clap_event_midi_t*>(_header);
					const auto msg = static_cast<uint32_t>(ev->data[0]) | static_cast<uint32_t>(ev->data[1]) << 8 | static_cast<uint32_t>(ev->data[2]) << 16;
					emu88_play_msg_on_port(m_context, ev->port_index, msg);
				}
				break;
			case CLAP_EVENT_MIDI_SYSEX:
				{
					const auto* ev = reinterpret_cast<const clap_event_midi_sysex_t*>(_header);
					emu88_play_sysex_on_port(m_context, ev->port_index, ev->buffer, ev->size);
				}
				break;
			default:
				break;
			}
		}

		void render(const clap_process_t* _process, const uint32_t _offset, const uint32_t _frames)
		{
			// A host may not hand us more than it announced in activate(), but rendering in pieces costs nothing
			const auto maxFrames = static_cast<uint32_t>(m_buffer.size() / 2);

			for(uint32_t done = 0; done < _frames && maxFrames;)
			{
				const auto count = std::min(_frames - done, maxFrames);
				emu88_render_float(m_context, m_buffer.data(), count);

				if(_process->audio_outputs_count && _process->audio_outputs[0].channel_count >= 2 && _process->audio_outputs[0].data32)
				{
					float* left = _process->audio_outputs[0].data32[0] + _offset + done;
					float* right = _process->audio_outputs[0].data32[1] + _offset + done;

					for(uint32_t i = 0; i < count; ++i)
					{
						left[i] = m_buffer[i * 2];
						right[i] = m_buffer[i * 2 + 1];
					}
				}

				done += count;
			}
		}

		void log(const clap_log_severity _severity, const std::string& _message) const
		{
			if(m_log)
				m_log->log(m_host, _severity, _message.c_str());
			else
				std::fprintf(stderr, "%s\n", _message.c_str());
		}

		static const void* getExtension(const char* _id);

		clap_plugin_t m_plugin{};
		const clap_host_t* const m_host;
		const clap_host_log_t* m_log = nullptr;
		emu88_context m_context = nullptr;
		std::vector<float> m_buffer;	// interleaved stereo, as the C interface renders it
	};

	// ---- extensions

	const clap_plugin_audio_ports_t g_audioPorts =
	{
		[](const clap_plugin_t*, const bool _isInput) -> uint32_t
		{
			return _isInput ? 0 : 1;
		},
		[](const clap_plugin_t*, const uint32_t _index, const bool _isInput, clap_audio_port_info_t* _info)
		{
			if(_isInput || _index)
				return false;

			_info->id = 0;
			std::snprintf(_info->name, sizeof(_info->name), "Main");
			_info->flags = CLAP_AUDIO_PORT_IS_MAIN;
			_info->channel_count = 2;
			_info->port_type = CLAP_PORT_STEREO;
			_info->in_place_pair = CLAP_INVALID_ID;
			return true;
		}
	};

	// One input per MIDI input of the device: the SC-88 family tells two apart, the SC-8850 four.
	// MIDI only, the device has no use for anything a CLAP note event says beyond it.
	const clap_plugin_note_ports_t g_notePorts =
	{
		[](const clap_plugin_t*, const bool _isInput) -> uint32_t
		{
			return _isInput ? static_cast<uint32_t>(std::max(1, emu88_get_device_midi_port_count(g_device))) : 0;
		},
		[](const clap_plugin_t*, const uint32_t _index, const bool _isInput, clap_note_port_info_t* _info)
		{
			if(!_isInput || _index >= static_cast<uint32_t>(std::max(1, emu88_get_device_midi_port_count(g_device))))
				return false;

			_info->id = _index;
			_info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
			_info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
			std::snprintf(_info->name, sizeof(_info->name), "MIDI In %c", static_cast<char>('A' + _index));
			return true;
		}
	};

	const void* Plugin::getExtension(const char* _id)
	{
		if(!std::strcmp(_id, CLAP_EXT_AUDIO_PORTS))
			return &g_audioPorts;
		if(!std::strcmp(_id, CLAP_EXT_NOTE_PORTS))
			return &g_notePorts;
		return nullptr;
	}

	// ---- factory

	const clap_plugin_factory_t g_factory =
	{
		[](const clap_plugin_factory_t*) -> uint32_t
		{
			return 1;
		},
		[](const clap_plugin_factory_t*, const uint32_t _index) -> const clap_plugin_descriptor_t*
		{
			return _index == 0 ? &g_descriptor : nullptr;
		},
		[](const clap_plugin_factory_t*, const clap_host_t* _host, const char* _pluginId) -> const clap_plugin_t*
		{
			if(!clap_version_is_compatible(_host->clap_version) || std::strcmp(_pluginId, g_descriptor.id) != 0)
				return nullptr;
			return (new Plugin(_host))->clapPlugin();
		}
	};

	bool entryInit(const char*)
	{
		g_name = std::string("Emu88 ") + emu88_get_device_name(g_device);

		g_descriptor.clap_version = CLAP_VERSION;
		g_descriptor.id = EMU88_CLAP_ID;
		g_descriptor.name = g_name.c_str();
		g_descriptor.vendor = "The Usual Suspects";
		g_descriptor.url = "https://dsp56300.wordpress.com";
		g_descriptor.manual_url = "https://dsp56300.wordpress.com";
		g_descriptor.support_url = "https://dsp56300.wordpress.com";
		g_descriptor.version = EMU88_CLAP_VERSION;
		g_descriptor.description = "Without an editor. Finds its ROMs in the folder EMU88_ROM_PATH names.";
		g_descriptor.features = g_features;
		return true;
	}
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry =
{
	CLAP_VERSION_INIT,
	entryInit,
	[] {},
	[](const char* _factoryId) -> const void*
	{
		return !std::strcmp(_factoryId, CLAP_PLUGIN_FACTORY_ID) ? &g_factory : nullptr;
	}
};
