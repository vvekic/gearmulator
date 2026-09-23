#pragma once

#include "88lib/deviceModel.h"

#include "jucePluginEditorLib/pluginProcessor.h"

#include "baseLib/event.h"

#include <string>

namespace emu88JucePlugin
{
	class AudioPluginAudioProcessor : public jucePluginEditorLib::Processor
	{
	public:
		// Fires after a board has been booted, whether it is a different one or the same one restarted
		baseLib::Event<emu88Lib::DeviceModel> evDeviceModelChanged;

		AudioPluginAudioProcessor();
		~AudioPluginAudioProcessor() override;

		jucePluginEditorLib::PluginEditorState* createEditorState() override;
		synthLib::Device* createDevice() override;
		void getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const override;

		pluginLib::Controller* createController() override;

		bool setLatencyBlocks(uint32_t _blocks) override;

		void saveChunkData(baseLib::BinaryStream& _s) override;
		void loadChunkData(baseLib::ChunkReader& _cr) override;

		emu88Lib::DeviceModel getDeviceModel() const { return m_deviceModel; }

		// False while no board could be booted yet, a silent dummy device runs in its place then
		bool isBoardRunning() const { return m_boardRunning; }

		// Boots _model in place of the running board, optionally making it the default for new
		// instances. Returns false, keeping the running board, if the ROM set of _model is incomplete
		bool setDeviceModel(emu88Lib::DeviceModel _model, bool _persistAsDefault = true);

		// Boots the selected board again, like switching the hardware off and on. Also brings up a
		// board that could not boot before, once its ROMs are in place
		bool restartDevice();

		// The folder the user pointed us at, searched with its subfolders on top of the ROM folder
		// in the plugin's data directory. Empty while none is configured
		const std::string& getRomSearchPath() const { return m_romSearchPath; }

		// Searches _path from now on instead of the folder configured before, remembers it for every
		// instance and boots the board again, so a set that has just turned up is the one playing.
		// Returns what the boot returned: a board whose ROMs are not in the new folder says so and
		// keeps running. An empty path leaves only the plugin's own ROM folder
		bool setRomSearchPath(const std::string& _path);

		// Whether _model is offered at all and its ROM set is complete
		static bool isModelAvailable(emu88Lib::DeviceModel _model);

	private:
		bool bootDevice(emu88Lib::DeviceModel _model, bool _persistAsDefault);
		emu88Lib::DeviceModel getDefaultDeviceModel();
		synthLib::DeviceCreateParams createDeviceParams() const;

		emu88Lib::DeviceModel m_deviceModel = emu88Lib::DeviceModel::Sc88Pro;
		bool m_boardRunning = false;
		std::string m_romSearchPath;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
	};
}
