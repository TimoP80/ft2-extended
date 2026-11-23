#include "ft2_vst3.h"

bool vst3AddSearchPath(const char *pathUTF8) { (void)pathUTF8; return false; }
bool vst3ScanDirectory(const char *pathUTF8) { (void)pathUTF8; return false; }
bool vst3Scan(void) { return false; }
uint32_t vst3GetPluginCount(void) { return 0; }
bool vst3GetPluginInfo(uint32_t index, vst3PluginInfo_t *info) { (void)index; (void)info; return false; }
bool vst3WritePluginCache(FILE *file) { (void)file; return true; } // no cache, but not a save error
bool vst3ReadPluginCache(FILE *file, uint32_t byteCount) { (void)file; (void)byteCount; return false; }
uint32_t vst3GetInstrumentPluginCount(void) { return 0; }
bool vst3GetInstrumentPluginName(uint32_t index, char *name, uint32_t nameSize) { (void)index; (void)name; (void)nameSize; return false; }
uint32_t vst3GetEffectPluginCount(void) { return 0; }
bool vst3GetEffectPluginName(uint32_t index, char *name, uint32_t nameSize) { (void)index; (void)name; (void)nameSize; return false; }
bool vst3GetInstrumentPluginLabel(uint32_t index, char *label, uint32_t labelSize) { (void)index; (void)label; (void)labelSize; return false; }
bool vst3GetEffectPluginLabel(uint32_t index, char *label, uint32_t labelSize) { (void)index; (void)label; (void)labelSize; return false; }

void vst3ProcessEffects(uint32_t numSamples, float *left, float *right) { (void)numSamples; (void)left; (void)right; }
int32_t vst3GetMasterEffectCount(void) { return 0; }
bool vst3GetMasterEffectInfo(int32_t slot, char *name, uint32_t nameSize, int32_t *pluginIndex, bool *bypassed) { (void)slot; (void)name; (void)nameSize; (void)pluginIndex; (void)bypassed; return false; }
bool vst3AddMasterEffect(int32_t pluginIndex) { (void)pluginIndex; return false; }
bool vst3RemoveMasterEffect(int32_t slot) { (void)slot; return false; }
bool vst3SetMasterEffectBypass(int32_t slot, bool bypassed) { (void)slot; (void)bypassed; return false; }
bool vst3OpenMasterEffectEditor(int32_t slot) { (void)slot; return false; }

int32_t vst3GetAssignedInstrument(uint8_t instrument) { (void)instrument; return -1; }
bool vst3AssignInstrument(uint8_t instrument, int32_t pluginIndex) { (void)instrument; (void)pluginIndex; return false; }
bool vst3OpenInstrumentEditor(uint8_t instrument) { (void)instrument; return false; }

int32_t vst3InstrumentNoteOn(uint8_t instrument, uint8_t note, float velocity) { (void)instrument; (void)note; (void)velocity; return -1; }
void vst3InstrumentNoteOff(uint8_t instrument, uint8_t note, int32_t noteId) { (void)instrument; (void)note; (void)noteId; }
void vst3AllNotesOff(void) { }
void vst3ProcessInstruments(uint32_t numSamples, float *mixLeft, float *mixRight) { (void)numSamples; (void)mixLeft; (void)mixRight; }

uint32_t vst3GetScopeSamples(uint8_t instrument, float *dest, uint32_t maxSamples) { (void)instrument; (void)dest; (void)maxSamples; return 0; }
float vst3GetScopePeak(uint8_t instrument) { (void)instrument; return 0.0f; }
void vst3SetRenderSoloInstrument(int32_t instrument) { (void)instrument; }
void vst3SetSampleRate(float sampleRate) { (void)sampleRate; }

void vst3GetScanProgress(vst3ScanProgress_t *progress) { (void)progress; }
void vst3ResetScanProgress(void) { }
uint32_t vst3GetSkippedPluginCount(void) { return 0; }
bool vst3GetSkippedPluginInfo(uint32_t index, vst3SkippedPlugin_t *info) { (void)index; (void)info; return false; }
uint32_t vst3GetLastScanError(char *buffer, uint32_t bufferSize) { (void)buffer; (void)bufferSize; return 0; }
uint32_t vst3GetLastLoadError(char *buffer, uint32_t bufferSize) { (void)buffer; (void)bufferSize; return 0; }

void vst3ScannerWorkerMain(void) { }
void vst3Cleanup(void) { }

// no VST3 support in this build, but keep the custom .xmv format well-formed
bool vst3WriteSongVstChunk(FILE *file)
{
	if (file == NULL)
		return false;

	const char magic[8] = { 'F', 'T', '2', 'V', 'S', 'T', 'X', 'M' };
	const uint32_t version = VST3_XM_CHUNK_VERSION;
	const uint32_t count = 0;
	return fwrite(magic, 1, sizeof(magic), file) == sizeof(magic) &&
		fwrite(&version, sizeof(version), 1, file) == 1 &&
		fwrite(&count, sizeof(count), 1, file) == 1;
}

bool vst3ReadSongVstChunk(FILE *file, uint32_t byteCount)
{
	(void)file; (void)byteCount;
	return false;
}

bool vst3ApplySongVstChunk(void) { return false; }
void vst3DiscardSongVstChunk(void) { }
