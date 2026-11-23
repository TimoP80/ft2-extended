#include "ft2_vst2.h"

#ifdef _WIN32

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

extern "C"
{
#include "ft2_video.h"
}

namespace
{
	using namespace ft2vst2;

	const wchar_t *kEditorWindowClass = L"FT2CloneVST2EditorWindow";
	constexpr UINT_PTR kEditorIdleTimerId = 1;
	constexpr UINT kEditorIdleIntervalMs = 40;
	constexpr uint32_t MAX_STATE_BYTES = 64u * 1024u * 1024u;

	// the plug-in asked for while creating a shell DLL instance
	int32_t requestedShellId = 0;

	void copyText(char *dst, size_t dstSize, const char *src)
	{
		if (dstSize == 0)
			return;

		if (src == nullptr)
			src = "";

		std::snprintf(dst, dstSize, "%s", src);
	}

	std::filesystem::path pathFromUtf8(const std::string &path)
	{
		if (path.empty())
			return std::filesystem::path();

		const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
		if (length <= 1)
			return std::filesystem::path();

		std::wstring wide(static_cast<size_t>(length), L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, wide.data(), length);
		wide.resize(static_cast<size_t>(length - 1));
		return std::filesystem::path(wide);
	}

	std::string pathToUtf8(const std::wstring &wide)
	{
		if (wide.empty())
			return std::string();

		const int length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (length <= 1)
			return std::string();

		std::string result(static_cast<size_t>(length), '\0');
		WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), length, nullptr, nullptr);
		result.resize(static_cast<size_t>(length - 1));
		return result;
	}

	Vst2Instance *instanceFromEffect(AEffect *effect)
	{
		return (effect != nullptr) ? reinterpret_cast<Vst2Instance *>(effect->resvd1) : nullptr;
	}

	VstIntPtr FT2_VSTCALLBACK scanCallback(AEffect *, VstInt32 opcode, VstInt32, VstIntPtr, void *, float)
	{
		switch (opcode)
		{
			case audioMasterVersion: return kVstVersion;
			case audioMasterCurrentId: return requestedShellId;
			case audioMasterGetAutomationState: return kVstAutomationOff;
			case audioMasterGetCurrentProcessLevel: return kVstProcessLevelRealtime;
			case audioMasterGetLanguage: return kVstLangEnglish;
			default: return 0;
		}
	}

	VstIntPtr FT2_VSTCALLBACK pluginCallback(AEffect *effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt)
	{
		Vst2Instance *instance = instanceFromEffect(effect);
		if (instance == nullptr)
		{
			// the plug-in is created before it can be tied to a host instance
			if (opcode == audioMasterVersion)
				return kVstVersion;
			if (opcode == audioMasterCurrentId)
				return requestedShellId;
			return 0;
		}

		return instance->hostCallback(opcode, index, value, ptr, opt);
	}

	LRESULT CALLBACK vst2EditorWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		Vst2Instance *instance = reinterpret_cast<Vst2Instance *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		if (message == WM_NCCREATE)
		{
			const CREATESTRUCTW *createInfo = reinterpret_cast<const CREATESTRUCTW *>(lParam);
			instance = static_cast<Vst2Instance *>(createInfo->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(instance));
			if (instance != nullptr)
				instance->editorWindow = hwnd;
		}

		if (message == WM_TIMER && wParam == kEditorIdleTimerId)
		{
			if (instance != nullptr)
				instance->idleEditor();
			return 0;
		}
		if (message == WM_CLOSE)
		{
			ShowWindow(hwnd, SW_HIDE);
			return 0;
		}
		if (message == WM_NCDESTROY && instance != nullptr && instance->editorWindow == hwnd)
			instance->editorWindow = nullptr;

		// the plug-in's editor is a child window and gets the keys it wants
		// directly, so the frame must not pass them on (Windows would beep)
		switch (message)
		{
			case WM_KEYDOWN: case WM_KEYUP:
			case WM_CHAR: case WM_UNICHAR:
			case WM_DEADCHAR: case WM_SYSDEADCHAR:
			case WM_SYSCHAR: case WM_SYSKEYUP:
				return 0;
			default:
				break;
		}

		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
}

