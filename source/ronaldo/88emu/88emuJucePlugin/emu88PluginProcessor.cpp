#include "emu88PluginProcessor.h"

#include "emu88Controller.h"
#include "emu88PluginEditorState.h"

// ReSharper disable once CppUnusedIncludeDirective
#include "BinaryData.h"

#include "88lib/hardwareDevice.h"
#include "88lib/rom/romloader.h"

#include "baseLib/binarystream.h"
#include "baseLib/filesystem.h"

#include "jucePluginLib/processorPropertiesInit.h"

#include "synthLib/deviceException.h"
#include "synthLib/romLoader.h"

namespace emu88JucePlugin
{
	namespace
	{
		constexpr const char* const g_configKeyDeviceModel = "deviceModel";
		constexpr const char* const g_configKeyRomSearchPath = "romSearchPath";
	}

	AudioPluginAudioProcessor::AudioPluginAudioProcessor()
		: Processor(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo(), true),
			{}, pluginLib::initProcessorProperties())
	{
		// A ROM set is a dozen files, sorted into folders by board as often as not, so look below
		// our ROM folder too, as the player does below its data folder. Only our ROM scanner
		// descends, the other products keep scanning this folder flat.
		//
		// The player's data folder is deliberately not added: search paths are shared by every
		// product in the process, and JE8086 takes any 512k .bin it finds there for its firmware,
		// which an SC-88 control ROM is.
		synthLib::RomLoader::addSearchPath(getPublicRomFolder(), true);

		// Where the user keeps the dumps, if they are not in our own folder. Before the first scan
		// below, which is what decides which board comes up
		m_romSearchPath = baseLib::filesystem::validatePath(getConfig().getValue(g_configKeyRomSearchPath, "").toStdString());

		if(!m_romSearchPath.empty())
			synthLib::RomLoader::addSearchPath(m_romSearchPath, true);

		m_deviceModel = getDefaultDeviceModel();

		getController();

		// The boards are sound modules, not sequencers: the host clock the engine would otherwise
		// generate is just traffic on their MIDI in.
		getPlugin().setMidiClockEnabled(false);

		setLatencyBlocks(0);
	}

	AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
	{
		destroyEditorState();
	}

	jucePluginEditorLib::PluginEditorState* AudioPluginAudioProcessor::createEditorState()
	{
		return new PluginEditorState(*this);
	}

	synthLib::Device* AudioPluginAudioProcessor::createDevice()
	{
		auto device = std::make_unique<emu88Lib::HardwareDevice>(createDeviceParams());

		if(!device->isValid())
		{
			const std::string name = emu88Lib::getDeviceProfile(m_deviceModel).displayName;
			const auto romDevice = emu88Lib::RomLoader::toRomDevice(m_deviceModel);

			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,
				"The ROM set of the " + name + " is incomplete.\n\n" +
				emu88Lib::RomLoader::scan().describeRequirements(romDevice));
		}

		m_boardRunning = true;

		return device.release();
	}

	void AudioPluginAudioProcessor::getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const
	{
		Processor::getRemoteDeviceParams(_params);

		// The boards read their ROM sets from disk by themselves, so there is no romData to hand
		// over, and the bridge server refuses a device without it. Remote mode needs the device
		// library to take its ROMs through DeviceCreateParams first.
		const auto params = createDeviceParams();
		_params.customData = params.customData;
		_params.romName = params.romName;
	}

	pluginLib::Controller* AudioPluginAudioProcessor::createController()
	{
		return new Controller(*this);
	}

	bool AudioPluginAudioProcessor::setLatencyBlocks(uint32_t)
	{
		// The boards render synchronously inside process() and never read the extra latency the
		// framework hands to a device. Any latency we report to the host would therefore not be
		// applied, and the host would compensate for a delay that does not exist.
		return Processor::setLatencyBlocks(0);
	}

	void AudioPluginAudioProcessor::saveChunkData(baseLib::BinaryStream& _s)
	{
		// Written first, so the board is switched before the rest of the state is restored
		{
			baseLib::ChunkWriter cw(_s, "MODL", 1);
			_s.write(static_cast<uint8_t>(m_deviceModel));
		}

		Processor::saveChunkData(_s);
	}

	void AudioPluginAudioProcessor::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("MODL", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t)
		{
			const auto model = _binaryStream.read<uint8_t>();

			if(emu88Lib::isDeviceModelValue(model))
				setDeviceModel(static_cast<emu88Lib::DeviceModel>(model), false);
		});

		Processor::loadChunkData(_cr);
	}

	bool AudioPluginAudioProcessor::setDeviceModel(const emu88Lib::DeviceModel _model, const bool _persistAsDefault)
	{
		if(_model == m_deviceModel && m_boardRunning)
			return true;

		return bootDevice(_model, _persistAsDefault);
	}

	bool AudioPluginAudioProcessor::restartDevice()
	{
		return bootDevice(m_deviceModel, false);
	}

	bool AudioPluginAudioProcessor::setRomSearchPath(const std::string& _path)
	{
		// A pasted path often brings a trailing space or line break along
		const auto path = baseLib::filesystem::validatePath(juce::String::fromUTF8(_path.c_str()).trim().toStdString());

		if(path == m_romSearchPath)
			return true;

		// The search paths belong to the process, not to us, so the folder that was ours has to go
		// before the new one arrives - both would be searched for the rest of the session otherwise.
		// Our own ROM folder is not ours to drop, whatever the user pointed us at before
		if(!m_romSearchPath.empty() && m_romSearchPath != baseLib::filesystem::validatePath(getPublicRomFolder()))
			synthLib::RomLoader::removeSearchPath(m_romSearchPath);

		m_romSearchPath = path;

		if(!m_romSearchPath.empty())
			synthLib::RomLoader::addSearchPath(m_romSearchPath, true);

		getConfig().setValue(g_configKeyRomSearchPath, juce::String(m_romSearchPath));
		getConfig().saveIfNeeded();

		// Rescans the folder and boots the board from what is in it now. Reports its own failure and
		// keeps the running board in that case
		return restartDevice();
	}

	bool AudioPluginAudioProcessor::isModelAvailable(const emu88Lib::DeviceModel _model)
	{
		return emu88Lib::isDeviceListed(_model) && emu88Lib::RomLoader::isDeviceAvailable(_model);
	}

	bool AudioPluginAudioProcessor::bootDevice(const emu88Lib::DeviceModel _model, const bool _persistAsDefault)
	{
		if(!emu88Lib::isDeviceModelValue(static_cast<uint32_t>(_model)))
			return false;

		// Switching or restarting the board is exactly when the user has just dropped the
		// missing dump into the ROM folder, so look again
		(void)emu88Lib::RomLoader::rescan();

		const auto previousModel = m_deviceModel;
		m_deviceModel = _model;

		// Reports its own failure to the user and keeps the running device in that case
		if(!rebootDevice())
		{
			m_deviceModel = previousModel;
			return false;
		}

		if(_persistAsDefault)
		{
			getConfig().setValue(g_configKeyDeviceModel, static_cast<int>(_model));
			getConfig().saveIfNeeded();
		}

		evDeviceModelChanged(_model);

		return true;
	}

	emu88Lib::DeviceModel AudioPluginAudioProcessor::getDefaultDeviceModel()
	{
		// The board picked last if its ROM set is still complete, else the first one that is. The
		// configured choice stays in the config either way, so it comes back once its ROMs do
		const auto configured = getConfig().getIntValue(g_configKeyDeviceModel, -1);

		const auto configuredModel = configured >= 0 && emu88Lib::isDeviceModelValue(static_cast<uint32_t>(configured))
			? static_cast<emu88Lib::DeviceModel>(configured)
			: emu88Lib::DeviceModel::Sc88Pro;

		if(isModelAvailable(configuredModel))
			return configuredModel;

		for(const auto model : emu88Lib::g_deviceMenuOrder)
		{
			if(isModelAvailable(model))
				return model;
		}

		return configuredModel;
	}

	synthLib::DeviceCreateParams AudioPluginAudioProcessor::createDeviceParams() const
	{
		synthLib::DeviceCreateParams params;

		params.homePath = getDataFolder();
		params.customData = static_cast<uint32_t>(m_deviceModel);
		params.romName = emu88Lib::getDeviceProfile(m_deviceModel).displayName;

		return params;
	}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new emu88JucePlugin::AudioPluginAudioProcessor();
}
