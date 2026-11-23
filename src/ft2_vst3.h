#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define MAX_VST3_INSTRUMENTS 131

enum
{
	VST3_PLUGIN_EFFECT = 0,
	VST3_PLUGIN_INSTRUMENT = 1,
	VST3_PLUGIN_OTHER = 2
};

enum
{
	VST3_PLUGIN_FORMAT_VST3 = 0,
	VST3_PLUGIN_FORMAT_VST2 = 1
};

typedef struct
{
	char name[128];
	char vendor[128];
	char category[64];
	char subCategories[128];
	char version[64];
	char classID[64];
	char modulePath[512];
	int32_t kind;
	int32_t format; // VST3_PLUGIN_FORMAT_VST3 or VST3_PLUGIN_FORMAT_VST2
} vst3PluginInfo_t;

typedef struct
{
	uint32_t completedModules;
	uint32_t totalModules;
	uint32_t skippedPlugins; // found but not instantiable, left out of the list
	char currentModulePath[512];
} vst3ScanProgress_t;

// scan report: one plug-in class (or whole module) that was found but left out
// of the plug-in list, so the user can see what did not load and why
#define MAX_VST3_SKIPPED_PLUGINS 256

typedef struct
{
	char name[128];   // plug-in name, or the module file name for module failures
	char reason[192];
	char modulePath[512];
} vst3SkippedPlugin_t;

/* Custom FT2 XM extension (.xmv). The file is a normal .XM module followed by
** this chunk, which stores the VST3 instrument reference and the plug-in state
** (the used preset) for every tracker instrument slot. Other trackers ignore
** the trailing chunk.
*/
#define VST3_XM_CHUNK_MAGIC "FT2VSTXM"
#define VST3_XM_CHUNK_VERSION 1

#ifdef _MSC_VER
#pragma pack(push)
#pragma pack(1)
#endif
typedef struct vst3SongRef_t
{
	uint8_t instrument;
	uint8_t reserved[3];
	uint32_t stateSize; // bytes of preset/state data following this record
	char name[128];
	char vendor[128];
	char classID[64];
	char modulePath[512];
}
#ifdef __GNUC__
__attribute__ ((packed))
#endif
vst3SongRef_t;
#ifdef _MSC_VER
#pragma pack(pop)
#endif

#ifdef __cplusplus
extern "C" {
#endif

// writes/reads the custom .xmv chunk. The reader only parses the chunk; the
// plug-ins are instantiated and restored later by vst3ApplySongVstChunk(). It
// returns false when the file has no chunk (a plain .xm) or the chunk is
// malformed; the module is still valid in that case.
bool vst3WriteSongVstChunk(FILE *file);
bool vst3ReadSongVstChunk(FILE *file, uint32_t byteCount);

// Applies the plug-in references/presets of an .xmv that vst3ReadSongVstChunk()
// parsed. Must be called from the main thread once the module was set up (the
// plug-in instances have to be created/destroyed on the thread that owns their
// editor windows). Returns true if a chunk was pending and got applied.
bool vst3ApplySongVstChunk(void);
void vst3DiscardSongVstChunk(void); // drops a parsed chunk of a failed load

bool vst3AddSearchPath(const char *pathUTF8);
bool vst3ScanDirectory(const char *pathUTF8);
bool vst3Scan(void);
uint32_t vst3GetPluginCount(void);
bool vst3GetPluginInfo(uint32_t index, vst3PluginInfo_t *info);
bool vst3WritePluginCache(FILE *file);
bool vst3ReadPluginCache(FILE *file, uint32_t byteCount);
uint32_t vst3GetInstrumentPluginCount(void);
bool vst3GetInstrumentPluginName(uint32_t index, char *name, uint32_t nameSize);
uint32_t vst3GetEffectPluginCount(void);
bool vst3GetEffectPluginName(uint32_t index, char *name, uint32_t nameSize);

// list row text with the plug-in format in front ("VST2  Name"), so the lists
// show which host a plug-in comes from
bool vst3GetInstrumentPluginLabel(uint32_t index, char *label, uint32_t labelSize);
bool vst3GetEffectPluginLabel(uint32_t index, char *label, uint32_t labelSize);

// master effect chain: VST3 effects inserted on the final stereo mix
#define MAX_VST3_MASTER_EFFECTS 4
void vst3ProcessEffects(uint32_t numSamples, float *left, float *right);
int32_t vst3GetMasterEffectCount(void);
bool vst3GetMasterEffectInfo(int32_t slot, char *name, uint32_t nameSize, int32_t *pluginIndex, bool *bypassed);
bool vst3AddMasterEffect(int32_t pluginIndex);
bool vst3RemoveMasterEffect(int32_t slot);
bool vst3SetMasterEffectBypass(int32_t slot, bool bypassed);
bool vst3OpenMasterEffectEditor(int32_t slot);

int32_t vst3GetAssignedInstrument(uint8_t instrument);
bool vst3AssignInstrument(uint8_t instrument, int32_t pluginIndex);
bool vst3OpenInstrumentEditor(uint8_t instrument);

// Starts a note on the plug-in assigned to a tracker instrument slot. Every
// note-on gets a unique id so that the matching note-off releases exactly this
// note, even when other channels play the same pitch. Returns the id (-1 if the
// note was not started, e.g. no plug-in is assigned).
int32_t vst3InstrumentNoteOn(uint8_t instrument, uint8_t note, float velocity);
void vst3InstrumentNoteOff(uint8_t instrument, uint8_t note, int32_t noteId);
void vst3AllNotesOff(void); // panic: release all notes held by VST3 instruments
void vst3ProcessInstruments(uint32_t numSamples, float *mixLeft, float *mixRight);

// live mono capture of an instrument's output, for the channel scopes
uint32_t vst3GetScopeSamples(uint8_t instrument, float *dest, uint32_t maxSamples);
float vst3GetScopePeak(uint8_t instrument);

// solo filtering for the individual-track WAV renderer
#define VST3_RENDER_ALL  (-1)
#define VST3_RENDER_NONE (-2)
void vst3SetRenderSoloInstrument(int32_t instrument);

// reconfigures every loaded instrument to a new sample rate. Must only be called
// while audio output is paused (the WAV renderer does this for off-rate renders).
void vst3SetSampleRate(float sampleRate);

void vst3GetScanProgress(vst3ScanProgress_t *progress);
void vst3ResetScanProgress(void);
uint32_t vst3GetSkippedPluginCount(void);
bool vst3GetSkippedPluginInfo(uint32_t index, vst3SkippedPlugin_t *info);
uint32_t vst3GetLastScanError(char *buffer, uint32_t bufferSize);
uint32_t vst3GetLastLoadError(char *buffer, uint32_t bufferSize);

// Called from main() when the process was started as an isolated scan worker.
void vst3ScannerWorkerMain(void);

void vst3Cleanup(void);

#ifdef __cplusplus
}
#endif