Vst2Instance::~Vst2Instance()
{
	closeEditor();

	if (effect != nullptr)
	{
		allNotesOff();
		sendEvents();

		if (resumed)
			effect->dispatcher(effect, effMainsChanged, 0, 0, nullptr, 0.0f);

		effect->dispatcher(effect, effClose, 0, 0, nullptr, 0.0f);
		effect = nullptr;
	}

	if (module != nullptr)
	{
		FreeLibrary(module);
		module = nullptr;
	}
}

bool vst2EnumerateModule(const char *modulePathUtf8, std::vector<Vst2PluginEntry> &entries, std::string &error)
{
	entries.clear();
	error.clear();

	if (modulePathUtf8 == nullptr || modulePathUtf8[0] == '\0')
	{
		error = "Invalid plug-in path";
		return false;
	}

	const std::wstring widePath = pathFromUtf8(modulePathUtf8).wstring();
	if (widePath.empty())
	{
		error = "Invalid plug-in path";
		return false;
	}

	HMODULE module = LoadLibraryExW(widePath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (module == nullptr)
	{
		char message[256];
		std::snprintf(message, sizeof(message), "Could not load %s (Windows error %lu)",
			modulePathUtf8, static_cast<unsigned long>(GetLastError()));
		error = message;
		return false;
	}

	auto pluginMain = reinterpret_cast<PluginMainProc>(GetProcAddress(module, "VSTPluginMain"));
	if (pluginMain == nullptr)
		pluginMain = reinterpret_cast<PluginMainProc>(GetProcAddress(module, "main"));

	if (pluginMain == nullptr)
	{
		error = std::string("VSTPluginMain export is missing in ") + modulePathUtf8;
		FreeLibrary(module);
		return false;
	}

	const std::string fileStem = pathToUtf8(std::filesystem::path(widePath).stem().wstring());
	int32_t shellId = 0;

	for (int32_t iteration = 0; iteration < 256; iteration++)
	{
		requestedShellId = shellId;
		AEffect *effect = pluginMain(scanCallback);
		requestedShellId = 0;

		if (effect == nullptr || effect->magic != kEffectMagic || effect->dispatcher == nullptr)
		{
			if (iteration == 0)
				error = std::string("Could not create a plug-in instance in ") + modulePathUtf8;
			break;
		}

		effect->dispatcher(effect, effOpen, 0, 0, nullptr, 0.0f);

		char buffer[512];

		buffer[0] = '\0';
		if (effect->dispatcher(effect, effGetProductString, 0, 0, buffer, 0.0f) == 0 || buffer[0] == '\0')
		{
			buffer[0] = '\0';
			if (effect->dispatcher(effect, effGetEffectName, 0, 0, buffer, 0.0f) == 0 || buffer[0] == '\0')
				copyText(buffer, sizeof(buffer), fileStem.c_str());
		}

		Vst2PluginEntry entry = {};
		copyText(entry.name, sizeof(entry.name), buffer);

		buffer[0] = '\0';
		effect->dispatcher(effect, effGetVendorString, 0, 0, buffer, 0.0f);
		copyText(entry.vendor, sizeof(entry.vendor), buffer);

		const int32_t version = static_cast<int32_t>(effect->dispatcher(effect, effGetVendorVersion, 0, 0, nullptr, 0.0f));
		if (version > 0)
			std::snprintf(entry.version, sizeof(entry.version), "%d", version);

		const int32_t category = static_cast<int32_t>(effect->dispatcher(effect, effGetPlugCategory, 0, 0, nullptr, 0.0f));
		const bool isShell = category == kPlugCategShell;

		entry.identifier = (effect->uniqueID != 0) ? effect->uniqueID : shellId;
		std::snprintf(entry.uniqueId, sizeof(entry.uniqueId), "%08X", static_cast<uint32_t>(entry.identifier));
		entry.isInstrument = (effect->flags & effFlagsIsSynth) != 0 ||
			category == kPlugCategSynth || category == kPlugCategGenerator;
		entry.isShell = isShell;

		// ask the shell DLL for the next plug-in it contains (0 = no more)
		int32_t nextShellId = 0;
		if (isShell)
		{
			char nextName[kVstMaxEffectNameLen * 2] = {};
			nextShellId = static_cast<int32_t>(effect->dispatcher(effect, effShellGetNextPlugin, 0, 0, nextName, 0.0f));
		}

		effect->dispatcher(effect, effClose, 0, 0, nullptr, 0.0f);
		effect = nullptr;

		if (entry.name[0] == '\0')
			copyText(entry.name, sizeof(entry.name), fileStem.c_str());

		bool duplicate = false;
		for (const auto &existing : entries)
		{
			if (existing.identifier == entry.identifier)
			{
				duplicate = true;
				break;
			}
		}
		if (!duplicate)
			entries.push_back(entry);

		if (!isShell || nextShellId == 0)
			break;

		shellId = nextShellId;
	}

	FreeLibrary(module);
	return !entries.empty();
}

bool Vst2Instance::initialize(const PluginLoadRequest &request)
{
	isEffect = request.effectMode;
	pluginName = (request.name != nullptr) ? request.name : "";
	lastErrorText.clear();

	if (request.modulePath == nullptr || request.modulePath[0] == '\0')
	{
		lastErrorText = "Invalid plug-in path";
		return false;
	}

	const std::wstring widePath = pathFromUtf8(request.modulePath).wstring();
	if (widePath.empty())
	{
		lastErrorText = "Invalid plug-in path";
		return false;
	}

	moduleDirectory = pathToUtf8(std::filesystem::path(widePath).parent_path().wstring());

	module = LoadLibraryExW(widePath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (module == nullptr)
	{
		char message[256];
		std::snprintf(message, sizeof(message), "Could not load module (Windows error %lu)",
			static_cast<unsigned long>(GetLastError()));
		lastErrorText = message;
		return false;
	}

	auto pluginMain = reinterpret_cast<PluginMainProc>(GetProcAddress(module, "VSTPluginMain"));
	if (pluginMain == nullptr)
		pluginMain = reinterpret_cast<PluginMainProc>(GetProcAddress(module, "main"));

	if (pluginMain == nullptr)
	{
		lastErrorText = "VSTPluginMain export is missing";
		return false;
	}

	int32_t identifier = 0;
	if (request.classID != nullptr && request.classID[0] != '\0')
		identifier = static_cast<int32_t>(std::strtoul(request.classID, nullptr, 16));

	sampleRate = (request.sampleRate > 0.0f) ? request.sampleRate : 44100.0f;

	requestedShellId = identifier;
	effect = pluginMain(pluginCallback);
	requestedShellId = 0;

	if (effect == nullptr || effect->magic != kEffectMagic || effect->dispatcher == nullptr)
	{
		lastErrorText = "Plug-in could not be created";
		return false;
	}

	// the callback needs to find its host instance again
	effect->resvd1 = reinterpret_cast<VstIntPtr>(this);

	effect->dispatcher(effect, effOpen, 0, 0, nullptr, 0.0f);

	numInputs = effect->numInputs;
	numOutputs = effect->numOutputs;
	if (numInputs < 0 || numInputs > 2 || numOutputs < 1 || numOutputs > 2)
	{
		lastErrorText = "Plug-in does not have a mono/stereo layout";
		return false;
	}
	if (isEffect && numInputs < 1)
	{
		lastErrorText = "Plug-in has no audio inputs";
		return false;
	}
	outputChannelCount = (numOutputs > 1) ? 2 : 1;

	hasEditor = (effect->flags & effFlagsHasEditor) != 0;
	canReplacing = (effect->flags & (effFlagsCanReplacing | effFlagsCanDoubleReplacing)) != 0 &&
		effect->processReplacing != nullptr;
	if (!canReplacing)
	{
		lastErrorText = "Plug-in does not support float audio processing";
		return false;
	}

	// the tracker mixer is float32, so ask VST 2.4 plug-ins for that precision
	const intptr_t vstVersion = effect->dispatcher(effect, effGetVstVersion, 0, 0, nullptr, 0.0f);
	if (vstVersion >= kVstVersion)
		effect->dispatcher(effect, effSetProcessPrecision, 0, kVstProcessPrecision32, nullptr, 0.0f);

	effect->dispatcher(effect, effSetBlockSize, 0, blockSize, nullptr, 0.0f);
	effect->dispatcher(effect, effSetSampleRate, 0, 0, nullptr, sampleRate);

	effect->dispatcher(effect, effMainsChanged, 0, 1, nullptr, 0.0f);
	resumed = true;
	processing = true;

	inputL.assign(blockSize, 0.0f);
	inputR.assign(blockSize, 0.0f);
	outputL.assign(blockSize, 0.0f);
	outputR.assign(blockSize, 0.0f);

	inputChannels = { inputL.data(), inputR.data() };
	outputChannels = { outputL.data(), outputR.data() };

	updateLatency();
	return true;
}

void Vst2Instance::setSampleRate(float newSampleRate)
{
	if (effect == nullptr || newSampleRate <= 0.0f)
		return;

	// the sample rate may only change while the plug-in is suspended
	if (resumed)
	{
		effect->dispatcher(effect, effMainsChanged, 0, 0, nullptr, 0.0f);
		resumed = false;
	}

	sampleRate = newSampleRate;
	effect->dispatcher(effect, effSetSampleRate, 0, 0, nullptr, sampleRate);
	effect->dispatcher(effect, effSetBlockSize, 0, blockSize, nullptr, 0.0f);
	effect->dispatcher(effect, effMainsChanged, 0, 1, nullptr, 0.0f);
	resumed = true;

	updateLatency();
}

void Vst2Instance::updateLatency()
{
	latencySamples = 0;
	if (effect == nullptr)
		return;

	int32_t latency = effect->initialDelay;
	if (latency < 0)
		latency = 0;
	if (latency > MAX_PLUGIN_LATENCY_SAMPLES)
		latency = MAX_PLUGIN_LATENCY_SAMPLES;

	latencySamples = latency;
}

void Vst2Instance::updateIoChannels()
{
	if (effect == nullptr)
		return;

	numInputs = std::clamp<int32_t>(effect->numInputs, 0, 2);
	numOutputs = std::clamp<int32_t>(effect->numOutputs, 1, 2);
	outputChannelCount = (numOutputs > 1) ? 2 : 1;
}

void Vst2Instance::addMidiEvent(uint8_t status, uint8_t data1, uint8_t data2)
{
	if (eventCount >= static_cast<int32_t>(midiEvents.size()))
		return;

	VstMidiEvent &event = midiEvents[static_cast<size_t>(eventCount++)];
	std::memset(&event, 0, sizeof(event));
	event.type = kVstMidiType;
	event.byteSize = sizeof(VstMidiEvent);
	event.deltaFrames = 0;
	event.midiData = static_cast<VstInt32>(static_cast<uint32_t>(status) |
		(static_cast<uint32_t>(data1) << 8) | (static_cast<uint32_t>(data2) << 16));
}

bool Vst2Instance::sendEvents()
{
	if (effect == nullptr || eventCount == 0)
		return true;

	VstEvents events = {};
	events.numEvents = eventCount;
	for (int32_t i = 0; i < eventCount; i++)
	{
		eventPointers[static_cast<size_t>(i)] = reinterpret_cast<VstEvent *>(&midiEvents[static_cast<size_t>(i)]);
		events.events[i] = eventPointers[static_cast<size_t>(i)];
	}

	eventCount = 0;
	return effect->dispatcher(effect, effProcessEvents, 0, 0, &events, 0.0f) != 0;
}

int32_t Vst2Instance::noteOn(uint8_t pitch, float velocity)
{
	if (effect == nullptr)
		return -1;

	int32_t midiVelocity = static_cast<int32_t>(velocity * 127.0f + 0.5f);
	if (midiVelocity < 1)
		midiVelocity = 1;
	if (midiVelocity > 127)
		midiVelocity = 127;

	const int32_t noteId = noteIdCounter++;
	if (noteIdCounter <= 0)
		noteIdCounter = 1;

	addMidiEvent(0x90, pitch, static_cast<uint8_t>(midiVelocity));

	for (auto &held : heldNotes)
	{
		if (held.id != 0)
			continue;

		held.id = noteId;
		held.pitch = pitch;
		break;
	}

	return noteId;
}

void Vst2Instance::noteOff(uint8_t pitch, int32_t noteId)
{
	if (effect == nullptr || noteId <= 0)
		return;

	bool found = false;
	for (auto &held : heldNotes)
	{
		if (held.id != noteId)
			continue;

		held.id = 0;
		found = true;
		break;
	}

	if (!found)
		return; // the note was never started on this instance

	addMidiEvent(0x80, pitch, 0);
}

void Vst2Instance::allNotesOff()
{
	bool hadNotes = false;
	for (auto &held : heldNotes)
	{
		if (held.id == 0)
			continue;

		addMidiEvent(0x80, static_cast<uint8_t>(held.pitch), 0);
		held.id = 0;
		hadNotes = true;
	}

	if (hadNotes)
		addMidiEvent(0xB0, 123, 0); // CC all notes off
}

void Vst2Instance::updateTimeInfo(const PluginHostTransport &transport)
{
	timeInfo.samplePos = static_cast<double>(transport.projectTimeSamples);
	timeInfo.sampleRate = transport.sampleRate;
	timeInfo.nanoSeconds = 0.0;
	timeInfo.ppqPos = transport.projectTimeMusic;
	timeInfo.tempo = transport.tempo;
	// tracker beats are 4 rows, so one bar is 4 quarter notes
	timeInfo.barStartPos = std::floor(transport.projectTimeMusic * 0.25) * 4.0;
	timeInfo.cycleStartPos = 0.0;
	timeInfo.cycleEndPos = 0.0;
	timeInfo.timeSigNumerator = 4;
	timeInfo.timeSigDenominator = 4;
	timeInfo.smpteOffset = 0;
	timeInfo.smpteFrameRate = 0;
	timeInfo.samplesToNextClock = 0;
	timeInfo.flags = kVstPpqPosValid | kVstTempoValid | kVstBarsValid | kVstTimeSigValid;
	if (transport.playing)
		timeInfo.flags |= kVstTransportPlaying;
}

bool Vst2Instance::process(uint32_t numSamples, float *scopeBuffer, std::atomic<uint32_t> &scopePosition,
	const PluginHostTransport &transport)
{
	if (effect == nullptr || !processing || scopeBuffer == nullptr || numSamples == 0 ||
		numSamples > static_cast<uint32_t>(blockSize))
	{
		return false;
	}

	updateTimeInfo(transport);
	sendEvents();

	std::fill(inputL.begin(), inputL.begin() + numSamples, 0.0f);
	std::fill(inputR.begin(), inputR.begin() + numSamples, 0.0f);
	std::fill(outputL.begin(), outputL.begin() + numSamples, 0.0f);
	std::fill(outputR.begin(), outputR.begin() + numSamples, 0.0f);

	float *inputs[2] = { inputL.data(), inputR.data() };
	float *outputs[2] = { outputL.data(), outputR.data() };
	effect->processReplacing(effect, inputs, outputs, static_cast<VstInt32>(numSamples));

	if (numOutputs < 2)
		std::copy(outputL.begin(), outputL.begin() + numSamples, outputR.begin());

	const uint32_t scopeMask = PLUGIN_SCOPE_BUFFER_SIZE - 1;
	uint32_t scopePos = scopePosition.load(std::memory_order_relaxed);
	for (uint32_t i = 0; i < numSamples; i++)
	{
		const float l = std::clamp(outputL[i], -8.0f, 8.0f);
		const float r = std::clamp(outputR[i], -8.0f, 8.0f);
		scopeBuffer[scopePos & scopeMask] = (l + r) * 0.5f;
		scopePos++;
	}

	scopePosition.store(scopePos, std::memory_order_relaxed);
	return true;
}

void Vst2Instance::processEffect(uint32_t numSamples, float *left, float *right,
	const PluginHostTransport &transport)
{
	if (!isEffect || effect == nullptr || !processing || left == nullptr || right == nullptr ||
		numSamples == 0 || numSamples > static_cast<uint32_t>(blockSize))
	{
		return;
	}

	updateTimeInfo(transport);
	sendEvents();

	if (numInputs > 1)
	{
		std::copy(left, left + numSamples, inputL.begin());
		std::copy(right, right + numSamples, inputR.begin());
	}
	else
	{
		for (uint32_t i = 0; i < numSamples; i++)
			inputL[i] = (left[i] + right[i]) * 0.5f;
	}

	std::fill(outputL.begin(), outputL.begin() + numSamples, 0.0f);
	std::fill(outputR.begin(), outputR.begin() + numSamples, 0.0f);

	float *inputs[2] = { inputL.data(), inputR.data() };
	float *outputs[2] = { outputL.data(), outputR.data() };
	effect->processReplacing(effect, inputs, outputs, static_cast<VstInt32>(numSamples));

	if (numOutputs > 1)
	{
		std::copy(outputL.begin(), outputL.begin() + numSamples, left);
		std::copy(outputR.begin(), outputR.begin() + numSamples, right);
	}
	else
	{
		for (uint32_t i = 0; i < numSamples; i++)
			left[i] = right[i] = outputL[i];
	}
}

namespace
{
	// header of the state blob stored in .xmv files for a VST2 instrument
	const char kVst2StateMagic[8] = { 'F', 'T', '2', 'V', '2', 'S', 'T', '1' };

	struct Vst2StateHeader
	{
		char magic[8];
		uint32_t mode; // 0 = program chunk, 1 = raw parameter values
		uint32_t program;
		uint32_t size;
	};
}

bool Vst2Instance::captureState(std::vector<uint8_t> &state)
{
	state.clear();
	if (effect == nullptr)
		return false;

	Vst2StateHeader header = {};
	memcpy(header.magic, kVst2StateMagic, sizeof(header.magic));
	header.program = static_cast<uint32_t>(effect->dispatcher(effect, effGetProgram, 0, 0, nullptr, 0.0f));

	if ((effect->flags & effFlagsProgramChunks) != 0)
	{
		void *chunk = nullptr;
		const intptr_t size = effect->dispatcher(effect, effGetChunk, 1 /*current program*/, 0, &chunk, 0.0f);
		if (size > 0 && chunk != nullptr && static_cast<uint64_t>(size) <= MAX_STATE_BYTES)
		{
			header.mode = 0;
			header.size = static_cast<uint32_t>(size);
			state.resize(sizeof(header) + static_cast<size_t>(size));
			memcpy(state.data(), &header, sizeof(header));
			memcpy(state.data() + sizeof(header), chunk, static_cast<size_t>(size));
			free(chunk);
			return true;
		}
		if (chunk != nullptr)
			free(chunk);
	}

	// no chunk support: remember the parameter values instead
	if (effect->numParams <= 0 || effect->numParams > static_cast<int32_t>(MAX_STATE_BYTES / sizeof(float)))
		return false;

	header.mode = 1;
	header.size = static_cast<uint32_t>(effect->numParams) * sizeof(float);
	state.resize(sizeof(header) + header.size);
	memcpy(state.data(), &header, sizeof(header));

	float *values = reinterpret_cast<float *>(state.data() + sizeof(header));
	for (int32_t i = 0; i < effect->numParams; i++)
		values[i] = effect->getParameter(effect, i);

	return true;
}

bool Vst2Instance::applyState(const void *stateData, uint32_t stateSize)
{
	if (effect == nullptr || stateData == nullptr || stateSize < sizeof(Vst2StateHeader))
		return false;

	Vst2StateHeader header = {};
	memcpy(&header, stateData, sizeof(header));
	if (memcmp(header.magic, kVst2StateMagic, sizeof(header.magic)) != 0)
		return false;
	if (header.size > stateSize - sizeof(header))
		return false;

	const uint8_t *payload = static_cast<const uint8_t *>(stateData) + sizeof(header);

	if (header.mode == 0)
	{
		effect->dispatcher(effect, effBeginSetProgram, 0, 0, nullptr, 0.0f);
		if (header.program < static_cast<uint32_t>(effect->numPrograms))
			effect->dispatcher(effect, effSetProgram, 0, static_cast<VstIntPtr>(header.program), nullptr, 0.0f);
		const bool ok = effect->dispatcher(effect, effSetChunk, 1, static_cast<VstIntPtr>(header.size),
			const_cast<uint8_t *>(payload), 0.0f) != 0;
		effect->dispatcher(effect, effEndSetProgram, 0, 0, nullptr, 0.0f);
		return ok;
	}

	if (header.mode == 1)
	{
		if ((header.size % sizeof(float)) != 0)
			return false;

		const uint32_t count = header.size / sizeof(float);
		for (uint32_t i = 0; i < count; i++)
		{
			float value;
			memcpy(&value, payload + i * sizeof(float), sizeof(value));
			effect->setParameter(effect, static_cast<VstInt32>(i), value);
		}
		return true;
	}

	return false;
}

bool Vst2Instance::openEditorWindow(int32_t width, int32_t height)
{
	static bool classRegistered = false;
	if (!classRegistered)
	{
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = vst2EditorWindowProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
		wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
		wc.lpszClassName = kEditorWindowClass;
		classRegistered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	}
	if (!classRegistered)
		return false;

	wchar_t title[192];
	MultiByteToWideChar(CP_UTF8, 0, pluginName.c_str(), -1, title, static_cast<int>(std::size(title)));
	title[std::size(title) - 1] = L'\0';
	if (title[0] == L'\0')
		wcscpy(title, L"VST Editor");
	else if (wcslen(title) + 12 < std::size(title))
		wcscat(title, L" - FT2 Clone");

	RECT windowRect = { 0, 0, width, height };
	AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE, 0);

	editorWindow = CreateWindowExW(0, kEditorWindowClass, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
		video.hWnd, nullptr, GetModuleHandleW(nullptr), this);
	if (editorWindow == nullptr)
		return false;

	if (effect->dispatcher(effect, effEditOpen, 0, 0, editorWindow, 0.0f) == 0)
	{
		DestroyWindow(editorWindow);
		editorWindow = nullptr;
		return false;
	}
	editorOpen = true;

	SetTimer(editorWindow, kEditorIdleTimerId, kEditorIdleIntervalMs, nullptr);
	ShowWindow(editorWindow, SW_SHOW);
	UpdateWindow(editorWindow);
	SetForegroundWindow(editorWindow);
	return true;
}

bool Vst2Instance::openEditor()
{
	if (effect == nullptr || !hasEditor)
		return false;

	if (editorWindow != nullptr)
	{
		ShowWindow(editorWindow, SW_SHOW);
		SetForegroundWindow(editorWindow);
		return true;
	}

	int32_t width = 400, height = 300;
	ERect *rect = nullptr;
	effect->dispatcher(effect, effEditGetRect, 0, 0, &rect, 0.0f);
	if (rect != nullptr)
	{
		const int32_t rectWidth = rect->right - rect->left;
		const int32_t rectHeight = rect->bottom - rect->top;
		if (rectWidth > 0 && rectHeight > 0)
		{
			width = rectWidth;
			height = rectHeight;
		}
	}

	return openEditorWindow(width, height);
}

void Vst2Instance::idleEditor()
{
	if (effect != nullptr && editorOpen)
		effect->dispatcher(effect, effEditIdle, 0, 0, nullptr, 0.0f);
}

void Vst2Instance::closeEditor()
{
	if (editorWindow == nullptr)
		return;

	KillTimer(editorWindow, kEditorIdleTimerId);

	if (effect != nullptr && editorOpen)
	{
		effect->dispatcher(effect, effEditClose, 0, 0, nullptr, 0.0f);
		editorOpen = false;
	}

	DestroyWindow(editorWindow);
	editorWindow = nullptr;
}

intptr_t Vst2Instance::hostCallback(int32_t opcode, int32_t index, intptr_t value, void *ptr, float opt)
{
	switch (opcode)
	{
		case audioMasterAutomate:
			// the plug-in already applied the value; there is no host automation
			return 0;

		case audioMasterVersion:
			return kVstVersion;

		case audioMasterCurrentId:
			return effect != nullptr ? effect->uniqueID : 0;

		case audioMasterIdle:
			idleEditor();
			return 0;

		case audioMasterPinConnected:
			return 1;

		case audioMasterWantMidi:
			return 1;

		case audioMasterGetTime:
			return reinterpret_cast<intptr_t>(&timeInfo);

		case audioMasterProcessEvents:
			return 0; // plug-in to host events are not used

		case audioMasterSetTime:
		case audioMasterGetParameterQuantization:
			return 0;

		case audioMasterTempoAt:
			return static_cast<intptr_t>(timeInfo.tempo * 10000.0);

		case audioMasterGetNumAutomatableParameters:
			return effect != nullptr ? effect->numParams : 0;

		case audioMasterIOChanged:
			updateIoChannels();
			return 0;

		case audioMasterNeedIdle:
			return 1;

		case audioMasterSizeWindow:
			if (editorWindow != nullptr)
			{
				SetWindowPos(editorWindow, nullptr, 0, 0, index, static_cast<int>(value),
					SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			}
			return 1;

		case audioMasterGetSampleRate:
			return static_cast<intptr_t>(sampleRate + 0.5f);

		case audioMasterGetBlockSize:
			return blockSize;

		case audioMasterGetInputLatency:
		case audioMasterGetOutputLatency:
			return latencySamples;

		case audioMasterGetPreviousPlug:
		case audioMasterGetNextPlug:
			return 0;

		case audioMasterWillReplaceOrAccumulate:
			return 1; // replacing mode

		case audioMasterGetCurrentProcessLevel:
			return kVstProcessLevelRealtime;

		case audioMasterGetAutomationState:
			return kVstAutomationOff;

		case audioMasterSetOutputSampleRate:
			return 0;

		case audioMasterGetVendorString:
		case audioMasterGetProductString:
			if (ptr != nullptr)
				copyText(static_cast<char *>(ptr), kVstMaxVendorStrLen, "FT2 Clone");
			return 1;

		case audioMasterGetVendorVersion:
			return 1000;

		case audioMasterVendorSpecific:
		case audioMasterSetIcon:
		case audioMasterOpenWindow:
		case audioMasterCloseWindow:
		case audioMasterOpenFileSelector:
		case audioMasterCloseFileSelector:
		case audioMasterEditFile:
		case audioMasterGetChunkFile:
			return 0;

		case audioMasterCanDo:
		{
			const char *canDo = static_cast<const char *>(ptr);
			if (canDo == nullptr)
				return 0;

			if (!std::strcmp(canDo, "sendVstEvents") ||
				!std::strcmp(canDo, "sendVstMidiEvent") ||
				!std::strcmp(canDo, "receiveVstTimeInfo") ||
				!std::strcmp(canDo, "supplyIdle") ||
				!std::strcmp(canDo, "startStopProcess") ||
				!std::strcmp(canDo, "bypass"))
			{
				return 1;
			}
			return 0;
		}

		case audioMasterGetLanguage:
			return kVstLangEnglish;

		case audioMasterGetDirectory:
			if (ptr != nullptr)
				copyText(static_cast<char *>(ptr), 1024, moduleDirectory.c_str());
			return 1;

		case audioMasterBeginEdit:
		case audioMasterEndEdit:
		case audioMasterUpdateDisplay:
			return 0;

		default:
			(void)index;
			(void)value;
			(void)opt;
			return 0;
	}
}

#endif
