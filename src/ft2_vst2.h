#pragma once

// VST2 plug-in hosting. The VST2 SDK is not redistributable, so this header
// declares the parts of the VST 2.4 ABI (aeffect.h) a host needs directly. The
// struct layouts and opcode values follow the published plug-in ABI.
#ifdef _WIN32

#include <windows.h>

#include <stdint.h>
#include <array>
#include <string>
#include <vector>

#include "ft2_plugin_instance.h"

namespace ft2vst2
{
	typedef int32_t VstInt32;
	typedef int16_t VstInt16;
	typedef intptr_t VstIntPtr;

#define FT2_VSTCALLBACK __cdecl

	enum { kVstVersion = 2400 };
	enum { kEffectMagic = (int32_t)0x56737450 }; // 'VstP'

	// host to plug-in
	enum AEffectOpcodes
	{
		effOpen = 0,
		effClose,
		effSetProgram,
		effGetProgram,
		effSetProgramName,
		effGetProgramName,
		effGetParamLabel,
		effGetParamDisplay,
		effGetParamName,
		effGetVu,
		effSetSampleRate,
		effSetBlockSize,
		effMainsChanged,
		effEditGetRect,
		effEditOpen,
		effEditClose,
		effEditDraw,
		effEditMouse,
		effEditKey,
		effEditIdle,
		effEditTop,
		effEditSleep,
		effIdentify,
		effGetChunk,
		effSetChunk,
		effProcessEvents,
		effCanBeAutomated,
		effString2Parameter,
		effGetNumProgramCategories,
		effGetProgramNameIndexed,
		effCopyProgram,
		effConnectInput,
		effConnectOutput,
		effGetInputProperties,
		effGetOutputProperties,
		effGetPlugCategory,
		effGetCurrentPosition,
		effGetDestinationBuffer,
		effOfflineNotify,
		effOfflinePrepare,
		effOfflineRun,
		effProcessVarIo,
		effSetSpeakerArrangement,
		effSetBlockSizeAndSampleRate,
		effSetBypass,
		effGetEffectName,
		effGetErrorText,
		effGetVendorString,
		effGetProductString,
		effGetVendorVersion,
		effVendorSpecific,
		effCanDo,
		effGetTailSize,
		effIdle,
		effGetIcon,
		effSetViewPosition,
		effGetParameterProperties,
		effKeysRequired,
		effGetVstVersion,
		effEditKeyDown,
		effEditKeyUp,
		effSetEditKnobMode,
		effGetMidiProgramName,
		effGetCurrentMidiProgram,
		effGetMidiProgramCategory,
		effHasMidiProgramsChanged,
		effGetMidiKeyName,
		effBeginSetProgram,
		effEndSetProgram,
		effGetSpeakerArrangement,
		effShellGetNextPlugin,
		effStartProcess,
		effStopProcess,
		effSetTotalSampleToProcess,
		effSetPanLaw,
		effBeginLoadBank,
		effBeginLoadProgram,
		effSetProcessPrecision,
		effGetNumMidiInputChannels,
		effGetNumMidiOutputChannels
	};

	// plug-in to host
	enum AudioMasterOpcodes
	{
		audioMasterAutomate = 0,
		audioMasterVersion,
		audioMasterCurrentId,
		audioMasterIdle,
		audioMasterPinConnected,
		audioMasterWantMidi = 6,
		audioMasterGetTime,
		audioMasterProcessEvents,
		audioMasterSetTime,
		audioMasterTempoAt,
		audioMasterGetNumAutomatableParameters,
		audioMasterGetParameterQuantization,
		audioMasterIOChanged,
		audioMasterNeedIdle,
		audioMasterSizeWindow,
		audioMasterGetSampleRate,
		audioMasterGetBlockSize,
		audioMasterGetInputLatency,
		audioMasterGetOutputLatency,
		audioMasterGetPreviousPlug,
		audioMasterGetNextPlug,
		audioMasterWillReplaceOrAccumulate,
		audioMasterGetCurrentProcessLevel,
		audioMasterGetAutomationState,
		audioMasterOfflineStart,
		audioMasterOfflineRead,
		audioMasterOfflineWrite,
		audioMasterOfflineGetCurrentPass,
		audioMasterOfflineGetCurrentMetaPass,
		audioMasterSetOutputSampleRate,
		audioMasterGetOutputSpeakerArrangement,
		audioMasterGetVendorString,
		audioMasterGetProductString,
		audioMasterGetVendorVersion,
		audioMasterVendorSpecific,
		audioMasterSetIcon,
		audioMasterCanDo,
		audioMasterGetLanguage,
		audioMasterOpenWindow,
		audioMasterCloseWindow,
		audioMasterGetDirectory,
		audioMasterUpdateDisplay,
		audioMasterBeginEdit,
		audioMasterEndEdit,
		audioMasterOpenFileSelector,
		audioMasterCloseFileSelector,
		audioMasterEditFile,
		audioMasterGetChunkFile,
		audioMasterGetInputSpeakerArrangement
	};

