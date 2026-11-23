#include "ft2_vst3.h"

extern "C" void vst3ScannerWorkerMain(void) { }
extern "C" void vst3Cleanup(void) { }

// no VST3 support in this build, but keep the custom .xmv format well-formed
extern "C" bool vst3WriteSongVstChunk(FILE *file)
{
	if (file == nullptr)
		return false;

	const char magic[8] = { 'F', 'T', '2', 'V', 'S', 'T', 'X', 'M' };
	const uint32_t version = VST3_XM_CHUNK_VERSION;
	const uint32_t count = 0;
	return fwrite(magic, 1, sizeof(magic), file) == sizeof(magic) &&
		fwrite(&version, sizeof(version), 1, file) == 1 &&
		fwrite(&count, sizeof(count), 1, file) == 1;
}

extern "C" bool vst3ReadSongVstChunk(FILE *file, uint32_t byteCount)
{
	(void)file; (void)byteCount;
	return false;
}

extern "C" bool vst3ApplySongVstChunk(void) { return false; }
extern "C" void vst3DiscardSongVstChunk(void) { }
