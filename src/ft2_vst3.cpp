#include "ft2_vst3.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <memory>
#include <new>
#include <string>
#include <cwctype>
#include <vector>

#ifndef MAX_WAV_RENDER_SAMPLES_PER_TICK
#define MAX_WAV_RENDER_SAMPLES_PER_TICK 4096
#endif

#ifdef _WIN32
#include <windows.h>
#include "ft2_vst2.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/gui/iplugview.h"
extern "C" {
#include "ft2_audio.h"
#include "ft2_video.h"
}
#endif

#ifdef _WIN32
namespace Steinberg {
DEF_CLASS_IID(FUnknown)
DEF_CLASS_IID(IBStream)
DEF_CLASS_IID(IPlugFrame)
namespace Vst {
DEF_CLASS_IID(IComponent)	DEF_CLASS_IID(IAudioProcessor)
	DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IConnectionPoint)
DEF_CLASS_IID(IComponentHandler)	DEF_CLASS_IID(IComponentHandler2)
DEF_CLASS_IID(IEventList)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
DEF_CLASS_IID(IHostApplication)
DEF_CLASS_IID(IMessage)
DEF_CLASS_IID(IAttributeList)
}
}
#endif

namespace
{
	std::mutex scanMutex;
	std::mutex scanRunMutex;
	std::mutex progressMutex;
	std::vector<std::string> extraPaths;
	std::vector<vst3PluginInfo_t> plugins;
	std::string lastError;
	std::string lastLoadError;
	std::mutex loadErrorMutex;

	void setLoadError(const char *reason)
	{
		std::lock_guard<std::mutex> lock(loadErrorMutex);
		lastLoadError = (reason != nullptr) ? reason : "";
	}

	vst3ScanProgress_t scanProgress;
	// scan report: what the last scan found but left out of the plug-in list
	std::vector<vst3SkippedPlugin_t> skippedPlugins;
	std::array<std::atomic<int32_t>, MAX_VST3_INSTRUMENTS> assignedInstrumentPlugins;
	struct AssignedPluginDefaults { AssignedPluginDefaults() { for (auto &slot : assignedInstrumentPlugins) slot.store(-1); } } assignedPluginDefaults;

#ifdef _WIN32
	using namespace Steinberg;
	using namespace Steinberg::Vst;
	using GetFactoryProc = IPluginFactory *(PLUGIN_API *)();
	constexpr uint32_t VST3_SCOPE_BUFFER_SIZE = PLUGIN_SCOPE_BUFFER_SIZE; // must be a power of two
	constexpr int32_t MAX_MASTER_EFFECTS = MAX_VST3_MASTER_EFFECTS;
	// upper bound for latency compensation (a plug-in can report a lot); keeping
	// this sane avoids huge delay buffers and absurd tracking latency
	constexpr int32_t MAX_PDC_LATENCY_SAMPLES = MAX_PLUGIN_LATENCY_SAMPLES;
	struct PluginInstance;

	// note ids handed to the plug-ins. They are unique for the whole process, so an
	// id that belongs to a plug-in instance which has since been replaced can never
	// match a note of the new instance. Only touched while the audio is locked.
	int32_t nextNoteId = 1;

	static void vst3DebugLog(const char *, ...) {}

	static const char *vst3ResultToString(tresult res)
	{
		switch (res)
		{
			case kResultOk:  return "kResultOk (== kResultTrue)";
			case kResultFalse: return "kResultFalse";
			case kNoInterface: return "kNoInterface";
			case kInvalidArgument: return "kInvalidArgument";
			default: return nullptr;
		}
	}

	// Fixed-length stereo delay line, used for plug-in latency compensation.
	// The dry tracker mix is delayed by the longest plug-in latency, and every
	// instrument's output by the remaining difference, so that notes and sample
	// channels land on the same sample instead of the plug-in sounding late.
	struct StereoDelayLine
	{
		void resize(size_t newSize)
		{
			size = newSize;
			left.assign(size, 0.0f);
			right.assign(size, 0.0f);
			position = 0;
		}

		void clear()
		{
			std::fill(left.begin(), left.end(), 0.0f);
			std::fill(right.begin(), right.end(), 0.0f);
			position = 0;
		}

		void processBlock(float *leftSamples, float *rightSamples, uint32 numSamples)
		{
			if (size == 0) // no delay
				return;

			for (uint32 i = 0; i < numSamples; ++i)
			{
				const float inL = leftSamples[i];
				const float inR = rightSamples[i];

				leftSamples[i] = left[position];
				rightSamples[i] = right[position];

				left[position] = inL;
				right[position] = inR;

				if (++position >= size)
					position = 0;
			}
		}

		std::vector<float> left, right;
		size_t size = 0, position = 0;
	};

	class PluginEditorFrame final : public U::Implements<U::Directly<IPlugFrame>>
	{
	public:
		explicit PluginEditorFrame(PluginInstance *owner) : owner(owner) {}
		tresult PLUGIN_API resizeView(IPlugView *view, ViewRect *newSize) override;
	private:
		PluginInstance *owner;
	};

	static const wchar_t *kEditorWindowClass = L"FT2CloneVST3EditorWindow";
	static LRESULT CALLBACK vst3EditorWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	std::filesystem::path pathFromUtf8(const std::string &path);

	// Plug-ins use the objects handed out by IHostApplication::createInstance to
	// talk to their own component/controller counterpart. The SDK's reference
	// host implements IMessage and IAttributeList here, and a plug-in may rely on
	// them without checking the result: Serum 2 asks for an IMessage while it
	// builds its editor and dereferences the null pointer it gets from a host
	// that answers kNoInterface.
	class HostAttributeList final : public U::Implements<U::Directly<IAttributeList>>
	{
	public:
		tresult PLUGIN_API setInt(AttrID aid, int64 value) override
		{
			if (aid == nullptr) return kInvalidArgument;
			std::lock_guard<std::mutex> lock(mutex);
			Attribute &attribute = values[aid];
			attribute.type = Attribute::kInt;
			attribute.intValue = value;
			return kResultOk;
		}
		tresult PLUGIN_API getInt(AttrID aid, int64 &value) override
		{
			std::lock_guard<std::mutex> lock(mutex);
			const Attribute *attribute = find(aid, Attribute::kInt);
			if (attribute == nullptr) return kResultFalse;
			value = attribute->intValue;
			return kResultOk;
		}
		tresult PLUGIN_API setFloat(AttrID aid, double value) override
		{
			if (aid == nullptr) return kInvalidArgument;
			std::lock_guard<std::mutex> lock(mutex);
			Attribute &attribute = values[aid];
			attribute.type = Attribute::kFloat;
			attribute.floatValue = value;
			return kResultOk;
		}
		tresult PLUGIN_API getFloat(AttrID aid, double &value) override
		{
			std::lock_guard<std::mutex> lock(mutex);
			const Attribute *attribute = find(aid, Attribute::kFloat);
			if (attribute == nullptr) return kResultFalse;
			value = attribute->floatValue;
			return kResultOk;
		}
		tresult PLUGIN_API setString(AttrID aid, const TChar *string) override
		{
			if (aid == nullptr || string == nullptr) return kInvalidArgument;
			std::lock_guard<std::mutex> lock(mutex);
			Attribute &attribute = values[aid];
			attribute.type = Attribute::kString;
			attribute.stringValue.assign(string);
			return kResultOk;
		}
		tresult PLUGIN_API getString(AttrID aid, TChar *string, uint32 sizeInBytes) override
		{
			if (aid == nullptr || string == nullptr || sizeInBytes == 0) return kInvalidArgument;
			std::lock_guard<std::mutex> lock(mutex);
			const Attribute *attribute = find(aid, Attribute::kString);
			if (attribute == nullptr) return kResultFalse;
			const size_t byteCount = std::min(attribute->stringValue.size() * sizeof(TChar), (size_t)sizeInBytes);
			std::memcpy(string, attribute->stringValue.data(), byteCount);
			return kResultOk;
		}
		tresult PLUGIN_API setBinary(AttrID aid, const void *data, uint32 sizeInBytes) override
		{
			if (aid == nullptr) return kInvalidArgument;
			if (data == nullptr && sizeInBytes != 0) return kInvalidArgument;
			std::lock_guard<std::mutex> lock(mutex);
			Attribute &attribute = values[aid];
			attribute.type = Attribute::kBinary;
			const char *bytes = static_cast<const char *>(data);
			attribute.binaryValue.assign(bytes, bytes + sizeInBytes);
			return kResultOk;
		}
		tresult PLUGIN_API getBinary(AttrID aid, const void *&data, uint32 &sizeInBytes) override
		{
			std::lock_guard<std::mutex> lock(mutex);
			const Attribute *attribute = find(aid, Attribute::kBinary);
			if (attribute == nullptr) return kResultFalse;
			data = attribute->binaryValue.data();
			sizeInBytes = static_cast<uint32>(attribute->binaryValue.size());
			return kResultOk;
		}

	private:
		struct Attribute
		{
			enum Type { kInt, kFloat, kString, kBinary } type = kInt;
			int64 intValue = 0;
			double floatValue = 0.0;
			std::basic_string<TChar> stringValue;
			std::vector<char> binaryValue;
		};

		const Attribute *find(const char *aid, Attribute::Type type)
		{
			if (aid == nullptr) return nullptr;
			const auto it = values.find(aid);
			if (it == values.end() || it->second.type != type) return nullptr;
			return &it->second;
		}

		std::mutex mutex;
		std::map<std::string, Attribute> values;
	};

	class HostMessage final : public U::Implements<U::Directly<IMessage>>
	{
	public:
		~HostMessage() override
		{
			if (attributes != nullptr) attributes->release();
		}

		const char *PLUGIN_API getMessageID() override { return messageId; }
		void PLUGIN_API setMessageID(const char *id) override
		{
			std::lock_guard<std::mutex> lock(mutex);
			std::snprintf(messageId, sizeof(messageId), "%s", (id != nullptr) ? id : "");
		}
		IAttributeList *PLUGIN_API getAttributes() override
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (attributes == nullptr)
				attributes = new (std::nothrow) HostAttributeList();
			return attributes;
		}