	enum AEffectFlags
	{
		effFlagsHasEditor = 1 << 0,
		effFlagsCanReplacing = 1 << 4,
		effFlagsProgramChunks = 1 << 5,
		effFlagsIsSynth = 1 << 8,
		effFlagsCanDoubleReplacing = 1 << 12
	};

	enum VstEventTypes
	{
		kVstMidiType = 1
	};

	enum VstPlugCategory
	{
		kPlugCategUnknown = 0,
		kPlugCategEffect,
		kPlugCategSynth,
		kPlugCategAnalysis,
		kPlugCategMastering,
		kPlugCategSpacializer,
		kPlugCategRoomFx,
		kPlugSurroundFx,
		kPlugCategRestoration,
		kPlugCategOfflineProcess,
		kPlugCategShell,
		kPlugCategGenerator
	};

	enum VstStringConstants
	{
		kVstMaxProgNameLen = 24,
		kVstMaxParamStrLen = 8,
		kVstMaxVendorStrLen = 64,
		kVstMaxProductStrLen = 64,
		kVstMaxEffectNameLen = 32
	};

	enum VstTimeInfoFlags
	{
		kVstTransportChanged = 1,
		kVstTransportPlaying = 1 << 1,
		kVstTransportCycleActive = 1 << 2,
		kVstTransportRecording = 1 << 3,
		kVstAutomationWriting = 1 << 6,
		kVstAutomationReading = 1 << 7,
		kVstNanosValid = 1 << 8,
		kVstPpqPosValid = 1 << 9,
		kVstTempoValid = 1 << 10,
		kVstBarsValid = 1 << 11,
		kVstCyclePosValid = 1 << 12,
		kVstTimeSigValid = 1 << 13,
		kVstSmpteValid = 1 << 14,
		kVstClockValid = 1 << 15
	};

	enum VstProcessPrecision
	{
		kVstProcessPrecision32 = 0,
		kVstProcessPrecision64 = 1
	};

	enum VstProcessLevels
	{
		kVstProcessLevelUnknown = 0,
		kVstProcessLevelUser,
		kVstProcessLevelRealtime,
		kVstProcessLevelPrefetch,
		kVstProcessLevelOffline
	};

	enum VstAutomationStates
	{
		kVstAutomationUnsupported = 0,
		kVstAutomationOff,
		kVstAutomationRead,
		kVstAutomationWrite,
		kVstAutomationReadWrite
	};

	enum VstLanguages
	{
		kVstLangEnglish = 1,
		kVstLangGerman,
		kVstLangFrench,
		kVstLangItalian,
		kVstLangSpanish,
		kVstLangJapanese
	};

	struct AEffect;

#pragma pack(push, 8)
	struct ERect
	{
		VstInt16 top, left, bottom, right;
	};

	struct VstEvent
	{
		VstInt32 type;
		VstInt32 byteSize;
		VstInt32 deltaFrames;
		VstInt32 flags;
	};

	struct VstMidiEvent
	{
		VstInt32 type;
		VstInt32 byteSize;
		VstInt32 deltaFrames;
		VstInt32 flags;
		VstInt32 noteLength;
		VstInt32 noteOffset;
		VstInt32 midiData;
		int8_t detune;
		int8_t noteOffVelocity;
		char reserved1, reserved2;
	};

	struct VstEvents
	{
		VstInt32 numEvents;
		VstIntPtr reserved;
		VstEvent *events[256];
	};

	struct VstTimeInfo
	{
		double samplePos;
		double sampleRate;
		double nanoSeconds;
		double ppqPos;
		double tempo;
		double barStartPos;
		double cycleStartPos;
		double cycleEndPos;
		VstInt32 timeSigNumerator;
		VstInt32 timeSigDenominator;
		VstInt32 smpteOffset;
		VstInt32 smpteFrameRate;
		VstInt32 samplesToNextClock;
		VstInt32 flags;
	};

