#pragma once

// Interface shared by the hosted plug-in instances. Both the VST3 and the VST2
// host implement it, so the tracker code (instrument assignment, master effect
// chain, latency compensation) does not have to know which format a plug-in is.
#ifdef _WIN32

#include <stdint.h>
#include <atomic>
#include <string>
#include <vector>

#include "ft2_vst3.h" // VST3_PLUGIN_* kinds and vst3PluginInfo_t::format values

#ifndef MAX_WAV_RENDER_SAMPLES_PER_TICK
#define MAX_WAV_RENDER_SAMPLES_PER_TICK 4096
#endif

constexpr uint32_t PLUGIN_SCOPE_BUFFER_SIZE = 2048; // must be a power of two
constexpr int32_t MAX_PLUGIN_LATENCY_SAMPLES = 8192;

// transport state handed to a plug-in for a block
struct PluginHostTransport
{
	bool playing;
	double sampleRate;
	double tempo;
	int64_t projectTimeSamples;
	double projectTimeMusic;
};

// describes the plug-in to load, independent of its format
struct PluginLoadRequest
{
	const char *name;
	const char *vendor;
	const char *classID;    // VST3 class UID (hex) or VST2 unique ID (hex)
	const char *modulePath; // UTF-8
	int32_t kind;
	bool effectMode;
	float sampleRate;
};

class PluginHostInstance
{
public:
	virtual ~PluginHostInstance() {}

	virtual bool initialize(const PluginLoadRequest &request) = 0;
	virtual void setSampleRate(float sampleRate) = 0;
	virtual int32_t noteOn(uint8_t pitch, float velocity) = 0;
	virtual void noteOff(uint8_t pitch, int32_t noteId) = 0;
	virtual void allNotesOff() = 0;
	virtual bool process(uint32_t numSamples, float *scopeBuffer, std::atomic<uint32_t> &scopePosition,
	                     const PluginHostTransport &transport) = 0;
	virtual void processEffect(uint32_t numSamples, float *left, float *right, const PluginHostTransport &transport) = 0;
	virtual bool captureState(std::vector<uint8_t> &state) = 0;
	virtual bool applyState(const void *stateData, uint32_t stateSize) = 0;
	virtual bool openEditor() = 0;
	virtual void closeEditor() = 0;

	// reason the last initialize() failed, shown to the user
	const char *lastError() const { return lastErrorText.c_str(); }

	// the block the last process() rendered (one plane per channel)
	float *renderedLeft() { return outputL.data(); }
	float *renderedRight() { return outputR.data(); }

	// state the host reads back after a processed block
	int32_t latencySamples = 0;
	int32_t outputChannelCount = 0;
	bool isEffect = false;
	std::string pluginName;

protected:
	std::string lastErrorText;
	std::vector<float> outputL, outputR;
};

#endif