	private:
		std::mutex mutex;
		char messageId[256] = {};
		HostAttributeList *attributes = nullptr;
	};

	class BasicHostApplication final : public U::Implements<U::Directly<IHostApplication>>
	{
	public:
		tresult PLUGIN_API getName(String128 name) override
		{
			const char16_t appName[] = u"FastTracker II Clone";
			std::copy(std::begin(appName), std::end(appName), name);
			return kResultOk;
		}
		tresult PLUGIN_API createInstance(TUID cid, TUID iid, void **obj) override
		{
			if (obj == nullptr) return kInvalidArgument;
			*obj = nullptr;

			if (FUnknownPrivate::iidEqual(cid, IMessage::iid) && FUnknownPrivate::iidEqual(iid, IMessage::iid))
			{
				auto *message = new (std::nothrow) HostMessage();
				if (message == nullptr) return kOutOfMemory;
				// the interface pointer, not the object itself, is what the plug-in calls
				*obj = static_cast<IMessage *>(message);
				return kResultOk;
			}
			if (FUnknownPrivate::iidEqual(cid, IAttributeList::iid) && FUnknownPrivate::iidEqual(iid, IAttributeList::iid))
			{
				auto *list = new (std::nothrow) HostAttributeList();
				if (list == nullptr) return kOutOfMemory;
				*obj = static_cast<IAttributeList *>(list);
				return kResultOk;
			}
			return kNoInterface;
		}
	};

	// In-memory IBStream handed to IComponent::getState/setState. Plug-in state
	// (the preset the user dialled in) is captured to and restored from a plain
	// byte vector. The reference count never reaches zero, so a plug-in that keeps
	// the stream for the duration of the call can't destroy this stack object.
	class MemoryStream final : public U::Implements<U::Directly<IBStream>>
	{
	public:
		MemoryStream() = default;
		MemoryStream(const void *source, size_t size)
		{
			if (source != nullptr && size > 0)
				data.assign(static_cast<const uint8_t *>(source), static_cast<const uint8_t *>(source) + size);
		}

		uint32 PLUGIN_API addRef() override { return 1; }
		uint32 PLUGIN_API release() override { return 1; }

		tresult PLUGIN_API read(void *buffer, int32 numBytes, int32 *numBytesRead) override
		{
			if (buffer == nullptr || numBytes < 0)
				return kInvalidArgument;

			const size_t available = (position < data.size()) ? data.size() - position : 0;
			const size_t amount = std::min(static_cast<size_t>(numBytes), available);
			if (amount > 0)
				memcpy(buffer, data.data() + position, amount);
			position += amount;

			if (numBytesRead != nullptr)
				*numBytesRead = static_cast<int32>(amount);
			return kResultOk;
		}

		tresult PLUGIN_API write(void *buffer, int32 numBytes, int32 *numBytesWritten) override
		{
			if (buffer == nullptr || numBytes < 0)
				return kInvalidArgument;

			const size_t amount = static_cast<size_t>(numBytes);
			if (position + amount > data.size())
				data.resize(position + amount);
			if (amount > 0)
				memcpy(data.data() + position, buffer, amount);
			position += amount;

			if (numBytesWritten != nullptr)
				*numBytesWritten = static_cast<int32>(amount);
			return kResultOk;
		}

		tresult PLUGIN_API seek(int64 pos, int32 mode, int64 *result) override
		{
			int64 newPosition = static_cast<int64>(position);
			if (mode == kIBSeekSet)
				newPosition = pos;
			else if (mode == kIBSeekCur)
				newPosition += pos;
			else if (mode == kIBSeekEnd)
				newPosition = static_cast<int64>(data.size()) + pos;
			else
				return kInvalidArgument;

			if (newPosition < 0)
				return kInvalidArgument;

			position = static_cast<size_t>(newPosition);
			if (result != nullptr)
				*result = newPosition;
			return kResultOk;
		}

		tresult PLUGIN_API tell(int64 *pos) override
		{
			if (pos == nullptr)
				return kInvalidArgument;
			*pos = static_cast<int64>(position);
			return kResultOk;
		}

		std::vector<uint8_t> data;

	private:
		size_t position = 0;
	};

	// Carries parameter edits made in a plug-in's own editor to its processor.
	// The editor calls IComponentHandler::performEdit on the UI thread, while the
	// processor expects the values as IParameterChanges in its next block, so the
	// edits cross the thread boundary through a small lock-free ring buffer.
	class HostParameterChanges final : public U::Implements<U::Directly<IParameterChanges>>
	{
	public:
		static constexpr uint32_t RING_SIZE = 1024; // power of two
		static constexpr int32_t MAX_QUEUES = 128;

		class Queue final : public IParamValueQueue
		{
		public:
			tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
			{
				if (obj == nullptr)
					return kInvalidArgument;
				if (FUnknownPrivate::iidEqual(iid, IParamValueQueue::iid) ||
					FUnknownPrivate::iidEqual(iid, FUnknown::iid))
				{
					*obj = this;
					return kResultOk;
				}
				*obj = nullptr;
				return kNoInterface;
			}
			// owned by its HostParameterChanges, a plug-in must not destroy it
			uint32 PLUGIN_API addRef() override { return 2; }
			uint32 PLUGIN_API release() override { return 1; }

			ParamID PLUGIN_API getParameterId() override { return paramId; }
			int32 PLUGIN_API getPointCount() override { return pointCount; }
			tresult PLUGIN_API getPoint(int32 index, int32 &sampleOffset, ParamValue &value) override
			{
				if (index < 0 || index >= pointCount)
					return kInvalidArgument;
				sampleOffset = points[static_cast<size_t>(index)].sampleOffset;
				value = points[static_cast<size_t>(index)].value;
				return kResultOk;
			}
			tresult PLUGIN_API addPoint(int32, ParamValue, int32 &) override { return kResultFalse; }

			struct Point { int32 sampleOffset; ParamValue value; };

			ParamID paramId = -1;
			int32 pointCount = 0;
			std::array<Point, 1> points{};
		};

		// UI thread: record an edit for the next block
		void push(ParamID id, ParamValue value)
		{
			const uint32_t write = writePos.load(std::memory_order_relaxed);
			const uint32_t read = readPos.load(std::memory_order_acquire);
			if (write - read >= RING_SIZE) // audio is not draining, drop the edit
				return;
			ring[write & (RING_SIZE - 1)] = { id, value };
			writePos.store(write + 1, std::memory_order_release);
		}

		// audio thread: turn the pending edits into one block of parameter queues.
		// A parameter edited several times between two blocks is applied once with
		// its newest value (offset 0), which is all a block start can express.
		void beginBlock()
		{
			queueCount = 0;
			uint32_t read = readPos.load(std::memory_order_relaxed);
			const uint32_t write = writePos.load(std::memory_order_acquire);
			while (read != write)
			{
				const Change change = ring[read & (RING_SIZE - 1)];
				read++;

				Queue *queue = findQueue(change.id);
				if (queue == nullptr)
				{
					if (queueCount >= MAX_QUEUES)
						continue;
					queue = &queues[static_cast<size_t>(queueCount++)];
					queue->paramId = change.id;
					queue->pointCount = 0;
				}

				queue->points[0] = { 0, change.value };
				queue->pointCount = 1;
			}
			readPos.store(read, std::memory_order_release);
		}

		int32 PLUGIN_API getParameterCount() override { return queueCount; }
		IParamValueQueue *PLUGIN_API getParameterData(int32 index) override
		{
			if (index < 0 || index >= queueCount)
				return nullptr;
			return &queues[static_cast<size_t>(index)];
		}
		IParamValueQueue *PLUGIN_API addParameterData(const ParamID &, int32 &) override { return nullptr; }

	private:
		struct Change { ParamID id; ParamValue value; };

		Queue *findQueue(ParamID id)
		{
			for (int32 i = 0; i < queueCount; ++i)
			{
				if (queues[static_cast<size_t>(i)].paramId == id)
					return &queues[static_cast<size_t>(i)];
			}
			return nullptr;
		}

		std::array<Change, RING_SIZE> ring{};
		std::atomic<uint32_t> writePos{ 0 };
		std::atomic<uint32_t> readPos{ 0 };
		std::array<Queue, MAX_QUEUES> queues{};
		int32 queueCount = 0;
	};

	// VST3 requires the host to give every edit controller an IComponentHandler
	// (the interface is marked "mandatory" in the SDK). Plug-ins call back into it
	// while their editor is alive: parameter edit gestures, restart requests, dirty
	// state. A plug-in that finds a null handler - Serum 2 does this while building
	// its UI - dereferences it and takes the whole host down.
	class ComponentHandler final : public U::Implements<U::Directly<IComponentHandler, IComponentHandler2>>
	{
	public:
		// A gesture itself needs no bookkeeping (there is no undo/host automation
		// here), but the edited value has to reach the processor or the controls in
		// a plug-in's editor only change its own display. Combined component/
		// controller plug-ins apply it directly, split ones take it from the queue.
		tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
		tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
		{
			if (parameterChanges != nullptr)
				parameterChanges->push(id, value);
			if (controller != nullptr && !applyingEdit)
			{
				// guarded: a split plug-in re-emits the edit from inside this call
				applyingEdit = true;
				controller->setParamNormalized(id, value);
				applyingEdit = false;
			}
			return kResultOk;
		}
		tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
		tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }

		tresult PLUGIN_API setDirty(TBool) override { return kResultOk; }
		tresult PLUGIN_API requestOpenEditor(FIDString) override { return kResultOk; }
		tresult PLUGIN_API startGroupEdit() override { return kResultOk; }
		tresult PLUGIN_API finishGroupEdit() override { return kResultOk; }

		// cleared by closeEditor(), so the handler never points at a dead controller
		void setTargets(IEditController *editController, HostParameterChanges *changes)
		{
			controller = editController;
			parameterChanges = changes;
			applyingEdit = false;
		}

		IEditController *controller = nullptr;
		HostParameterChanges *parameterChanges = nullptr;
		bool applyingEdit = false;
	};

	class HostEventList final : public U::Implements<U::Directly<IEventList>>
	{
	public:
		static constexpr size_t MAX_EVENTS = 256;

		HostEventList()
		{
			events.reserve(MAX_EVENTS);
			deliveryEarly.reserve(MAX_EVENTS);
			deliveryScratch.reserve(MAX_EVENTS);
		}
		int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events.size()); }
		tresult PLUGIN_API getEvent(int32 index, Event &event) override
		{
			if (index < 0 || static_cast<size_t>(index) >= events.size()) return kInvalidArgument;
			event = events[static_cast<size_t>(index)]; return kResultOk;
		}
		tresult PLUGIN_API addEvent(Event &event) override
		{
			if (events.size() >= MAX_EVENTS) return kResultFalse;
			events.push_back(event); return kResultOk;
		}
		void clear() { events.clear(); }

		// A channel keys off its previous note before starting a new one, so a block
		// can contain "note-off X, note-on Y" pairs from several channels. A plug-in
		// that matches note-offs by pitch (most of them ignore note ids) would then
		// lose notes of a chord whose voices move between channels. Deliver the
		// note-offs of earlier voices first, which is what a per-channel MIDI stream
		// would do. A note-off belonging to a note-on of the same block (a note cut
		// right after it was started) keeps its position.
		void prepareForDelivery()
		{
			if (events.size() < 2)
				return;

			deliveryEarly.clear();
			deliveryScratch.clear();

			for (size_t i = 0; i < events.size(); ++i)
			{
				const Event &event = events[i];

				bool startedInThisBlock = false;
				if (event.type == Event::kNoteOffEvent)
				{
					for (size_t j = 0; j < i; ++j)
					{
						const Event &prior = events[j];
						if (prior.type == Event::kNoteOnEvent && prior.noteOn.noteId > 0 &&
							prior.noteOn.noteId == event.noteOff.noteId)
						{
							startedInThisBlock = true;
							break;
						}
					}
				}

				if (startedInThisBlock)
					deliveryScratch.push_back(event);
				else if (event.type == Event::kNoteOffEvent)
					deliveryEarly.push_back(event);
				else
					deliveryScratch.push_back(event);
			}

			if (deliveryEarly.empty())
			{
				deliveryScratch.clear();
				return;
			}

			events.clear();
			events.insert(events.end(), deliveryEarly.begin(), deliveryEarly.end());
			events.insert(events.end(), deliveryScratch.begin(), deliveryScratch.end());
		}

	private:
		std::vector<Event> events;
		std::vector<Event> deliveryEarly, deliveryScratch;
	};

	std::string uidToHex(const Steinberg::TUID uid);

	// converts the format independent transport state into a VST3 process context
	ProcessContext processContextFromTransport(const PluginHostTransport &transport)
	{
		ProcessContext context{};
		context.state = ProcessContext::kTempoValid | ProcessContext::kTimeSigValid |
		                ProcessContext::kProjectTimeMusicValid | ProcessContext::kBarPositionValid |
		                ProcessContext::kContTimeValid | ProcessContext::kSystemTimeValid;
		if (transport.playing)
			context.state |= ProcessContext::kPlaying;

		context.sampleRate = transport.sampleRate;
		context.tempo = transport.tempo;
		context.timeSigNumerator = 4;
		context.timeSigDenominator = 4;
		context.projectTimeSamples = transport.projectTimeSamples;
		context.continousTimeSamples = transport.projectTimeSamples;
		context.projectTimeMusic = transport.projectTimeMusic;
		// tracker beats are 4 rows, so one bar is 4 quarter notes
		context.barPositionMusic = std::floor(transport.projectTimeMusic * 0.25) * 4.0;
		context.systemTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		return context;
	}

	// hosts one VST3 plug-in, either an instrument (note driven, summed into the
	// mix) or an effect (fed the mix, output replaces it in the effect chain)
	struct PluginInstance final : PluginHostInstance
	{
		HMODULE module = nullptr;
		bool moduleInitialized = false; // InitDll was called, so ExitDll must be paired with it
		IPluginFactory *factory = nullptr;
		IComponent *component = nullptr;
		IAudioProcessor *processor = nullptr;
		IEditController *controller = nullptr;
		IPlugView *editorView = nullptr;
		IConnectionPoint *componentConnection = nullptr;
		IConnectionPoint *controllerConnection = nullptr;
		PluginEditorFrame *editorFrame = nullptr;
		ComponentHandler *componentHandler = nullptr;
		HWND editorWindow = nullptr;
		BasicHostApplication *host = nullptr;
		HostEventList *events = nullptr;
		HostParameterChanges *parameterChanges = nullptr;
		AudioBusBuffers inputBus{};
		AudioBusBuffers outputBus{};
		std::array<float *, 2> inputChannels{};
		std::array<float *, 2> outputChannels{};
		std::vector<float> inputL, inputR;
		int32 inputChannelCount = 0;
		bool hasEventBus = false;
		bool processing = false;
		bool initialized = false;
		bool active = false;
		bool controllerInitialized = false;
		bool editorAttached = false;
		ProcessContext processContext{};

		// notes this instance is still holding. Every note-on gets its own id, so
		// releasing one voice can't cut another one that happens to share its pitch
		// (e.g. the same note held by a second channel).
		struct HeldNote { int32_t id; int16_t pitch; };
		std::array<HeldNote, 256> heldNotes{};

		~PluginInstance()
		{
			closeEditor();
			if (componentHandler != nullptr)
			{
				componentHandler->release();
				componentHandler = nullptr;
			}
			if (processor != nullptr && processing) processor->setProcessing(false);
			if (component != nullptr && active) component->setActive(false);
			if (processor != nullptr) processor->release();
			if (component != nullptr) { if (initialized) component->terminate(); component->release(); }
			if (events != nullptr) events->release();
			delete parameterChanges;
			if (host != nullptr) host->release();
			if (factory != nullptr) factory->release();
			if (module != nullptr && moduleInitialized)
			{
				auto exitDll = reinterpret_cast<bool (PLUGIN_API *)()>(GetProcAddress(module, "ExitDll"));
				if (exitDll != nullptr) exitDll();
				moduleInitialized = false;
			}
			if (module != nullptr) FreeLibrary(module);
		}

		bool initialize(const PluginLoadRequest &request) override
		{
			isEffect = request.effectMode;
			pluginName = (request.name != nullptr) ? request.name : "";

			std::string modulePath = (request.modulePath != nullptr) ? request.modulePath : "";
			module = LoadLibraryExW(pathFromUtf8(modulePath).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (module == nullptr)
			{
				const DWORD error = GetLastError();
				setLoadError("Could not load module");
				return false;
			}

			// optional module entry point. SDK hosts call this before touching the factory;
			// plug-ins like Serum 2 build up globals their editor needs in here
			auto initDll = reinterpret_cast<bool (PLUGIN_API *)()>(GetProcAddress(module, "InitDll"));
			if (initDll != nullptr)
			{
				if (!initDll())
				{
					setLoadError("Plugin module initialization failed");
					return false;
				}
				moduleInitialized = true;
			}

			auto getFactory = reinterpret_cast<GetFactoryProc>(GetProcAddress(module, "GetPluginFactory"));
			if (getFactory == nullptr)
			{
				setLoadError("GetPluginFactory export is missing");
				return false;
			}

			if ((factory = getFactory()) == nullptr)
			{
				setLoadError("Plugin factory creation failed");
				return false;
			}

			{
				PFactoryInfo factoryInfo = {};
				factory->getFactoryInfo(&factoryInfo);
			}

			auto fromHex = [](const char *hex, TUID out)
			{
				for (int i = 0; i < 16; ++i)
				{
					unsigned int byte = 0;
					if (std::sscanf(hex + i * 2, "%2x", &byte) != 1) return false;
					out[i] = static_cast<char>(byte);
				}
				return true;
			};
			TUID cid{};
			if (!fromHex((request.classID != nullptr) ? request.classID : "", cid))
			{
				setLoadError("Invalid plugin class ID");
				return false;
			}

			host = new (std::nothrow) BasicHostApplication();
			if (host == nullptr)
			{
				setLoadError("Out of memory creating host application");
				return false;
			}

			tresult createRes = factory->createInstance(cid, IComponent::iid,
			                                            reinterpret_cast<void **>(&component));
			if (createRes != kResultOk || component == nullptr)
			{
				setLoadError("Could not create plugin component");
				return false;
			}
			initialized = true;

			tresult initRes = component->initialize(host);
			if (initRes != kResultOk)
			{
				setLoadError("Plugin component initialization failed");
				return false;
			}

			tresult qiProcRes = component->queryInterface(IAudioProcessor::iid,
			                                              reinterpret_cast<void **>(&processor));
			if (qiProcRes != kResultOk || processor == nullptr)
			{
				setLoadError("Plugin does not provide an audio processor");
				return false;
			}

			const int32 outBuses = component->getBusCount(kAudio, kOutput);
			if (outBuses < 1)
			{
				setLoadError("Plugin has no audio output buses");
				return false;
			}

			BusInfo busInfo{};
			tresult busInfoRes = component->getBusInfo(kAudio, kOutput, 0, busInfo);
			if (busInfoRes != kResultOk || busInfo.channelCount < 1)
			{
				setLoadError("Plugin output bus info is invalid");
				return false;
			}

			if (busInfo.channelCount > 2)
			{
				setLoadError("Plugin output has more than 2 channels");
				return false;
			}
			outputChannelCount = busInfo.channelCount;

			if (request.effectMode)
			{
				const int32 inBuses = component->getBusCount(kAudio, kInput);
				if (inBuses < 1)
				{
					setLoadError("Plugin has no audio input buses");
					return false;
				}

				BusInfo inBusInfo{};
				tresult inBusInfoRes = component->getBusInfo(kAudio, kInput, 0, inBusInfo);
				if (inBusInfoRes != kResultOk || inBusInfo.channelCount < 1)
				{
					setLoadError("Plugin input bus info is invalid");
					return false;
				}

				if (inBusInfo.channelCount > 2)
				{
					setLoadError("Plugin input has more than 2 channels");
					return false;
				}
				inputChannelCount = inBusInfo.channelCount;
			}

			ProcessSetup setup{};
			setup.processMode = kRealtime;
			setup.symbolicSampleSize = kSample32;
			setup.maxSamplesPerBlock = MAX_WAV_RENDER_SAMPLES_PER_TICK;
			setup.sampleRate = audio.freq > 0 ? audio.freq : 44100.0;
			tresult setupRes = processor->setupProcessing(setup);
			if (setupRes != kResultOk)
			{
				setLoadError("Plugin setup processing failed");
				return false;
			}

			tresult activateOutRes = component->activateBus(kAudio, kOutput, 0, true);
			if (activateOutRes != kResultOk)
			{
				setLoadError("Could not activate audio output bus");
				return false;
			}

			if (request.effectMode)
			{
				tresult activateInRes = component->activateBus(kAudio, kInput, 0, true);
				if (activateInRes != kResultOk)
				{
					setLoadError("Could not activate audio input bus");
					return false;
				}
			}

			const int32 eventInputs = component->getBusCount(kEvent, kInput);
			hasEventBus = eventInputs > 0;
			if (eventInputs > 0)
			{
				tresult activateEvRes = component->activateBus(kEvent, kInput, 0, true);
				if (activateEvRes != kResultOk)
				{
					setLoadError("Could not activate event input bus");
					return false;
				}
			}

			tresult setActiveRes = component->setActive(true);
			if (setActiveRes != kResultOk)
			{
				setLoadError("Could not activate plugin component");
				return false;
			}
			active = true;
			updateLatency();

			tresult setProcRes = processor->setProcessing(true);
			if (setProcRes != kResultOk)
			{
				setLoadError("Could not start plugin processing");
				return false;
			}
			processing = true;

			outputL.resize(MAX_WAV_RENDER_SAMPLES_PER_TICK);
			outputR.resize(MAX_WAV_RENDER_SAMPLES_PER_TICK);
			outputChannels = { outputL.data(), outputR.data() };
			outputBus.numChannels = outputChannelCount;
			outputBus.channelBuffers32 = outputChannels.data();

			if (request.effectMode)
			{
				inputL.resize(MAX_WAV_RENDER_SAMPLES_PER_TICK);
				inputR.resize(MAX_WAV_RENDER_SAMPLES_PER_TICK);
				inputChannels = { inputL.data(), inputR.data() };
				inputBus.numChannels = inputChannelCount;
				inputBus.channelBuffers32 = inputChannels.data();
			}
			events = new (std::nothrow) HostEventList();
			if (events == nullptr)
			{
				setLoadError("Out of memory creating event list");
				return false;
			}
			parameterChanges = new (std::nothrow) HostParameterChanges();
			if (parameterChanges == nullptr)
			{
				setLoadError("Out of memory creating parameter changes");
				return false;
			}
			return true;
		}

		void closeEditor() override
		{
			if (editorView != nullptr)
			{
				if (editorAttached)
					editorView->removed();
				editorView->setFrame(nullptr);
				editorAttached = false;
				editorView->release();
				editorView = nullptr;
			}

			if (editorWindow != nullptr)
			{
				DestroyWindow(editorWindow);
				editorWindow = nullptr;
			}

			if (componentConnection != nullptr)
			{
				if (controllerConnection != nullptr)
				{
					controllerConnection->disconnect(componentConnection);
					componentConnection->disconnect(controllerConnection);
				}
				componentConnection->release();
				componentConnection = nullptr;
			}
			if (controllerConnection != nullptr)
			{
				controllerConnection->release();
				controllerConnection = nullptr;
			}
			if (controller != nullptr)
			{
				// drop the host callback before the controller goes away, so the plug-in
				// can't call back into a handler that outlives it in a broken state
				if (componentHandler != nullptr)
					componentHandler->setTargets(nullptr, nullptr);
				controller->setComponentHandler(nullptr);
				if (controllerInitialized)
					controller->terminate();
				controller->release();
				controller = nullptr;
				controllerInitialized = false;
			}
			if (editorFrame != nullptr)
			{
				editorFrame->release();
				editorFrame = nullptr;
			}
		}

		// creates the plug-in's edit controller on demand (the editor and state
		// restore both need it). Combined component/controller plug-ins provide it
		// through queryInterface.
		bool ensureController()
		{
			if (controller != nullptr)
				return true;

			if (component == nullptr || factory == nullptr || host == nullptr)
				return false;

			if (component->queryInterface(IEditController::iid, reinterpret_cast<void **>(&controller)) == kResultOk &&
				controller != nullptr)
			{
				return true;
			}
			controller = nullptr;

			TUID controllerCID{};
			component->getControllerClassId(controllerCID);

			if (factory->createInstance(controllerCID, IEditController::iid,
				reinterpret_cast<void **>(&controller)) != kResultOk || controller == nullptr)
			{
				controller = nullptr;
				return false;
			}

			if (controller->initialize(host) != kResultOk)
			{
				controller->release();
				controller = nullptr;
				return false;
			}
			controllerInitialized = true;
			return true;
		}

		// Captures the plug-in state (the preset) into a byte vector. Must not be
		// called from the audio thread.
		bool captureState(std::vector<uint8_t> &state) override
		{
			if (component == nullptr)
				return false;

			MemoryStream stream;
			if (component->getState(&stream) != kResultOk)
				return false;

			state = std::move(stream.data);
			return true;
		}

		// Restores a captured preset into the plug-in's processing state.
		// IEditController::setComponentState() is not called afterwards: it takes
		// OB-Xd 3 down with an access violation (whichever state is passed, the
		// stored one or a freshly captured one), while the component state alone
		// restores the preset exactly (it round-trips unchanged).
		bool applyState(const void *stateData, uint32_t stateSize) override
		{
			if (component == nullptr || stateData == nullptr || stateSize == 0)
				return false;

			MemoryStream stream(stateData, stateSize);
			return component->setState(&stream) == kResultOk;
		}

		bool openEditor(void) override
		{
			if (editorWindow != nullptr)
			{
				ShowWindow(editorWindow, SW_SHOW);
				SetForegroundWindow(editorWindow);
				return true;
			}

			if (component == nullptr || host == nullptr)
				return false;

			if (!ensureController())
			{
				closeEditor();
				return false;
			}

			// plug-ins expect a component handler before their editor is created
			if (componentHandler == nullptr)
				componentHandler = new (std::nothrow) ComponentHandler();
			if (componentHandler == nullptr)
			{
				closeEditor();
				return false;
			}
			componentHandler->setTargets(controller, parameterChanges);
			controller->setComponentHandler(componentHandler);

			tresult qiCompConnRes = component->queryInterface(IConnectionPoint::iid,
			                                                  reinterpret_cast<void **>(&componentConnection));
			tresult qiCtrlConnRes = controller->queryInterface(IConnectionPoint::iid,
			                                                   reinterpret_cast<void **>(&controllerConnection));

			if (componentConnection != nullptr && controllerConnection != nullptr)
			{
				componentConnection->connect(controllerConnection);
				controllerConnection->connect(componentConnection);
			}

			editorView = controller->createView(ViewType::kEditor);
			if (editorView == nullptr)
			{
				closeEditor();
				return false;
			}

			tresult platformRes = editorView->isPlatformTypeSupported(kPlatformTypeHWND);
			if (platformRes != kResultTrue)
			{
				closeEditor();
				return false;
			}

			ViewRect viewRect{};
			tresult getSizeRes = editorView->getSize(&viewRect);
			if (getSizeRes != kResultOk || viewRect.getWidth() < 1 || viewRect.getHeight() < 1)
			{
				closeEditor();
				return false;
			}

			static bool classRegistered = false;
			if (!classRegistered)
			{
				WNDCLASSEXW wc{};
				wc.cbSize = sizeof(wc);
				wc.lpfnWndProc = vst3EditorWindowProc;
				wc.hInstance = GetModuleHandleW(nullptr);
				wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
				wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
				wc.lpszClassName = kEditorWindowClass;
				classRegistered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
			}
			if (!classRegistered)
			{
				closeEditor();
				return false;
			}

			wchar_t title[192];
			MultiByteToWideChar(CP_UTF8, 0, pluginName.c_str(), -1, title, static_cast<int>(std::size(title)));
			if (title[0] == L'\0')
				wcscpy_s(title, L"Plug-in Editor");
			else
			{
				const size_t titleLen = wcslen(title);
				if (titleLen + 18 < std::size(title))
					wcscat_s(title, L" - FT2 Clone");
			}

			RECT windowRect{ 0, 0, viewRect.getWidth(), viewRect.getHeight() };
			AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, FALSE, 0);
			editorFrame = new (std::nothrow) PluginEditorFrame(this);
			if (editorFrame == nullptr)
			{
				closeEditor();
				return false;
			}

			editorWindow = CreateWindowExW(0, kEditorWindowClass, title,
				WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
				CW_USEDEFAULT, CW_USEDEFAULT, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
				video.hWnd, nullptr, GetModuleHandleW(nullptr), this);
			if (editorWindow == nullptr)
			{
				closeEditor();
				return false;
			}

			tresult setFrameRes = editorView->setFrame(editorFrame);
			tresult attachedRes = editorView->attached(editorWindow, kPlatformTypeHWND);
			if (setFrameRes != kResultOk || attachedRes != kResultOk)
			{
				closeEditor();
				return false;
			}
			editorAttached = true;

			ShowWindow(editorWindow, SW_SHOW);
			UpdateWindow(editorWindow);
			SetForegroundWindow(editorWindow);
			return true;
		}

		void resizeEditor(const ViewRect &newSize)
		{
			if (editorWindow == nullptr || editorView == nullptr || newSize.getWidth() < 1 || newSize.getHeight() < 1)
				return;

			RECT rect{ 0, 0, newSize.getWidth(), newSize.getHeight() };
			AdjustWindowRectEx(&rect, static_cast<DWORD>(GetWindowLongPtrW(editorWindow, GWL_STYLE)), FALSE,
				static_cast<DWORD>(GetWindowLongPtrW(editorWindow, GWL_EXSTYLE)));
			SetWindowPos(editorWindow, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		}

		// starts a note and returns the id the matching note-off has to use
		int32_t noteOn(uint8_t pitch, float velocity) override
		{
			if (events == nullptr || events->getEventCount() >= HostEventList::MAX_EVENTS)
				return -1;

			if (nextNoteId <= 0) // ids must stay positive
				nextNoteId = 1;
			const int32_t noteId = nextNoteId++;

			Event event{};
			event.busIndex = 0;
			event.type = Event::kNoteOnEvent;
			event.noteOn.channel = 0;
			event.noteOn.pitch = pitch;
			event.noteOn.velocity = velocity;
			event.noteOn.noteId = noteId;
			if (events->addEvent(event) != kResultOk)
				return -1;

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

		void noteOff(uint8_t pitch, int32_t noteId) override
		{
			if (noteId <= 0 || events == nullptr)
				return;

			// find the voice this note-off belongs to, so that it doesn't release a
			// note of a different channel that happens to have the same pitch
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

			Event event{};
			event.busIndex = 0;
			event.type = Event::kNoteOffEvent;
			event.noteOff.channel = 0;
			event.noteOff.pitch = pitch;
			event.noteOff.velocity = 0.0f;
			event.noteOff.noteId = noteId;
			events->addEvent(event);
		}

		// queues a note-off for every note this instance is still holding, so that a
		// reassignment or a panic doesn't leave notes hanging inside the plugin
		void allNotesOff() override
		{
			for (auto &held : heldNotes)
			{
				if (held.id == 0)
					continue;

				const int32_t noteId = held.id;
				const int16_t pitch = held.pitch;
				held.id = 0;

				if (events == nullptr)
					continue;

				Event event{};
				event.busIndex = 0;
				event.type = Event::kNoteOffEvent;
				event.noteOff.channel = 0;
				event.noteOff.pitch = pitch;
				event.noteOff.velocity = 0.0f;
				event.noteOff.noteId = noteId;
				events->addEvent(event);
			}
		}

		// Fetches the plug-in latency (in samples). Must be called after setActive(true),
		// which is also what the VST3 spec expects.
		void updateLatency()
		{
			latencySamples = 0;
			if (processor == nullptr)
				return;

			const uint32 latency = processor->getLatencySamples();
			latencySamples = (latency > (uint32)MAX_PDC_LATENCY_SAMPLES) ?
				MAX_PDC_LATENCY_SAMPLES : (int32_t)latency;
		}

		void setSampleRate(float sampleRate) override
		{
			if (processor == nullptr || sampleRate <= 0.0f) return;

			// setupProcessing must be called while the component is inactive
			if (processing)
			{
				processor->setProcessing(false);
				processing = false;
			}
			if (component != nullptr && active)
			{
				component->setActive(false);
				active = false;
			}

			ProcessSetup setup{};
			setup.processMode = kRealtime;
			setup.symbolicSampleSize = kSample32;
			setup.maxSamplesPerBlock = MAX_WAV_RENDER_SAMPLES_PER_TICK;
			setup.sampleRate = sampleRate;
			if (processor->setupProcessing(setup) != kResultOk)
				setLoadError("Could not change the plugin's sample rate");

			if (component != nullptr)
			{
				component->setActive(true);
				active = true;
			}
			processor->setProcessing(true);
			processing = true;

			updateLatency();
		}

		// renders one block into outputL/outputR and writes it to the scope buffer.
		// Mixing into the output bus happens in the caller, after the block has been
		// run through the latency-compensation delay line.
		bool process(uint32_t numSamples, float *scopeBuffer, std::atomic<uint32_t> &scopePosition,
		             const PluginHostTransport &transport) override
		{
			if (!processing || numSamples == 0 || numSamples > outputL.size()) return false;
			std::fill(outputL.begin(), outputL.begin() + numSamples, 0.0f);
			std::fill(outputR.begin(), outputR.begin() + numSamples, 0.0f);
			ProcessData data{};
			data.processMode = kRealtime;
			data.symbolicSampleSize = kSample32;
			data.numSamples = static_cast<int32>(numSamples);
			data.numInputs = 0;
			data.inputs = nullptr;
			data.numOutputs = 1;
			data.outputs = &outputBus;
			data.inputEvents = hasEventBus ? events : nullptr;
			data.inputParameterChanges = parameterChanges;
			// tempo/time-signature/musical position, needed by tempo-synced plug-in
			// features such as arpeggiators, LFOs and delays
			processContext = processContextFromTransport(transport);
			data.processContext = &processContext;
			if (hasEventBus && events != nullptr)
				events->prepareForDelivery();
			if (parameterChanges != nullptr)
				parameterChanges->beginBlock();
			processor->process(data);
			if (events != nullptr)
				events->clear();

			const uint32_t scopeMask = VST3_SCOPE_BUFFER_SIZE - 1;
			uint32_t scopePos = scopePosition.load(std::memory_order_relaxed);

			for (uint32 i = 0; i < numSamples; ++i)
			{
				const float l = std::clamp(outputL[i], -8.0f, 8.0f);
				const float r = std::clamp(outputChannelCount > 1 ? outputR[i] : outputL[i], -8.0f, 8.0f);

				scopeBuffer[scopePos & scopeMask] = (l + r) * 0.5f;
				scopePos++;
			}

			scopePosition.store(scopePos, std::memory_order_relaxed);
			return true;
		}

		// effect processing: the mix buffer is fed in, and the plug-in output
		// replaces it, so a chain of instances processes the signal in series
		void processEffect(uint32_t numSamples, float *left, float *right, const PluginHostTransport &transport) override
		{
			if (!isEffect || !processing || numSamples == 0 || numSamples > outputL.size() ||
				left == nullptr || right == nullptr)
				return;

			if (inputChannelCount > 1)
			{
				std::copy(left, left + numSamples, inputL.begin());
				std::copy(right, right + numSamples, inputR.begin());
			}
			else
			{
				for (uint32 i = 0; i < numSamples; ++i)
					inputL[i] = (left[i] + right[i]) * 0.5f;
			}

			std::fill(outputL.begin(), outputL.begin() + numSamples, 0.0f);
			std::fill(outputR.begin(), outputR.begin() + numSamples, 0.0f);

			ProcessData data{};
			data.processMode = kRealtime;
			data.symbolicSampleSize = kSample32;
			data.numSamples = static_cast<int32>(numSamples);
			data.numInputs = 1;
			data.inputs = &inputBus;
			data.numOutputs = 1;
			data.outputs = &outputBus;
			data.inputEvents = hasEventBus ? events : nullptr;
			data.inputParameterChanges = parameterChanges;
			processContext = processContextFromTransport(transport);
			data.processContext = &processContext;
			if (hasEventBus && events != nullptr)
				events->prepareForDelivery();
			if (parameterChanges != nullptr)
				parameterChanges->beginBlock();
			processor->process(data);
			if (events != nullptr)
				events->clear();

			if (outputChannelCount > 1)
			{
				std::copy(outputL.begin(), outputL.begin() + numSamples, left);
				std::copy(outputR.begin(), outputR.begin() + numSamples, right);
			}
			else
			{
				for (uint32 i = 0; i < numSamples; ++i)
					left[i] = right[i] = outputL[i];
			}
		}
	};

	tresult PLUGIN_API PluginEditorFrame::resizeView(IPlugView *view, ViewRect *newSize)
	{
		if (owner == nullptr || view != owner->editorView || newSize == nullptr)
			return kInvalidArgument;
		owner->resizeEditor(*newSize);
		return kResultOk;
	}

	LRESULT CALLBACK vst3EditorWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		PluginInstance *instance = reinterpret_cast<PluginInstance *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		if (message == WM_NCCREATE)
		{
			const CREATESTRUCTW *createInfo = reinterpret_cast<const CREATESTRUCTW *>(lParam);
			instance = static_cast<PluginInstance *>(createInfo->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(instance));
			if (instance != nullptr)
				instance->editorWindow = hwnd;
		}

		if (message == WM_CLOSE)
		{
			ShowWindow(hwnd, SW_HIDE);
			return 0;
		}
		if (message == WM_SIZE && instance != nullptr && instance->editorAttached && instance->editorView != nullptr)
		{
			ViewRect rect(0, 0, LOWORD(lParam), HIWORD(lParam));
			if (rect.getWidth() > 0 && rect.getHeight() > 0)
				instance->editorView->onSize(&rect);
		}
		if (message == WM_NCDESTROY && instance != nullptr && instance->editorWindow == hwnd)
			instance->editorWindow = nullptr;

		// The frame itself has no keyboard input: the plug-in's view is a child
		// window and receives the keys it wants directly. Passing the key and
		// character messages on to DefWindowProcW makes Windows play its default
		// beep on every key press while this window has focus, which makes keyboard
		// jamming impossible. WM_SYSKEYDOWN stays untouched so Alt+F4 still works.
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

	// creates the host instance of a scanned plug-in, in the format it was found in
	std::unique_ptr<PluginHostInstance> createPluginInstance(const vst3PluginInfo_t &info, bool effectMode)
	{
		setLoadError(nullptr);

		std::unique_ptr<PluginHostInstance> instance;
		if (info.format == VST3_PLUGIN_FORMAT_VST2)
			instance.reset(new (std::nothrow) Vst2Instance());
		else
			instance.reset(new (std::nothrow) PluginInstance());

		if (instance == nullptr)
			return nullptr;

		PluginLoadRequest request{};
		request.name = info.name;
		request.vendor = info.vendor;
		request.classID = info.classID;
		request.modulePath = info.modulePath;
		request.kind = info.kind;
		request.effectMode = effectMode;
		request.sampleRate = (audio.freq > 0) ? (float)audio.freq : 44100.0f;

		bool loaded = false;
		try
		{
			loaded = instance->initialize(request);
		}
		catch (...)
		{
			return nullptr;
		}

		if (!loaded)
		{
			// a VST2 host reports the reason through the instance, VST3 through setLoadError()
			const char *reason = instance->lastError();
			if (reason != nullptr && reason[0] != '\0')
				setLoadError(reason);
			return nullptr;
		}

		return instance;
	}

	std::array<std::unique_ptr<PluginHostInstance>, MAX_VST3_INSTRUMENTS> instrumentInstances;

	// .xmv plug-in references/presets parsed by the module loading thread. They
	// are applied later, on the main thread (see vst3ApplySongVstChunk()).
	struct PendingSongInstrument
	{
		uint8_t instrument;
		vst3PluginInfo_t info;
		std::vector<uint8_t> state;
	};
	std::mutex pendingSongMutex;
	std::vector<PendingSongInstrument> pendingSongInstruments;
	bool pendingSongChunkValid = false;

	// latency compensation state (see StereoDelayLine above)
	StereoDelayLine dryDelay;
	std::array<StereoDelayLine, MAX_VST3_INSTRUMENTS> instrumentDelays;

	// lines the dry tracker mix and every loaded instrument up on the same sample
	// position (must be called with the audio locked, or while audio is paused)
	void updateLatencyCompensation()
	{
		int32_t maxLatency = 0;
		for (const auto &instance : instrumentInstances)
		{
			if (instance != nullptr && instance->latencySamples > maxLatency)
				maxLatency = instance->latencySamples;
		}

		dryDelay.resize((size_t)maxLatency);
		for (int32_t i = 0; i < MAX_VST3_INSTRUMENTS; ++i)
		{
			const int32_t latency = (instrumentInstances[i] != nullptr) ? instrumentInstances[i]->latencySamples : 0;
			instrumentDelays[i].resize((size_t)(maxLatency - latency));
		}
	}

	void resetLatencyCompensationBuffers()
	{
		dryDelay.clear();
		for (auto &delay : instrumentDelays)
			delay.clear();
	}

	// master effect chain, processed in series on the final stereo mix
	struct MasterEffect
	{
		int32_t pluginIndex = -1;
		bool bypassed = false;
		std::unique_ptr<PluginHostInstance> instance;
	};
	std::array<MasterEffect, MAX_MASTER_EFFECTS> masterEffects;
	int32_t masterEffectCount = 0;

	// host transport, advanced once per processed block and handed to every instance.
	// plug-ins derive their tempo-synced timing (arpeggiators, LFOs, delays) from this.
	int64 hostTransportSamples = 0;
	double hostTransportTimeMusic = 0.0;
	bool hostTransportWasPlaying = false;

	PluginHostTransport makeTransportContext(void)
	{
		PluginHostTransport transport{};
		transport.playing = songPlaying && !musicPaused;
		transport.sampleRate = (audio.freq > 0) ? (double)audio.freq : 44100.0;
		transport.tempo = (song.BPM >= MIN_BPM && song.BPM <= MAX_BPM) ? (double)song.BPM : 125.0;
		transport.projectTimeSamples = hostTransportSamples;
		transport.projectTimeMusic = hostTransportTimeMusic;
		return transport;
	}
#endif

	// mono ring buffer per instrument slot, read by the channel scopes
	std::array<std::array<float, VST3_SCOPE_BUFFER_SIZE>, MAX_VST3_INSTRUMENTS> scopeBuffers;
	std::array<std::atomic<uint32_t>, MAX_VST3_INSTRUMENTS> scopeBufferPositions;
	struct ScopeBufferDefaults { ScopeBufferDefaults() { for (auto &pos : scopeBufferPositions) pos.store(0); } } scopeBufferDefaults;
	std::atomic<int32_t> renderSoloInstrument{ VST3_RENDER_ALL };

	void copyText(char *dst, size_t dstSize, const char *src)
	{
		if (dstSize == 0)
			return;

		if (src == nullptr)
			src = "";

		std::snprintf(dst, dstSize, "%s", src);
	}

	std::string pathToUtf8(const std::filesystem::path &path)
	{
		const std::wstring wide = path.wstring();
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

	std::string uidToHex(const Steinberg::TUID uid)
	{
		static const char hex[] = "0123456789ABCDEF";
		std::string result(32, '0');
		for (size_t i = 0; i < 16; ++i)
		{
			const uint8_t b = static_cast<uint8_t>(uid[i]);
			result[i * 2 + 0] = hex[b >> 4];
			result[i * 2 + 1] = hex[b & 0x0F];
		}
		return result;
	}

	void setScanProgress(uint32_t completed, uint32_t total, const char *currentModulePath)
	{
		std::lock_guard<std::mutex> lock(progressMutex);
		scanProgress.completedModules = completed;
		scanProgress.totalModules = total;
		copyText(scanProgress.currentModulePath, sizeof(scanProgress.currentModulePath), currentModulePath);
	}

	std::string fileNameFromPath(const std::string &path)
	{
		const size_t pos = path.find_last_of("/\\");
		return (pos == std::string::npos) ? path : path.substr(pos + 1);
	}

	// Adds one line to the scan report: something that was found but left out of
	// the plug-in list. The skipped count in the status line follows the report.
	void addSkippedPlugin(const char *name, const std::string &modulePath, const char *reason)
	{
		vst3SkippedPlugin_t entry = {};
		if (name != nullptr && name[0] != '\0')
			copyText(entry.name, sizeof(entry.name), name);
		else
			copyText(entry.name, sizeof(entry.name), fileNameFromPath(modulePath).c_str());
		copyText(entry.reason, sizeof(entry.reason), reason);
		copyText(entry.modulePath, sizeof(entry.modulePath), modulePath.c_str());

		std::lock_guard<std::mutex> lock(progressMutex);
		if (skippedPlugins.size() < MAX_VST3_SKIPPED_PLUGINS)
			skippedPlugins.push_back(entry);
		scanProgress.skippedPlugins = static_cast<uint32_t>(skippedPlugins.size());
	}

	// merges the report of an isolated scanner process into ours
	void mergeSkippedPlugins(const std::vector<vst3SkippedPlugin_t> &entries)
	{
		std::lock_guard<std::mutex> lock(progressMutex);
		for (const auto &entry : entries)
		{
			if (skippedPlugins.size() >= MAX_VST3_SKIPPED_PLUGINS)
				break;
			skippedPlugins.push_back(entry);
		}
		scanProgress.skippedPlugins = static_cast<uint32_t>(skippedPlugins.size());
	}

#ifdef _WIN32
	// unloads a plug-in module, running its optional ExitDll entry point first so it
	// can tear down whatever InitDll set up
	void unloadModule(HMODULE module, bool moduleInitialized)
	{
		if (module == nullptr) return;
		if (moduleInitialized)
		{
			auto exitDll = reinterpret_cast<bool (PLUGIN_API *)()>(GetProcAddress(module, "ExitDll"));
			if (exitDll != nullptr) exitDll();
		}
		FreeLibrary(module);
	}

	// adds the plug-ins a VST2 module (a plain DLL) exposes
	void addVst2ModuleClasses(const std::filesystem::path &binaryPath)
	{
		const std::string modulePathUtf8 = pathToUtf8(binaryPath);

		std::vector<Vst2PluginEntry> entries;
		std::string error;
		if (!vst2EnumerateModule(modulePathUtf8.c_str(), entries, error))
		{
			if (!error.empty())
				lastError = error;
			addSkippedPlugin(nullptr, modulePathUtf8,
				error.empty() ? "Could not read the plug-in list" : error.c_str());
			return;
		}

		for (const auto &entry : entries)
		{
			vst3PluginInfo_t plugin = {};
			copyText(plugin.name, sizeof(plugin.name), entry.name);
			copyText(plugin.vendor, sizeof(plugin.vendor), entry.vendor);
			copyText(plugin.category, sizeof(plugin.category), entry.isInstrument ? "VST2 Instrument" : "VST2 Effect");
			copyText(plugin.subCategories, sizeof(plugin.subCategories), entry.isInstrument ? "Instrument" : "Fx");
			copyText(plugin.version, sizeof(plugin.version), entry.version);
			copyText(plugin.classID, sizeof(plugin.classID), entry.uniqueId);
			copyText(plugin.modulePath, sizeof(plugin.modulePath), modulePathUtf8.c_str());
			plugin.kind = entry.isInstrument ? VST3_PLUGIN_INSTRUMENT : VST3_PLUGIN_EFFECT;
			plugin.format = VST3_PLUGIN_FORMAT_VST2;
			plugins.push_back(plugin);
		}
	}

	void addVst3ModuleClasses(const std::filesystem::path &binaryPath);

	void addModuleClasses(const std::filesystem::path &binaryPath)
	{
		std::wstring extension = binaryPath.extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), std::towlower);

		if (extension == L".dll")
			addVst2ModuleClasses(binaryPath);
		else
			addVst3ModuleClasses(binaryPath);
	}

	void addVst3ModuleClasses(const std::filesystem::path &binaryPath)
	{
		const std::string modulePathUtf8 = pathToUtf8(binaryPath);

		HMODULE module = LoadLibraryExW(binaryPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (module == nullptr)
		{
			const DWORD error = GetLastError();
			lastError = "Could not load " + modulePathUtf8 + " (Windows error " +
				std::to_string(error) + ")";
			FreeLibrary(module);
			return;
		}

		// optional module entry point, as SDK hosts call it before using the factory
		auto initDll = reinterpret_cast<bool (PLUGIN_API *)()>(GetProcAddress(module, "InitDll"));
		const bool moduleInitialized = initDll != nullptr && initDll();
		if (initDll != nullptr && !moduleInitialized)
		{
			lastError = "Plugin module initialization failed in " + modulePathUtf8;
			unloadModule(module, moduleInitialized);
			return;
		}

		auto getFactory = reinterpret_cast<GetFactoryProc>(GetProcAddress(module, "GetPluginFactory"));
		if (getFactory == nullptr)
		{
			lastError = "GetPluginFactory export is missing in " + modulePathUtf8;
			unloadModule(module, moduleInitialized);
			return;
		}

		Steinberg::IPluginFactory *factory = getFactory();
		if (factory == nullptr)
		{
			lastError = "Plugin factory initialization failed in " + modulePathUtf8;
			unloadModule(module, moduleInitialized);
			return;
		}

		Steinberg::PFactoryInfo factoryInfo = {};
		factory->getFactoryInfo(&factoryInfo);

		Steinberg::IPluginFactory2 *factory2 = nullptr;
		tresult qiFactory2Res = factory->queryInterface(Steinberg::IPluginFactory2_iid,
			reinterpret_cast<void **>(&factory2));
		const bool hasFactory2 = qiFactory2Res == kResultOk;

		const int32_t count = factory->countClasses();

		int32_t added = 0;
		for (int32_t i = 0; i < count; ++i)
		{
			Steinberg::PClassInfo info = {};
			tresult classInfoRes = factory->getClassInfo(i, &info);
			if (classInfoRes != Steinberg::kResultOk)
				continue;

			Steinberg::PClassInfo2 info2 = {};
			tresult classInfo2Res = Steinberg::kResultOk;
			const bool hasInfo2 = hasFactory2 && (classInfo2Res = factory2->getClassInfo2(i, &info2)) == Steinberg::kResultOk;

			const char *category = hasInfo2 ? info2.category : info.category;
			if (std::strcmp(category, "Audio Module Class") != 0)
				continue;

			vst3PluginInfo_t plugin = {};
			copyText(plugin.name, sizeof(plugin.name), hasInfo2 ? info2.name : info.name);
			copyText(plugin.vendor, sizeof(plugin.vendor), hasInfo2 && info2.vendor[0] != '\0' ? info2.vendor : factoryInfo.vendor);
			copyText(plugin.category, sizeof(plugin.category), category);
			if (hasInfo2)
			{
				copyText(plugin.subCategories, sizeof(plugin.subCategories), info2.subCategories);
				copyText(plugin.version, sizeof(plugin.version), info2.version);
			}
			copyText(plugin.classID, sizeof(plugin.classID), uidToHex(info.cid).c_str());
			copyText(plugin.modulePath, sizeof(plugin.modulePath), modulePathUtf8.c_str());

			if (std::strstr(plugin.subCategories, "Instrument") != nullptr)
				plugin.kind = VST3_PLUGIN_INSTRUMENT;
			else
				plugin.kind = VST3_PLUGIN_EFFECT;

			// broken modules often list classes they cannot create, so make sure the
			// class really instantiates before offering it. This runs in the isolated
			// scanner process, so even a crashing plug-in only loses itself.
			Steinberg::FUnknown *component = nullptr;
			if (factory->createInstance(info.cid, IComponent::iid,
				reinterpret_cast<void **>(&component)) != Steinberg::kResultOk || component == nullptr)
			{
				addSkippedPlugin(plugin.name, modulePathUtf8, "Could not create a plugin instance");
				continue;
			}
			component->release();

			plugins.push_back(plugin);
			added++;
		}

		if (factory2 != nullptr)
			factory2->release();
		factory->release();
		unloadModule(module, moduleInitialized);
	}

	bool writeExact(HANDLE file, const void *data, DWORD size)
	{
		DWORD written = 0;
		return WriteFile(file, data, size, &written, nullptr) != FALSE && written == size;
	}

	bool readExact(HANDLE file, void *data, DWORD size)
	{
		DWORD read = 0;
		return ReadFile(file, data, size, &read, nullptr) != FALSE && read == size;
	}

	void setScanErrorForModule(std::string &error, const std::filesystem::path &path, const char *reason)
	{
		if (error.empty())
			error = std::string(reason) + ": " + pathToUtf8(path);
	}

	bool scanModuleInIsolatedProcess(const std::filesystem::path &modulePath,
		std::vector<vst3PluginInfo_t> &scannedPlugins, std::string &error)
	{
		const std::string modulePathUtf8 = pathToUtf8(modulePath);

		wchar_t tempDirectory[MAX_PATH], resultPath[MAX_PATH];
		if (GetTempPathW(MAX_PATH, tempDirectory) == 0 ||
			GetTempFileNameW(tempDirectory, L"v3s", 0, resultPath) == 0)
		{
			setScanErrorForModule(error, modulePath, "Could not create scanner result file");
			addSkippedPlugin(nullptr, modulePathUtf8, "Could not create scanner result file");
			return false;
		}

		const std::wstring moduleEnvName = L"FT2CLONE_VST3_SCAN_MODULE";
		const std::wstring resultEnvName = L"FT2CLONE_VST3_SCAN_RESULT";
		const bool envModuleSet = SetEnvironmentVariableW(moduleEnvName.c_str(), modulePath.c_str()) != FALSE;
		const bool envResultSet = SetEnvironmentVariableW(resultEnvName.c_str(), resultPath) != FALSE;
		if (!envModuleSet || !envResultSet)
		{
			SetEnvironmentVariableW(moduleEnvName.c_str(), nullptr);
			SetEnvironmentVariableW(resultEnvName.c_str(), nullptr);
			DeleteFileW(resultPath);
			setScanErrorForModule(error, modulePath, "Could not configure isolated scanner");
			addSkippedPlugin(nullptr, modulePathUtf8, "Could not configure isolated scanner");
			return false;
		}

		std::vector<wchar_t> executablePath(32768);
		const DWORD exeLen = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
		if (exeLen == 0 || exeLen >= executablePath.size())
		{
			SetEnvironmentVariableW(moduleEnvName.c_str(), nullptr);
			SetEnvironmentVariableW(resultEnvName.c_str(), nullptr);
			DeleteFileW(resultPath);
			setScanErrorForModule(error, modulePath, "Could not locate scanner executable");
			addSkippedPlugin(nullptr, modulePathUtf8, "Could not locate scanner executable");
			return false;
		}

		std::wstring commandLine = L"\"" + std::wstring(executablePath.data(), exeLen) + L"\" --ft2-vst3-scan-worker";
		std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
		commandBuffer.push_back(L'\0');

		STARTUPINFOW startupInfo = {};
		startupInfo.cb = sizeof(startupInfo);
		PROCESS_INFORMATION processInfo = {};
		const BOOL created = CreateProcessW(executablePath.data(), commandBuffer.data(), nullptr, nullptr,
			FALSE, CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE, nullptr, nullptr, &startupInfo, &processInfo);
		const DWORD createError = created ? ERROR_SUCCESS : GetLastError();
		SetEnvironmentVariableW(moduleEnvName.c_str(), nullptr);
		SetEnvironmentVariableW(resultEnvName.c_str(), nullptr);

		if (!created)
		{
			DeleteFileW(resultPath);
			setScanErrorForModule(error, modulePath, "Could not start isolated scanner");
			addSkippedPlugin(nullptr, modulePathUtf8, "Could not start isolated scanner");
			return false;
		}

		DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 30000);
		if (waitResult == WAIT_TIMEOUT)
		{
			TerminateProcess(processInfo.hProcess, 0xDEAD);
			WaitForSingleObject(processInfo.hProcess, INFINITE);
			setScanErrorForModule(error, modulePath, "Plugin scan timed out");
			addSkippedPlugin(nullptr, modulePathUtf8, "Plugin scan timed out");
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			DeleteFileW(resultPath);
			return false;
		}

		DWORD exitCode = 1;
		GetExitCodeProcess(processInfo.hProcess, &exitCode);
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		if (waitResult != WAIT_OBJECT_0 || exitCode != 0)
		{
			const char *reason = (exitCode == 11) ? "Could not load the plugin module" :
				"Plugin crashed or failed in scanner worker";
			setScanErrorForModule(error, modulePath, reason);
			addSkippedPlugin(nullptr, modulePathUtf8, reason);
			DeleteFileW(resultPath);
			return false;
		}

		HANDLE resultFile = CreateFileW(resultPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (resultFile == INVALID_HANDLE_VALUE)
		{
			DeleteFileW(resultPath);
			setScanErrorForModule(error, modulePath, "Scanner worker returned no result");
			addSkippedPlugin(nullptr, modulePathUtf8, "Scanner worker returned no result");
			return false;
		}

		uint32_t count = 0, skipped = 0;
		if (!readExact(resultFile, &count, sizeof(count)) ||
			!readExact(resultFile, &skipped, sizeof(skipped)) ||
			count > 4096 || skipped > MAX_VST3_SKIPPED_PLUGINS)
		{
			CloseHandle(resultFile);
			DeleteFileW(resultPath);
			setScanErrorForModule(error, modulePath, "Scanner worker returned invalid metadata");
			addSkippedPlugin(nullptr, modulePathUtf8, "Scanner worker returned invalid metadata");
			return false;
		}

		std::vector<vst3PluginInfo_t> modulePlugins(count);
		std::vector<vst3SkippedPlugin_t> moduleSkipped(skipped);
		const DWORD dataSize = static_cast<DWORD>(count * sizeof(vst3PluginInfo_t));
		const DWORD skippedSize = static_cast<DWORD>(skipped * sizeof(vst3SkippedPlugin_t));
		const bool readOk = (dataSize == 0 || readExact(resultFile, modulePlugins.data(), dataSize)) &&
			(skippedSize == 0 || readExact(resultFile, moduleSkipped.data(), skippedSize));
		CloseHandle(resultFile);
		DeleteFileW(resultPath);
		if (!readOk)
		{
			setScanErrorForModule(error, modulePath, "Scanner worker returned truncated metadata");
			addSkippedPlugin(nullptr, modulePathUtf8, "Scanner worker returned truncated metadata");
			return false;
		}

		scannedPlugins.insert(scannedPlugins.end(), modulePlugins.begin(), modulePlugins.end());
		mergeSkippedPlugins(moduleSkipped);
		return true;
	}

	int runScannerWorker(void)
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

		wchar_t modulePath[32768], resultPath[32768];
		const DWORD moduleLen = GetEnvironmentVariableW(L"FT2CLONE_VST3_SCAN_MODULE", modulePath, 32768);
		const DWORD resultLen = GetEnvironmentVariableW(L"FT2CLONE_VST3_SCAN_RESULT", resultPath, 32768);
		if (moduleLen == 0 || moduleLen >= 32768 || resultLen == 0 || resultLen >= 32768)
			return 10;

		plugins.clear();
		lastError.clear();
		{
			std::lock_guard<std::mutex> lock(progressMutex);
			skippedPlugins.clear();
			scanProgress.skippedPlugins = 0;
		}
		addModuleClasses(std::filesystem::path(modulePath));
		if (!lastError.empty())
			return 11;

		HANDLE resultFile = CreateFileW(resultPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (resultFile == INVALID_HANDLE_VALUE)
			return 12;

		std::vector<vst3SkippedPlugin_t> skippedReport;
		{
			std::lock_guard<std::mutex> lock(progressMutex);
			skippedReport = skippedPlugins;
		}

		const uint32_t count = static_cast<uint32_t>(plugins.size());
		const uint32_t skipped = static_cast<uint32_t>(skippedReport.size());
		const DWORD dataSize = static_cast<DWORD>(count * sizeof(vst3PluginInfo_t));
		const DWORD skippedSize = static_cast<DWORD>(skipped * sizeof(vst3SkippedPlugin_t));
		const bool writeOk = writeExact(resultFile, &count, sizeof(count)) &&
			writeExact(resultFile, &skipped, sizeof(skipped)) &&
			(dataSize == 0 || writeExact(resultFile, plugins.data(), dataSize)) &&
			(skippedSize == 0 || writeExact(resultFile, skippedReport.data(), skippedSize));
		CloseHandle(resultFile);
		return writeOk ? 0 : 13;
	}

	// VST2 plug-ins are plain DLLs in a folder, and installers are free to put
	// them anywhere. Besides the usual folders, read the paths registered under
	// HKLM/HKCU\SOFTWARE\VST (the VST2 equivalent of the VST3 folder).
	void addVst2RegistryRoots(std::vector<std::filesystem::path> &roots)
	{
		static const wchar_t *keyNames[] = { L"SOFTWARE\\VST", L"SOFTWARE\\WOW6432Node\\VST" };
		static const HKEY rootKeys[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };

		for (const wchar_t *keyName : keyNames)
		{
			for (HKEY rootKey : rootKeys)
			{
				HKEY key = nullptr;
				if (RegOpenKeyExW(rootKey, keyName, 0, KEY_READ, &key) != ERROR_SUCCESS)
					continue;

				wchar_t value[32768];
				DWORD valueSize = sizeof(value);
				DWORD valueType = 0;
				if (RegQueryValueExW(key, L"VSTPluginsPath", nullptr, &valueType,
					reinterpret_cast<LPBYTE>(value), &valueSize) == ERROR_SUCCESS &&
					(valueType == REG_SZ || valueType == REG_EXPAND_SZ) && valueSize >= sizeof(wchar_t))
				{
					value[valueSize / sizeof(wchar_t) - 1] = L'\0';
					std::filesystem::path path(value);
					if (!path.empty())
						roots.push_back(path);
				}

				RegCloseKey(key);
			}
		}
	}

	void collectModules(const std::filesystem::path &root, std::vector<std::filesystem::path> &modulePaths)
	{
		std::error_code ec;
		if (!std::filesystem::is_directory(root, ec))
			return;

		for (std::filesystem::recursive_directory_iterator it(root,
			std::filesystem::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
		{
			if (ec)
			{
				ec.clear();
				continue;
			}

			if (!it->is_regular_file(ec))
				continue;

			std::wstring extension = it->path().extension().wstring();
			std::transform(extension.begin(), extension.end(), extension.begin(), std::towlower);

			const bool isVst3Bundle = extension == L".vst3";
			const bool isVst2Module = extension == L".dll";
			if (!isVst3Bundle && !isVst2Module)
				continue;

			// a VST3 bundle is a directory tree; only its 64-bit program counts
			if (isVst3Bundle)
			{
				std::wstring fullPath = it->path().wstring();
				std::transform(fullPath.begin(), fullPath.end(), fullPath.begin(), std::towlower);
				if (fullPath.find(L"\\contents\\x86_64-win\\") == std::wstring::npos)
					continue;
			}

			const auto &modulePath = it->path();
			if (std::find(modulePaths.begin(), modulePaths.end(), modulePath) == modulePaths.end())
				modulePaths.push_back(modulePath);
		}
	}

	void scanModules(const std::vector<std::filesystem::path> &modulePaths,
		std::vector<vst3PluginInfo_t> &scannedPlugins, std::string &error)
	{
		const uint32_t total = static_cast<uint32_t>(modulePaths.size());
		setScanProgress(0, total, nullptr);
		for (uint32_t i = 0; i < total; ++i)
		{
			const std::string modulePath = pathToUtf8(modulePaths[i]);
			setScanProgress(i, total, modulePath.c_str());
			// failures are added to the scan report by scanModuleInIsolatedProcess()
			scanModuleInIsolatedProcess(modulePaths[i], scannedPlugins, error);
			setScanProgress(i + 1, total, nullptr);
		}
	}
#endif
}

extern "C" void vst3ScannerWorkerMain(void)
{
#ifdef _WIN32
	runScannerWorker();
#endif
}

bool vst3AddSearchPath(const char *pathUTF8)
{
	if (pathUTF8 == nullptr || pathUTF8[0] == '\0')
		return false;

	std::lock_guard<std::mutex> lock(scanMutex);
	if (std::find(extraPaths.begin(), extraPaths.end(), pathUTF8) == extraPaths.end())
		extraPaths.emplace_back(pathUTF8);
	return true;
}

bool vst3ScanDirectory(const char *pathUTF8)
{
	if (pathUTF8 == nullptr || pathUTF8[0] == '\0')
		return false;

	std::lock_guard<std::mutex> runLock(scanRunMutex);
	{
		std::lock_guard<std::mutex> lock(scanMutex);
		plugins.clear();
		lastError.clear();
	}
	setScanProgress(0, 0, "Preparing scan...");

#ifdef _WIN32
	const std::filesystem::path root = pathFromUtf8(pathUTF8);
	if (root.empty() || !std::filesystem::is_directory(root))
	{
		std::lock_guard<std::mutex> lock(scanMutex);
		lastError = "VST3 scan directory does not exist";
		return false;
	}

	std::vector<std::filesystem::path> modulePaths;
	collectModules(root, modulePaths);

	std::vector<vst3PluginInfo_t> scannedPlugins;
	std::string scanError;
	scanModules(modulePaths, scannedPlugins, scanError);

	std::lock_guard<std::mutex> lock(scanMutex);
	plugins.swap(scannedPlugins);
	lastError.swap(scanError);
	return true;
#else
	std::lock_guard<std::mutex> lock(scanMutex);		lastError = "Plugin scanning is currently implemented for Windows builds only";		return false;
#endif
}


extern "C" bool vst3Scan(void)
{
	std::lock_guard<std::mutex> runLock(scanRunMutex);
	std::vector<std::string> searchPaths;
	{
		std::lock_guard<std::mutex> lock(scanMutex);
		plugins.clear();
		lastError.clear();
		searchPaths = extraPaths;
	}
	setScanProgress(0, 0, "Preparing scan...");

#ifdef _WIN32
	std::vector<std::filesystem::path> roots;
	auto addRoot = [&roots](const std::filesystem::path &root)
	{
		if (root.empty())
			return;
		if (std::find(roots.begin(), roots.end(), root) == roots.end())
			roots.push_back(root);
	};

	wchar_t programFilesBuffer[MAX_PATH];
	DWORD programFilesLength = GetEnvironmentVariableW(L"ProgramW6432", programFilesBuffer, MAX_PATH);
	if (programFilesLength == 0 || programFilesLength >= MAX_PATH)
		programFilesLength = GetEnvironmentVariableW(L"ProgramFiles", programFilesBuffer, MAX_PATH);
	if (programFilesLength > 0 && programFilesLength < MAX_PATH)
	{
		const std::filesystem::path programFiles(programFilesBuffer);
		addRoot(programFiles / L"Common Files" / L"VST3");
		addRoot(programFiles / L"Common Files" / L"VST2");
		addRoot(programFiles / L"VSTPlugins");
		addRoot(programFiles / L"Steinberg" / L"VSTPlugins");
	}

	wchar_t programFilesX86Buffer[MAX_PATH];
	const DWORD programFilesX86Length = GetEnvironmentVariableW(L"ProgramFiles(x86)", programFilesX86Buffer, MAX_PATH);
	if (programFilesX86Length > 0 && programFilesX86Length < MAX_PATH)
	{
		const std::filesystem::path programFilesX86(programFilesX86Buffer);
		addRoot(programFilesX86 / L"Common Files" / L"VST3");
		addRoot(programFilesX86 / L"Common Files" / L"VST2");
		addRoot(programFilesX86 / L"VSTPlugins");
		addRoot(programFilesX86 / L"Steinberg" / L"VSTPlugins");
	}

	std::vector<std::filesystem::path> registryRoots;
	addVst2RegistryRoots(registryRoots);
	for (const auto &registryRoot : registryRoots)
		addRoot(registryRoot);

	for (const auto &path : searchPaths)
		addRoot(pathFromUtf8(path));

	std::vector<std::filesystem::path> modulePaths;
	for (const auto &root : roots)
	{
		collectModules(root, modulePaths);
	}

	std::vector<vst3PluginInfo_t> scannedPlugins;
	std::string scanError;
	scanModules(modulePaths, scannedPlugins, scanError);

	{
		std::lock_guard<std::mutex> lock(scanMutex);
		plugins.swap(scannedPlugins);
		lastError.swap(scanError);
	}

	return true;
#else
	std::lock_guard<std::mutex> lock(scanMutex);		lastError = "Plugin scanning is currently implemented for Windows builds only";		return false;
#endif
}


uint32_t vst3GetPluginCount(void)
{
	std::lock_guard<std::mutex> lock(scanMutex);
	return static_cast<uint32_t>(plugins.size());
}

bool vst3GetPluginInfo(uint32_t index, vst3PluginInfo_t *info)
{
	if (info == nullptr)
		return false;

	std::lock_guard<std::mutex> lock(scanMutex);
	if (index >= plugins.size())
		return false;

	*info = plugins[index];
	return true;
}

bool vst3WritePluginCache(FILE *file)
{
	if (file == nullptr)
		return false;

	const uint8_t magic[8] = { 'F', 'T', '2', 'V', 'S', 'T', '3', 0 };
	const uint32_t version = 2;
	std::lock_guard<std::mutex> lock(scanMutex);
	const uint32_t count = static_cast<uint32_t>(plugins.size());
	const uint32_t recordSize = sizeof(vst3PluginInfo_t);
	if (fwrite(magic, sizeof(magic), 1, file) != 1 ||
		fwrite(&version, sizeof(version), 1, file) != 1 ||
		fwrite(&count, sizeof(count), 1, file) != 1 ||
		fwrite(&recordSize, sizeof(recordSize), 1, file) != 1)
		return false;

	return count == 0 || fwrite(plugins.data(), sizeof(vst3PluginInfo_t), count, file) == count;
}

bool vst3ReadPluginCache(FILE *file, uint32_t byteCount)
{
	if (file == nullptr || byteCount == 0)
	{
		std::lock_guard<std::mutex> lock(scanMutex);
		plugins.clear();
		return byteCount == 0;
	}

	const uint8_t expectedMagic[8] = { 'F', 'T', '2', 'V', 'S', 'T', '3', 0 };
	uint8_t magic[8];
	uint32_t version, count, recordSize;
	if (byteCount < sizeof(magic) + sizeof(version) + sizeof(count) + sizeof(recordSize) ||
		fread(magic, sizeof(magic), 1, file) != 1 ||
		fread(&version, sizeof(version), 1, file) != 1 ||
		fread(&count, sizeof(count), 1, file) != 1 ||
		fread(&recordSize, sizeof(recordSize), 1, file) != 1 ||
		memcmp(magic, expectedMagic, sizeof(magic)) != 0 ||
		(version != 1 && version != 2))
	{
		std::lock_guard<std::mutex> lock(scanMutex);
		plugins.clear();
		return false;
	}

	// version 1 was written before VST2 support and has no format field
	const uint32_t legacyRecordSize = sizeof(vst3PluginInfo_t) - sizeof(int32_t);
	if ((version == 1 && recordSize != legacyRecordSize) ||
		(version == 2 && recordSize != sizeof(vst3PluginInfo_t)) ||
		count > (byteCount - 20) / recordSize ||
		20 + (uint64_t)count * recordSize != byteCount)
	{
		std::lock_guard<std::mutex> lock(scanMutex);
		plugins.clear();
		return false;
	}

	std::vector<vst3PluginInfo_t> loadedPlugins(count);
	if (version == 1)
	{
		std::vector<uint8_t> rawRecords((size_t)count * recordSize);
		if (count > 0 && fread(rawRecords.data(), recordSize, count, file) != count)
		{
			std::lock_guard<std::mutex> lock(scanMutex);
			plugins.clear();
			return false;
		}

		for (uint32_t i = 0; i < count; ++i)
		{
			memcpy(&loadedPlugins[i], rawRecords.data() + (size_t)i * recordSize, recordSize);
			loadedPlugins[i].format = VST3_PLUGIN_FORMAT_VST3;
		}
	}
	else if (count > 0 && fread(loadedPlugins.data(), sizeof(vst3PluginInfo_t), count, file) != count)
	{
		std::lock_guard<std::mutex> lock(scanMutex);
		plugins.clear();
		return false;
	}

	for (auto &plugin : loadedPlugins)
	{
		plugin.name[sizeof(plugin.name) - 1] = '\0';
		plugin.vendor[sizeof(plugin.vendor) - 1] = '\0';
		plugin.category[sizeof(plugin.category) - 1] = '\0';
		plugin.subCategories[sizeof(plugin.subCategories) - 1] = '\0';
		plugin.version[sizeof(plugin.version) - 1] = '\0';
		plugin.classID[sizeof(plugin.classID) - 1] = '\0';
		plugin.modulePath[sizeof(plugin.modulePath) - 1] = '\0';
		if (plugin.kind < VST3_PLUGIN_EFFECT || plugin.kind > VST3_PLUGIN_OTHER ||
			plugin.format < VST3_PLUGIN_FORMAT_VST3 || plugin.format > VST3_PLUGIN_FORMAT_VST2)
		{
			std::lock_guard<std::mutex> lock(scanMutex);
			plugins.clear();
			return false;
		}
	}

	std::lock_guard<std::mutex> lock(scanMutex);
	plugins.swap(loadedPlugins);
	return true;
}

uint32_t vst3GetInstrumentPluginCount(void)
{
	std::lock_guard<std::mutex> lock(scanMutex);
	uint32_t count = 0;
	for (const auto &plugin : plugins)
		if (plugin.kind == VST3_PLUGIN_INSTRUMENT) count++;
	return count;
}

bool vst3GetInstrumentPluginName(uint32_t index, char *name, uint32_t nameSize)
{
	if (name == nullptr || nameSize == 0) return false;
	name[0] = '\0';
	std::lock_guard<std::mutex> lock(scanMutex);
	uint32_t current = 0;
	for (const auto &plugin : plugins)
	{
		if (plugin.kind != VST3_PLUGIN_INSTRUMENT) continue;
		if (current++ == index) { std::snprintf(name, nameSize, "%s", plugin.name); return true; }
	}
	return false;
}

int32_t vst3GetAssignedInstrument(uint8_t instrument)
{
	if (instrument >= MAX_VST3_INSTRUMENTS) return -1;
	return assignedInstrumentPlugins[instrument].load(std::memory_order_relaxed);
}

bool vst3AssignInstrument(uint8_t instrument, int32_t pluginIndex)
{
	if (instrument == 0 || instrument >= MAX_VST3_INSTRUMENTS) return false;
	if (pluginIndex < -1) return false;

#ifdef _WIN32
	// Load the new plug-in before touching the audio lock: creating a plug-in can
	// take a while, and it must not run while the audio callback is blocked.
	std::unique_ptr<PluginHostInstance> newInstance;
	if (pluginIndex >= 0)
	{
		vst3PluginInfo_t info{};
		bool found = false;
		{
			std::lock_guard<std::mutex> lock(scanMutex);
			int32_t current = 0;
			for (const auto &plugin : plugins)
			{
				if (plugin.kind != VST3_PLUGIN_INSTRUMENT) continue;
				if (current++ == pluginIndex) { info = plugin; found = true; break; }
			}
		}
		if (!found)
			return false;

		newInstance = createPluginInstance(info, false);
		if (newInstance == nullptr)
			return false;
	}

	// The old instance leaves the audio path under the lock, but its destruction
	// (which unloads the module, closes editor windows and may wait on the
	// plug-in's own threads) happens after the lock is released.
	std::unique_ptr<PluginHostInstance> oldInstance;
	lockAudio();
	// release the notes the previous plugin was still holding before it goes away
	if (instrumentInstances[instrument] != nullptr)
		instrumentInstances[instrument]->allNotesOff();
	oldInstance = std::move(instrumentInstances[instrument]);
	instrumentInstances[instrument] = std::move(newInstance);
	assignedInstrumentPlugins[instrument].store(
		(instrumentInstances[instrument] != nullptr) ? pluginIndex : -1, std::memory_order_relaxed);
	scopeBufferPositions[instrument].store(0, std::memory_order_relaxed); // drop stale scope data
	updateLatencyCompensation();
	unlockAudio();

	oldInstance.reset();
	return true;
#else
	return false;
#endif
}

// returns the plug-in info for an instrument-relative plug-in index
static bool getInstrumentPluginInfoByIndex(int32_t index, vst3PluginInfo_t &info)
{
	if (index < 0)
		return false;

	std::lock_guard<std::mutex> lock(scanMutex);
	int32_t current = 0;
	for (const auto &plugin : plugins)
	{
		if (plugin.kind != VST3_PLUGIN_INSTRUMENT)
			continue;

		if (current++ == index)
		{
			info = plugin;
			return true;
		}
	}
	return false;
}

// makes a reference loaded from an .xmv file available in the plug-in list, so
// the instrument editor can show its name and index-based assignment works
static int32_t findOrAddInstrumentPlugin(const vst3PluginInfo_t &info)
{
	if (info.classID[0] == '\0' || info.modulePath[0] == '\0')
		return -1;

	std::lock_guard<std::mutex> lock(scanMutex);
	int32_t current = 0;
	for (const auto &plugin : plugins)
	{
		if (plugin.kind != VST3_PLUGIN_INSTRUMENT)
			continue;

		if (std::strcmp(plugin.classID, info.classID) == 0 &&
			std::strcmp(plugin.modulePath, info.modulePath) == 0)
		{
			return current;
		}
		current++;
	}

	plugins.push_back(info);
	return current;
}

// Writes the custom FT2 VST3 chunk (.xmv). The file is a complete, standard XM
// module followed by this chunk, so other trackers can still read the song.
bool vst3WriteSongVstChunk(FILE *file)
{
	if (file == nullptr)
		return false;

	const uint8_t magic[8] = { 'F', 'T', '2', 'V', 'S', 'T', 'X', 'M' };
	const uint32_t version = VST3_XM_CHUNK_VERSION;
	uint32_t count = 0;

#ifdef _WIN32
	for (int32_t i = 1; i < MAX_VST3_INSTRUMENTS; ++i)
	{
		if (instrumentInstances[i] != nullptr)
			count++;
	}
#endif

	if (fwrite(magic, sizeof(magic), 1, file) != 1 ||
		fwrite(&version, sizeof(version), 1, file) != 1 ||
		fwrite(&count, sizeof(count), 1, file) != 1)
	{
		return false;
	}

#ifdef _WIN32
	for (int32_t i = 1; i < MAX_VST3_INSTRUMENTS; ++i)
	{
		PluginHostInstance *instance = instrumentInstances[i].get();
		if (instance == nullptr)
			continue;

		vst3PluginInfo_t info{};
		getInstrumentPluginInfoByIndex(assignedInstrumentPlugins[i].load(std::memory_order_relaxed), info);

		vst3SongRef_t ref{};
		ref.instrument = static_cast<uint8_t>(i);
		ref.reserved[0] = static_cast<uint8_t>((info.format == VST3_PLUGIN_FORMAT_VST2) ?
			VST3_PLUGIN_FORMAT_VST2 : VST3_PLUGIN_FORMAT_VST3);
		copyText(ref.name, sizeof(ref.name), info.name);
		copyText(ref.vendor, sizeof(ref.vendor), info.vendor);
		copyText(ref.classID, sizeof(ref.classID), info.classID);
		copyText(ref.modulePath, sizeof(ref.modulePath), info.modulePath);

		std::vector<uint8_t> state;
		lockAudio();
		try
		{
			instance->captureState(state);
		}
		catch (...)
		{
			state.clear();
		}
		unlockAudio();

		if (state.size() > 0xFFFFFFFFu)
			state.clear();
		ref.stateSize = static_cast<uint32_t>(state.size());

		if (fwrite(&ref, sizeof(ref), 1, file) != 1)
			return false;
		if (!state.empty() && fwrite(state.data(), 1, state.size(), file) != state.size())
			return false;
	}
#endif

	return true;
}

// Reads the custom chunk that follows the XM data. Returns false if the file has
// no chunk (a plain .xm) or the chunk is malformed; the module itself is still
// valid in that case. Only parsing happens here: this runs on the module loading
// thread, and plug-in instances must not be created or destroyed there.
bool vst3ReadSongVstChunk(FILE *file, uint32_t byteCount)
{
	constexpr uint32_t HEADER_SIZE = 8 + 4 + 4;
	constexpr uint32_t MAX_STATE_SIZE = 64u * 1024u * 1024u;

	if (file == nullptr || byteCount < HEADER_SIZE)
		return false;

	uint8_t magic[8];
	uint32_t version = 0, count = 0;
	if (fread(magic, sizeof(magic), 1, file) != 1 ||
		fread(&version, sizeof(version), 1, file) != 1 ||
		fread(&count, sizeof(count), 1, file) != 1)
	{
		return false;
	}

	if (memcmp(magic, "FT2VSTXM", sizeof(magic)) != 0 ||
		version != VST3_XM_CHUNK_VERSION || count > MAX_VST3_INSTRUMENTS)
	{
		return false;
	}

	uint32_t remaining = byteCount - HEADER_SIZE;

#ifdef _WIN32
	std::vector<PendingSongInstrument> pending;
#endif

	for (uint32_t i = 0; i < count; ++i)
	{
		vst3SongRef_t ref{};
		if (remaining < sizeof(ref) || fread(&ref, sizeof(ref), 1, file) != 1)
			return false;
		remaining -= sizeof(ref);

		ref.name[sizeof(ref.name) - 1] = '\0';
		ref.vendor[sizeof(ref.vendor) - 1] = '\0';
		ref.classID[sizeof(ref.classID) - 1] = '\0';
		ref.modulePath[sizeof(ref.modulePath) - 1] = '\0';

		if (ref.stateSize > remaining || ref.stateSize > MAX_STATE_SIZE)
			return false;

		std::vector<uint8_t> state;
		if (ref.stateSize > 0)
		{
			state.resize(ref.stateSize);
			if (fread(state.data(), 1, state.size(), file) != state.size())
				return false;
			remaining -= ref.stateSize;
		}

#ifdef _WIN32
		if (ref.instrument > 0 && ref.instrument < MAX_VST3_INSTRUMENTS)
		{
			PendingSongInstrument entry{};
			entry.instrument = ref.instrument;
			copyText(entry.info.name, sizeof(entry.info.name), ref.name);
			copyText(entry.info.vendor, sizeof(entry.info.vendor), ref.vendor);
			copyText(entry.info.classID, sizeof(entry.info.classID), ref.classID);
			copyText(entry.info.modulePath, sizeof(entry.info.modulePath), ref.modulePath);
			entry.info.kind = VST3_PLUGIN_INSTRUMENT;
			entry.info.format = (ref.reserved[0] == VST3_PLUGIN_FORMAT_VST2) ?
				VST3_PLUGIN_FORMAT_VST2 : VST3_PLUGIN_FORMAT_VST3;
			entry.state = std::move(state);
			pending.push_back(std::move(entry));
		}
#endif
	}

#ifdef _WIN32
	{
		std::lock_guard<std::mutex> lock(pendingSongMutex);
		pendingSongInstruments = std::move(pending);
		pendingSongChunkValid = true;
	}
#endif
	return true;
}

// Creates/restores the plug-in instances of a loaded .xmv file. Must run on the
// main thread, after the module was set up: plug-in instances may only be
// created or destroyed on the thread that owns their editor windows, and never
// while the audio callback is blocked by the audio lock. Doing this from the
// module loading thread made the second .xmv load (the first one that has to
// destroy the previous song's plug-ins) crash inside the plug-ins.
bool vst3ApplySongVstChunk(void)
{
#ifdef _WIN32
	std::vector<PendingSongInstrument> pending;
	{
		std::lock_guard<std::mutex> lock(pendingSongMutex);
		if (!pendingSongChunkValid)
			return false;

		pendingSongChunkValid = false;
		pending.swap(pendingSongInstruments);
	}

	// a loaded .xmv replaces whatever the previous song had assigned. The old
	// instances leave the audio path under the lock, but are destroyed after it
	// is released.
	{
		std::array<std::unique_ptr<PluginHostInstance>, MAX_VST3_INSTRUMENTS> oldInstances;
		lockAudio();
		for (auto &instance : instrumentInstances)
		{
			if (instance != nullptr)
				instance->allNotesOff();
		}
		oldInstances.swap(instrumentInstances);
		for (auto &slot : assignedInstrumentPlugins)
			slot.store(-1, std::memory_order_relaxed);
		for (auto &pos : scopeBufferPositions)
			pos.store(0, std::memory_order_relaxed);
		updateLatencyCompensation();
		unlockAudio();
	}

	for (const auto &entry : pending)
	{
		const int32_t pluginIndex = findOrAddInstrumentPlugin(entry.info);
		if (pluginIndex < 0 || !vst3AssignInstrument(entry.instrument, pluginIndex))
			continue;
		if (entry.state.empty())
			continue;

		lockAudio();
		PluginHostInstance *instance = instrumentInstances[entry.instrument].get();
		if (instance != nullptr)
			instance->applyState(entry.state.data(), static_cast<uint32_t>(entry.state.size()));
		unlockAudio();
	}

	return true;
#else
	return false;
#endif
}

// Drops a parsed .xmv plug-in chunk when the module did not load after all.
void vst3DiscardSongVstChunk(void)
{
#ifdef _WIN32
	std::lock_guard<std::mutex> lock(pendingSongMutex);
	pendingSongInstruments.clear();
	pendingSongChunkValid = false;
#endif
}


bool vst3OpenInstrumentEditor(uint8_t instrument)
{
#ifdef _WIN32
	if (instrument == 0 || instrument >= MAX_VST3_INSTRUMENTS)
		return false;

	const int32_t assignedIndex = assignedInstrumentPlugins[instrument].load(std::memory_order_relaxed);
	if (assignedIndex < 0 || instrumentInstances[instrument] == nullptr)
		return false;

	lockAudio();
	const bool opened = instrumentInstances[instrument]->openEditor();
	unlockAudio();
	return opened;
#else
	(void)instrument;		return false;
#endif
}


extern "C" int32_t vst3InstrumentNoteOn(uint8_t instrument, uint8_t note, float velocity)
{
#ifdef _WIN32
	if (instrument < MAX_VST3_INSTRUMENTS && instrumentInstances[instrument] != nullptr)
		return instrumentInstances[instrument]->noteOn(note, velocity);
#else
	(void)instrument; (void)note; (void)velocity;
#endif
	return -1;
}

extern "C" void vst3InstrumentNoteOff(uint8_t instrument, uint8_t note, int32_t noteId)
{
#ifdef _WIN32
	if (noteId <= 0)
		return;

	if (instrument < MAX_VST3_INSTRUMENTS && instrumentInstances[instrument] != nullptr)
		instrumentInstances[instrument]->noteOff(note, noteId);
#else
	(void)instrument; (void)note; (void)noteId;
#endif
}

// panic: release every note the loaded instruments are still holding
// (must not be called while the audio callback is locked by the caller)
extern "C" void vst3AllNotesOff(void)
{
#ifdef _WIN32
	lockAudio();
	for (auto &instance : instrumentInstances)
	{
		if (instance != nullptr)
			instance->allNotesOff();
	}
	unlockAudio();
#endif
}

void vst3ProcessInstruments(uint32_t numSamples, float *mixLeft, float *mixRight)
{
#ifdef _WIN32
	if (numSamples == 0 || mixLeft == nullptr || mixRight == nullptr)
		return;

	// a fresh playback starts on the song grid, so restart the transport clock there
	if (songPlaying && !hostTransportWasPlaying)
	{
		hostTransportSamples = 0;
		hostTransportTimeMusic = 0.0;
		resetLatencyCompensationBuffers();
	}
	hostTransportWasPlaying = songPlaying;

	const PluginHostTransport transport = makeTransportContext();

	// delay the dry tracker mix by the longest plug-in latency, so that the
	// instrument outputs (which get delayed inside the plug-ins) stay on the same
	// grid. Each instrument's own output is only delayed by the remaining difference.
	dryDelay.processBlock(mixLeft, mixRight, numSamples);

	const int32_t solo = renderSoloInstrument.load(std::memory_order_relaxed);
	for (int32_t i = 0; i < MAX_VST3_INSTRUMENTS; ++i)
	{
		PluginHostInstance *instance = instrumentInstances[i].get();
		if (instance == nullptr)
			continue;

		// non-soloed instruments still get processed so their note state stays in sync
		const bool mixToOutput = (solo == VST3_RENDER_ALL) || (solo == i);
		if (!instance->process(numSamples, scopeBuffers[i].data(), scopeBufferPositions[i], transport))
			continue;

		float *outL = instance->renderedLeft();
		float *outR = instance->renderedRight();
		instrumentDelays[i].processBlock(outL, outR, numSamples);
		if (!mixToOutput)
			continue;

		for (uint32_t s = 0; s < numSamples; ++s)
		{
			mixLeft[s] += std::clamp(outL[s], -8.0f, 8.0f);
			mixRight[s] += std::clamp(instance->outputChannelCount > 1 ? outR[s] : outL[s], -8.0f, 8.0f);
		}
	}

	hostTransportSamples += numSamples;
	hostTransportTimeMusic += ((double)numSamples * (transport.tempo / 60.0)) / transport.sampleRate;
#else
    (void)numSamples; (void)mixLeft; (void)mixRight;
#endif
}

// runs the master effect chain over the finished stereo mix. Called from the
// audio callback right after vst3ProcessInstruments() has summed the
// instruments into the mix buffer.
void vst3ProcessEffects(uint32_t numSamples, float *left, float *right)
{
#ifdef _WIN32
	if (numSamples == 0 || left == nullptr || right == nullptr || masterEffectCount <= 0)
		return;

	// individual-track WAV renders only contain one instrument, so the master
	// effect chain must stay out of them
	if (renderSoloInstrument.load(std::memory_order_relaxed) != VST3_RENDER_ALL)
		return;

	const PluginHostTransport transport = makeTransportContext();

	for (int32_t i = 0; i < masterEffectCount; ++i)
	{
		MasterEffect &fx = masterEffects[i];
		if (fx.bypassed || fx.instance == nullptr)
			continue;

		fx.instance->processEffect(numSamples, left, right, transport);
	}

#else
	(void)numSamples; (void)left; (void)right;
#endif
}

void vst3SetRenderSoloInstrument(int32_t instrument)
{
	renderSoloInstrument.store(instrument, std::memory_order_relaxed);
}

void vst3SetSampleRate(float sampleRate)
{
#ifdef _WIN32
	if (sampleRate <= 0.0f)
		return;

	for (auto &instance : instrumentInstances)
	{
		if (instance != nullptr)
			instance->setSampleRate(sampleRate);
	}

	for (int32_t i = 0; i < masterEffectCount; ++i)
	{
		if (masterEffects[i].instance != nullptr)
			masterEffects[i].instance->setSampleRate(sampleRate);
	}

	// the reported latency can depend on the sample rate, and a new rate also means
	// fresh delay lines (e.g. at the start of a WAV render)
	updateLatencyCompensation();
	resetLatencyCompensationBuffers();
#else
	(void)sampleRate;
#endif
}

uint32_t vst3GetScopeSamples(uint8_t instrument, float *dest, uint32_t maxSamples)
{
	if (instrument >= MAX_VST3_INSTRUMENTS || dest == nullptr || maxSamples == 0)
		return 0;

	const uint32_t pos = scopeBufferPositions[instrument].load(std::memory_order_relaxed);
	const uint32_t available = (pos < VST3_SCOPE_BUFFER_SIZE) ? pos : VST3_SCOPE_BUFFER_SIZE;
	const uint32_t count = (available < maxSamples) ? available : maxSamples;
	if (count == 0)
		return 0;

	const uint32_t mask = VST3_SCOPE_BUFFER_SIZE - 1;
	const float *src = scopeBuffers[instrument].data();
	uint32_t readPos = (pos - count) & mask;
	for (uint32_t i = 0; i < count; ++i)
	{
		dest[i] = src[readPos];
		readPos = (readPos + 1) & mask;
	}

	return count;
}

float vst3GetScopePeak(uint8_t instrument)
{
	if (instrument >= MAX_VST3_INSTRUMENTS)
		return 0.0f;

	const uint32_t pos = scopeBufferPositions[instrument].load(std::memory_order_relaxed);
	uint32_t count = (pos < VST3_SCOPE_BUFFER_SIZE) ? pos : VST3_SCOPE_BUFFER_SIZE;
	if (count > 512)
		count = 512;

	const uint32_t mask = VST3_SCOPE_BUFFER_SIZE - 1;
	const float *src = scopeBuffers[instrument].data();
	const uint32_t start = (pos - count) & mask;

	float peak = 0.0f;
	for (uint32_t i = 0; i < count; ++i)
	{
		const float sample = fabsf(src[(start + i) & mask]);
		if (sample > peak)
			peak = sample;
	}

	return peak;
}

void vst3GetScanProgress(vst3ScanProgress_t *progress)
{
	if (progress == nullptr)
		return;

	std::lock_guard<std::mutex> lock(progressMutex);
	*progress = scanProgress;
}

void vst3ResetScanProgress(void)
{
	setScanProgress(0, 0, "Preparing scan...");

	std::lock_guard<std::mutex> lock(progressMutex);
	skippedPlugins.clear();
	scanProgress.skippedPlugins = 0;
}

uint32_t vst3GetSkippedPluginCount(void)
{
	std::lock_guard<std::mutex> lock(progressMutex);
	return static_cast<uint32_t>(skippedPlugins.size());
}

bool vst3GetSkippedPluginInfo(uint32_t index, vst3SkippedPlugin_t *info)
{
	if (info == nullptr)
		return false;

	std::lock_guard<std::mutex> lock(progressMutex);
	if (index >= skippedPlugins.size())
		return false;

	*info = skippedPlugins[index];
	return true;
}

uint32_t vst3GetLastScanError(char *buffer, uint32_t bufferSize)
{
	std::lock_guard<std::mutex> lock(scanMutex);
	if (buffer != nullptr && bufferSize > 0)
		std::snprintf(buffer, bufferSize, "%s", lastError.c_str());
	return static_cast<uint32_t>(lastError.size());
}

uint32_t vst3GetLastLoadError(char *buffer, uint32_t bufferSize)
{
	std::lock_guard<std::mutex> lock(loadErrorMutex);
	if (buffer != nullptr && bufferSize > 0)
		std::snprintf(buffer, bufferSize, "%s", lastLoadError.c_str());
	return static_cast<uint32_t>(lastLoadError.size());
}

extern "C" void vst3Cleanup(void)
{
#ifdef _WIN32
    lockAudio();
    for (auto &instance : instrumentInstances)
    {
        if (instance != nullptr)
            instance->allNotesOff();
        instance.reset();
    }
    for (auto &slot : assignedInstrumentPlugins)
        slot.store(-1, std::memory_order_relaxed);
    updateLatencyCompensation();
    masterEffectCount = 0;
    for (auto &fx : masterEffects)
    {
        fx.instance.reset();
        fx.pluginIndex = -1;
        fx.bypassed = false;
    }
    unlockAudio();
#endif
}


#ifdef _WIN32
namespace
{
	bool getEffectPluginInfoByIndex(int32_t index, vst3PluginInfo_t &info)
	{
		if (index < 0)
			return false;

		std::lock_guard<std::mutex> lock(scanMutex);
		int32_t current = 0;
		for (const auto &plugin : plugins)
		{
			if (plugin.kind != VST3_PLUGIN_EFFECT)
				continue;

			if (current++ == index)
			{
				info = plugin;
				return true;
			}
		}

		return false;
	}

	bool loadEffectInstance(int32_t pluginIndex, std::unique_ptr<PluginHostInstance> &instance)
	{
		vst3PluginInfo_t info{};
		if (!getEffectPluginInfoByIndex(pluginIndex, info))
			return false;

		instance = createPluginInstance(info, true);
		return instance != nullptr;
	}
}
#endif

uint32_t vst3GetEffectPluginCount(void)
{
	std::lock_guard<std::mutex> lock(scanMutex);
	uint32_t count = 0;
	for (const auto &plugin : plugins)
	{
		if (plugin.kind == VST3_PLUGIN_EFFECT)
			count++;
	}
	return count;
}

bool vst3GetEffectPluginName(uint32_t index, char *name, uint32_t nameSize)
{
	if (name == nullptr || nameSize == 0)
		return false;

	name[0] = '\0';

	std::lock_guard<std::mutex> lock(scanMutex);
	uint32_t current = 0;
	for (const auto &plugin : plugins)
	{
		if (plugin.kind != VST3_PLUGIN_EFFECT)
			continue;

		if (current++ == index)
		{
			std::snprintf(name, nameSize, "%s", plugin.name);
			return true;
		}
	}

	return false;
}

bool vst3GetEffectPluginLabel(uint32_t index, char *label, uint32_t labelSize)
{
	if (label == nullptr || labelSize == 0)
		return false;

	label[0] = '\0';

	std::lock_guard<std::mutex> lock(scanMutex);
	uint32_t current = 0;
	for (const auto &plugin : plugins)
	{
		if (plugin.kind != VST3_PLUGIN_EFFECT)
			continue;

		if (current++ == index)
		{
			std::snprintf(label, labelSize, "%s  %s",
				(plugin.format == VST3_PLUGIN_FORMAT_VST2) ? "VST2" : "VST3", plugin.name);
			return true;
		}
	}

	return false;
}

bool vst3GetInstrumentPluginLabel(uint32_t index, char *label, uint32_t labelSize)
{
	if (label == nullptr || labelSize == 0)
		return false;

	label[0] = '\0';

	std::lock_guard<std::mutex> lock(scanMutex);
	uint32_t current = 0;
	for (const auto &plugin : plugins)
	{
		if (plugin.kind != VST3_PLUGIN_INSTRUMENT)
			continue;

		if (current++ == index)
		{
			std::snprintf(label, labelSize, "%s  %s",
				(plugin.format == VST3_PLUGIN_FORMAT_VST2) ? "VST2" : "VST3", plugin.name);
			return true;
		}
	}

	return false;
}

int32_t vst3GetMasterEffectCount(void)
{
#ifdef _WIN32
	return masterEffectCount;
#else
	return 0;
#endif
}

bool vst3GetMasterEffectInfo(int32_t slot, char *name, uint32_t nameSize, int32_t *pluginIndex, bool *bypassed)
{
	if (name != nullptr && nameSize > 0)
		name[0] = '\0';

#ifdef _WIN32
	if (slot < 0 || slot >= masterEffectCount || masterEffects[slot].instance == nullptr)
		return false;

	if (name != nullptr && nameSize > 0)
		std::snprintf(name, nameSize, "%s", masterEffects[slot].instance->pluginName.c_str());
	if (pluginIndex != nullptr)
		*pluginIndex = masterEffects[slot].pluginIndex;
	if (bypassed != nullptr)
		*bypassed = masterEffects[slot].bypassed;

	return true;
#else
	(void)slot; (void)pluginIndex; (void)bypassed;		return false;
#endif
}


bool vst3AddMasterEffect(int32_t pluginIndex)
{
#ifdef _WIN32
	if (pluginIndex < 0 || masterEffectCount >= MAX_MASTER_EFFECTS)
		return false;

	std::unique_ptr<PluginHostInstance> instance;
	if (!loadEffectInstance(pluginIndex, instance))
		return false;

	lockAudio();
	MasterEffect &fx = masterEffects[masterEffectCount];
	fx.pluginIndex = pluginIndex;
	fx.bypassed = false;
	fx.instance = std::move(instance);
	masterEffectCount++;
	unlockAudio();
	return true;
#else
	(void)pluginIndex;		return false;
#endif
}


bool vst3RemoveMasterEffect(int32_t slot)
{
#ifdef _WIN32
	if (slot < 0 || slot >= masterEffectCount)
		return false;

	std::unique_ptr<PluginHostInstance> removed;
	lockAudio();
	removed = std::move(masterEffects[slot].instance);
	for (int32_t i = slot; i < masterEffectCount - 1; ++i)
		masterEffects[i] = std::move(masterEffects[i + 1]);
	masterEffectCount--;
	masterEffects[masterEffectCount].instance.reset();
	masterEffects[masterEffectCount].pluginIndex = -1;
	masterEffects[masterEffectCount].bypassed = false;
	unlockAudio();

	// unload the plug-in outside the audio lock (this terminates the instance)
	removed.reset();
	return true;
#else
	(void)slot;		return false;
#endif
}


bool vst3SetMasterEffectBypass(int32_t slot, bool bypassed)
{
#ifdef _WIN32
	if (slot < 0 || slot >= masterEffectCount)
		return false;

	lockAudio();
	masterEffects[slot].bypassed = bypassed;
	unlockAudio();
	return true;
#else
	(void)slot; (void)bypassed;		return false;
#endif
}


bool vst3OpenMasterEffectEditor(int32_t slot)
{
#ifdef _WIN32
	if (slot < 0 || slot >= masterEffectCount || masterEffects[slot].instance == nullptr)
		return false;

	lockAudio();
	const bool opened = masterEffects[slot].instance->openEditor();
	unlockAudio();
	return opened;
#else
	(void)slot;		return false;
#endif
}