	struct AEffect
	{
		VstInt32 magic;
		VstIntPtr(FT2_VSTCALLBACK *dispatcher)(AEffect *, VstInt32, VstInt32, VstIntPtr, void *, float);
		void (FT2_VSTCALLBACK *process)(AEffect *, float **, float **, VstInt32);
		void (FT2_VSTCALLBACK *setParameter)(AEffect *, VstInt32, float);
		float (FT2_VSTCALLBACK *getParameter)(AEffect *, VstInt32);
		VstInt32 numPrograms;
		VstInt32 numParams;
		VstInt32 numInputs;
		VstInt32 numOutputs;
		VstInt32 flags;
		VstIntPtr resvd1;
		VstIntPtr resvd2;
		VstInt32 initialDelay;
		VstInt32 realQualities;
		VstInt32 offQualities;
		float ioRatio;
		void *object;
		void *user;
		VstInt32 uniqueID;
		VstInt32 version;
		void (FT2_VSTCALLBACK *processReplacing)(AEffect *, float **, float **, VstInt32);
		void (FT2_VSTCALLBACK *processDoubleReplacing)(AEffect *, double **, double **, VstInt32);
		char future[56];
	};
#pragma pack(pop)

	typedef VstIntPtr(FT2_VSTCALLBACK *AudioMasterProc)(AEffect *, VstInt32, VstInt32, VstIntPtr, void *, float);
	typedef AEffect *(FT2_VSTCALLBACK *PluginMainProc)(AudioMasterProc);
}

// one plug-in exposed by a VST2 module (a shell DLL can contain several)
struct Vst2PluginEntry
{
	char name[128];
	char vendor[128];
	char version[64];
	char uniqueId[64]; // hex plug-in id, identifies the plug-in inside a shell DLL
	int32_t identifier;
	bool isInstrument;
	bool isShell;
};

// Reads the plug-ins a VST2 module exposes. The module is opened and closed
// again, nothing is left loaded.
bool vst2EnumerateModule(const char *modulePathUtf8, std::vector<Vst2PluginEntry> &entries, std::string &error);

class Vst2Instance final : public PluginHostInstance
{
public:
	~Vst2Instance() override;

	bool initialize(const PluginLoadRequest &request) override;
	void setSampleRate(float sampleRate) override;
	int32_t noteOn(uint8_t pitch, float velocity) override;
	void noteOff(uint8_t pitch, int32_t noteId) override;
	void allNotesOff() override;
	bool process(uint32_t numSamples, float *scopeBuffer, std::atomic<uint32_t> &scopePosition,
	             const PluginHostTransport &transport) override;
	void processEffect(uint32_t numSamples, float *left, float *right, const PluginHostTransport &transport) override;
	bool captureState(std::vector<uint8_t> &state) override;
	bool applyState(const void *stateData, uint32_t stateSize) override;
	bool openEditor() override;
	void closeEditor() override;

	// called by the plug-in through the host callback
	intptr_t hostCallback(int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt);
	void idleEditor();

	// read by the editor window procedure
	HWND editorWindow = nullptr;

private:
	void addMidiEvent(uint8_t status, uint8_t data1, uint8_t data2);
	bool sendEvents();
	void updateTimeInfo(const PluginHostTransport &transport);
	void updateLatency();
	void updateIoChannels();
	bool openEditorWindow(int32_t width, int32_t height);

	HMODULE module = nullptr;
	ft2vst2::AEffect *effect = nullptr;
	bool processing = false;
	bool resumed = false;
	bool editorOpen = false;
	bool hasEditor = false;
	bool canReplacing = false;
	int32_t numInputs = 0, numOutputs = 0;
	int32_t blockSize = MAX_WAV_RENDER_SAMPLES_PER_TICK;
	float sampleRate = 44100.0f;
	int32_t noteIdCounter = 1;
	std::string moduleDirectory;

	std::array<float *, 2> inputChannels{};
	std::array<float *, 2> outputChannels{};
	std::vector<float> inputL, inputR;

	std::array<ft2vst2::VstMidiEvent, 256> midiEvents{};
	std::array<ft2vst2::VstEvent *, 256> eventPointers{};
	int32_t eventCount = 0;

	ft2vst2::VstTimeInfo timeInfo{};

	struct HeldNote { int32_t id; int16_t pitch; };
	std::array<HeldNote, 256> heldNotes{};
};

#endif
