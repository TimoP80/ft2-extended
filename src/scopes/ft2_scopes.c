// for finding memory leaks in debug mode with Visual Studio
#if defined _DEBUG && defined _MSC_VER
#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "../ft2_header.h"
#include "../ft2_events.h"
#include "../ft2_config.h"
#include "../ft2_audio.h"
#include "../ft2_gui.h"
#include "../ft2_midi.h"
#include "../ft2_bmp.h"
#include "../ft2_mouse.h"
#include "../ft2_video.h"
#include "../ft2_tables.h"
#include "../ft2_structs.h"
#include "../ft2_hpc.h"
#include "../ft2_vst3.h"
#include "ft2_scopes.h"
#include "ft2_scopedraw.h"

static volatile bool scopesUpdatingFlag, scopesDisplayingFlag;
static hpc_t scopeHpc;
static volatile scope_t scope[MAX_CHANNELS];
static uint8_t vst3ScopeOwner[MAX_VST3_INSTRUMENTS]; // channel showing an instrument's live output
static SDL_Thread *scopeThread;

lastChInstr_t lastChInstr[MAX_CHANNELS]; // global

int32_t getSamplePositionFromScopes(uint8_t ch)
{
	if (ch >= song.numChannels)
		return -1;

	volatile scope_t sc = scope[ch]; // cache it

	if (!sc.active || sc.sampleEnd == 0)
		return -1;

	if (sc.position >= 0 && sc.position < sc.sampleEnd)
	{
		if (sc.samplingBackwards) // get actual pos when in backwards mode (pingpong loop)
			sc.position = (sc.sampleEnd - 1) - (sc.position - sc.loopStart);

		return sc.position;
	}

	return -1; // not active or overflown
}

void stopAllScopes(void)
{
	// wait for scopes to finish updating
	while (scopesUpdatingFlag);
	
	volatile scope_t *sc = scope;
	for (int32_t i = 0; i < MAX_CHANNELS; i++, sc++)
		sc->active = false;

	memset(vst3ScopeOwner, 0xFF, sizeof (vst3ScopeOwner));

	// wait for scope displaying to be done (safety)
	while (scopesDisplayingFlag);
}

// toggle mute
static void setChannelMute(int32_t chNr, bool off)
{
	channel_t *ch = &channel[chNr];

	if (!audio.locked)
		lockAudio();

	ch->channelOff = off;
	if (ch->channelOff)
	{
		ch->efx = 0;
		ch->efxData = 0;
		ch->realVol = 0;
		ch->outVol = 0;
		ch->oldVol = 0;
		ch->fFinalVol = 0.0f;
		ch->outPan = 128;
		ch->oldPan = 128;
		ch->finalPan = 128;
		ch->status = CS_UPDATE_VOL;

		ch->keyOff = true; // non-FT2 bug fix for stuck piano keys
	}

	if (audio.locked)
		unlockAudio();

	scope[chNr].wasCleared = false;
}

static void drawScopeNumber(uint16_t scopeXOffs, uint16_t scopeYOffs, uint8_t chNr, bool outline)
{
	scopeXOffs++;
	scopeYOffs++;
	chNr++;

	if (outline)
	{
		if (chNr < 10) // one digit?
		{
			charOutOutlined(scopeXOffs, scopeYOffs, PAL_MOUSEPT, '0' + chNr);
		}
		else
		{
			charOutOutlined(scopeXOffs, scopeYOffs, PAL_MOUSEPT, '0' + (chNr / 10));
			charOutOutlined(scopeXOffs + 7, scopeYOffs, PAL_MOUSEPT, '0' + (chNr % 10));
		}
	}
	else
	{
		if (chNr < 10) // one digit?
		{
			charOut(scopeXOffs, scopeYOffs, PAL_MOUSEPT, '0' + chNr);
		}
		else
		{
			charOut(scopeXOffs, scopeYOffs, PAL_MOUSEPT, '0' + (chNr / 10));
			charOut(scopeXOffs + 7, scopeYOffs, PAL_MOUSEPT, '0' + (chNr % 10));
		}
	}
}

static void redrawScope(int32_t ch)
{
	int32_t i;

	int32_t chansPerRow = (uint32_t)song.numChannels >> 1;
	int32_t chanLookup = chansPerRow - 1;
	const uint16_t *scopeLens = scopeLenTab[chanLookup];

	// get x,y,len for scope according to channel (we must do it this way since 'len' can differ!)

	uint16_t x = 2;
	uint16_t y = 94;

	uint16_t scopeLen = 0; // prevent compiler warning
	for (i = 0; i < song.numChannels; i++)
	{
		scopeLen = scopeLens[i];

		if (i == chansPerRow) // did we reach end of row?
		{
			// yes, go one row down
			x  = 2;
			y += 39;
		}

		if (i == ch)
			break;

		// adjust position to next channel
		x += scopeLen + 3;
	}

	drawFramework(x, y, scopeLen + 2, 38, FRAMEWORK_TYPE2);

	// draw mute graphics if channel is muted
	if (editor.channelMuted[i])
	{
		const uint16_t muteGfxLen = scopeMuteBMP_Widths[chanLookup];
		const uint16_t muteGfxX = x + ((scopeLen - muteGfxLen) >> 1);

		blitFastClipX(muteGfxX, y + 6, bmp.scopeMute+scopeMuteBMP_Offs[chanLookup], 162, scopeMuteBMP_Heights[chanLookup], muteGfxLen);

		if (config.ptnChnNumbers)
			drawScopeNumber(x + 1, y + 1, (uint8_t)i, true);
	}

	scope[ch].wasCleared = false;
}

void refreshScopes(void)
{
	for (int32_t i = 0; i < MAX_CHANNELS; i++)
		scope[i].wasCleared = false;
}

static void channelMode(int32_t chn)
{
	int32_t i;
	
	ASSERT(chn < song.numChannels);

	bool m = mouse.leftButtonPressed && !mouse.rightButtonPressed;
	bool m2 = mouse.rightButtonPressed && mouse.leftButtonPressed;

	if (m2)
	{
		bool test = false;
		for (i = 0; i < song.numChannels; i++)
		{
			if (i != chn && editor.channelMuted[i])
				test = true;
		}

		if (test)
		{
			for (i = 0; i < song.numChannels; i++)
				editor.channelMuted[i] = false;
		}
		else
		{
			for (i = 0; i < song.numChannels; i++)
				editor.channelMuted[i] = !(i == chn);
		}
	}
	else if (m)
	{
		editor.channelMuted[chn] ^= 1;
	}
	else
	{
		if (!editor.channelMuted[chn])
		{
			config.multiRecChn[chn] ^= 1;
		}
		else
		{
			config.multiRecChn[chn] = true;
			editor.channelMuted[chn] = false;
			m = true;
		}
	}

	for (i = 0; i < song.numChannels; i++)
		setChannelMute(i, editor.channelMuted[i]);

	if (m2)
	{
		for (i = 0; i < song.numChannels; i++)
			redrawScope(i);
	}
	else
	{
		redrawScope(chn);
	}
}

bool testScopesMouseDown(void)
{
	int32_t i;

	if (!ui.scopesShown)
		return false;

	if (mouse.y >= 95 && mouse.y <= 169 && mouse.x >= 3 && mouse.x <= 288)
	{
		if (mouse.y > 130 && mouse.y < 134)
			return true;

		int32_t chansPerRow = (uint32_t)song.numChannels >> 1;
		const uint16_t *scopeLens = scopeLenTab[chansPerRow-1];

		// find out if we clicked inside a scope
		uint16_t x = 3;
		for (i = 0; i < chansPerRow; i++)
		{
			if (mouse.x >= x && mouse.x < x+scopeLens[i])
				break;

			x += scopeLens[i]+3;
		}

		if (i == chansPerRow)
			return true; // scope framework was clicked instead

		int32_t chanToToggle = i;
		if (mouse.y >= 134) // second row of scopes?
			chanToToggle += chansPerRow; // yes, increase lookup offset

		channelMode(chanToToggle);
		return true;
	}

	return false;
}

static void scopeTrigger(int32_t ch, const sample_t *s, int32_t playOffset)
{
	volatile scope_t tempState;
	volatile scope_t *sc = &scope[ch];

	int32_t length = s->length;
	int32_t loopStart = s->loopStart;
	int32_t loopLength = s->loopLength;
	int32_t loopEnd = s->loopStart + s->loopLength;
	uint8_t loopType = GET_LOOPTYPE(s->flags);
	bool sample16Bit = !!(s->flags & SAMPLE_16BIT);

	if (s->dataPtr == NULL || length < 1)
	{
		sc->active = false; // shut down scope (illegal parameters)
		return;
	}

	tempState = *sc; // get copy of current scope state

	if (loopLength < 1) // disable loop if loopLength is below 1
		loopType = 0;

	if (sample16Bit)
	{
		tempState.base16 = (const int16_t *)s->dataPtr;
		tempState.leftEdgeTaps16 = s->leftEdgeTapSamples16 + MAX_LEFT_TAPS;
	}
	else
	{
		tempState.base8 = s->dataPtr;
		tempState.leftEdgeTaps8 = s->leftEdgeTapSamples8 + MAX_LEFT_TAPS;
	}

	tempState.sample16Bit = sample16Bit;
	tempState.loopType = loopType;
	tempState.hasLooped = false;
	tempState.samplingBackwards = false;
	tempState.sampleEnd = (loopType == LOOP_DISABLED) ? length : loopEnd;
	tempState.loopStart = loopStart;
	tempState.loopLength = loopLength;
	tempState.loopEnd = loopEnd;
	tempState.position = playOffset;
	tempState.positionFrac = 0;
	
	// if position overflows (f.ex. through 9xx command), shut down scopes
	if (tempState.position >= tempState.sampleEnd)
	{
		sc->active = false;
		return;
	}

	tempState.active = true;

	/* Update live scope now.
	** In theory it -can- be written to in the middle of a cached read,
	** then the read thread writes its own non-updated cached copy back and
	** the trigger never happens. So far I have never seen it happen,
	** so it's probably very rare. Yes, this is not good coding...
	*/

	*sc = tempState; // set new scope state
}

static void updateScopes(void)
{
	scopesUpdatingFlag = true;

	volatile scope_t *sc = scope;
	for (int32_t i = 0; i < song.numChannels; i++, sc++)
	{
		volatile scope_t s = *sc; // get copy of current scope state
		if (!s.active)
			continue; // scope is not active

		if (s.vst3)
		{
			// live VST3 output has no sample position to advance. Blank the scope once
			// the instrument has been silent for a while (release tails included).
			if (vst3GetScopePeak(s.vst3Instrument) < 0.0002f)
			{
				if (s.vst3SilentFrames < 255)
					s.vst3SilentFrames++;

				if (s.vst3SilentFrames > 16) // ~250ms at SCOPE_HZ
					s.active = false;
			}
			else
			{
				s.vst3SilentFrames = 0;
			}

			*sc = s; // set new scope state
			continue;
		}

		// scope position update

		s.positionFrac += s.delta;
		s.position += s.positionFrac >> SCOPE_FRAC_BITS;
		s.positionFrac &= SCOPE_FRAC_MASK;

		if (s.position >= s.sampleEnd)
		{
			if (s.loopType == LOOP_PINGPONG)
			{
				if (s.loopLength >= 2)
				{
					// wrap as forward loop (position is inverted if sampling backwards, when needed)

					const uint32_t overflow = s.position - s.sampleEnd;
					const uint32_t cycles = overflow / s.loopLength;
					const uint32_t phase = overflow % s.loopLength;

					s.position = s.loopStart + phase;
					s.samplingBackwards ^= !(cycles & 1);
				}
				else
				{
					s.position = s.loopStart;
				}

				s.hasLooped = true;
			}
			else if (s.loopType == LOOP_FORWARD)
			{
				if (s.loopLength >= 2)
					s.position = s.loopStart + ((s.position - s.sampleEnd) % s.loopLength);
				else
					s.position = s.loopStart;

				s.hasLooped = true;
			}
			else // no loop
			{
				s.active = false;
			}
		}

		*sc = s; // set new scope state
	}
	scopesUpdatingFlag = false;
}

// live waveform of a VST3 instrument, drawn as a min/max envelope
static void drawVST3Scope(const scope_t *s, uint32_t scopeXOffs, uint32_t scopeDrawLen, int32_t scopeLineY)
{
	enum { VST3_SCOPE_WINDOW = 1024 };
	float samples[VST3_SCOPE_WINDOW];

	const uint32_t count = vst3GetScopeSamples(s->vst3Instrument, samples, VST3_SCOPE_WINDOW);
	if (count == 0 || scopeDrawLen == 0)
		return;

	// the plugin output is not scaled by the channel volume, so use full scale
	const float fVolume = (float)(SCOPE_HEIGHT / 2);

	const uint32_t color = video.palette[PAL_PATTEXT];
	const int32_t yMin = scopeLineY - (SCOPE_HEIGHT / 2) + 1;
	const int32_t yMax = scopeLineY + (SCOPE_HEIGHT / 2) - 1;

	for (uint32_t i = 0; i < scopeDrawLen; i++)
	{
		const uint32_t start = (i * count) / scopeDrawLen;
		if (start >= count)
			break;

		uint32_t end = ((i + 1) * count) / scopeDrawLen;
		if (end <= start)
			end = start + 1;

		float minSample = samples[start];
		float maxSample = samples[start];
		for (uint32_t j = start + 1; j < end; j++)
		{
			if (samples[j] < minSample) minSample = samples[j];
			if (samples[j] > maxSample) maxSample = samples[j];
		}

		int32_t y1 = CLAMP(scopeLineY - (int32_t)floorf(maxSample * fVolume), yMin, yMax);
		int32_t y2 = CLAMP(scopeLineY - (int32_t)floorf(minSample * fVolume), yMin, yMax);
		if (y1 > y2)
		{
			const int32_t tmp = y1;
			y1 = y2;
			y2 = tmp;
		}

		const uint32_t x = scopeXOffs + i;
		for (int32_t y = y1; y <= y2; y++)
			video.frameBuffer[(y * SCREEN_W) + x] = color;
	}
}

void drawScopes(void)
{
	scopesDisplayingFlag = true;
	int32_t chansPerRow = (uint32_t)song.numChannels >> 1;

	const uint16_t *scopeLens = scopeLenTab[chansPerRow-1];
	uint16_t scopeXOffs = 3;
	uint16_t scopeYOffs = 95;
	int16_t scopeLineY = 112;

	for (int32_t i = 0; i < song.numChannels; i++)
	{
		// if we reached the last scope on the row, go to first scope on the next row
		if (i == chansPerRow)
		{
			scopeXOffs = 3;
			scopeYOffs = 134;
			scopeLineY = 151;
		}

		const uint16_t scopeDrawLen = scopeLens[i];
		if (editor.channelMuted[i]) // scope muted (mute graphics blit()'ed elsewhere)
		{
			scopeXOffs += scopeDrawLen+3; // align x to next scope
			continue;
		}

		volatile scope_t s = scope[i]; // cache scope to lower thread race condition issues
		// VST3 scopes are not gated on the channel volume (empty instrument slots report 0)
		if (s.active && (s.vst3 || s.volume > 0) && !audio.locked)
		{
			// scope is active
			scope[i].wasCleared = false;

			// clear scope background
			clearRect(scopeXOffs, scopeYOffs, scopeDrawLen, SCOPE_HEIGHT);

			if (s.vst3)
			{
				// live VST3 instrument output
				drawVST3Scope((const scope_t *)&s, scopeXOffs, scopeDrawLen, scopeLineY);
			}
			else
			{
				// draw scope
				bool linedScopesFlag = !!(config.specialFlags & LINED_SCOPES);
				scopeDrawRoutineTable[(linedScopesFlag * 6) + (s.sample16Bit * 3) + s.loopType]((const scope_t *)&s, scopeXOffs, scopeLineY, scopeDrawLen);
			}
		}
		else
		{
			// scope is inactive
			volatile scope_t *sc = &scope[i];
			if (!sc->wasCleared)
			{
				// clear scope background
				clearRect(scopeXOffs, scopeYOffs, scopeDrawLen, SCOPE_HEIGHT);

				// draw empty line
				hLine(scopeXOffs, scopeLineY, scopeDrawLen, PAL_PATTEXT);

				sc->wasCleared = true;
			}
		}

		// draw channel numbering (if enabled)
		if (config.ptnChnNumbers)
			drawScopeNumber(scopeXOffs, scopeYOffs, (uint8_t)i, false);

		// draw rec. symbol (if enabled)
		if (config.multiRecChn[i])
			blit(scopeXOffs + 1, scopeYOffs + 31, bmp.scopeRec, 13, 4);

		scopeXOffs += scopeDrawLen+3; // align x to next scope
	}

	scopesDisplayingFlag = false;
}

void drawScopeFramework(void)
{
	drawFramework(0, 92, 291, 81, FRAMEWORK_TYPE1);
	for (int32_t i = 0; i < song.numChannels; i++)
		redrawScope(i);
}

void handleScopesFromChQueue(chSyncData_t *chSyncData, uint8_t *scopeUpdateStatus)
{
	volatile scope_t *sc = scope;

	syncedChannel_t *ch = chSyncData->channels;
	for (int32_t i = 0; i < song.numChannels; i++, sc++, ch++)
	{
		const uint8_t status = scopeUpdateStatus[i];

		if (status & CS_UPDATE_VOL)
			sc->volume = ch->scopeVolume;

		if (status & CF_UPDATE_PERIOD)
		{
			sc->delta = period2ScopeDelta(ch->period);
			sc->drawDelta = period2ScopeDrawDelta(ch->period);
		}

		if (status & CS_TRIGGER_VOICE)
		{
			sample_t *smp = NULL;
			if (instr[ch->instrNum] != NULL && ch->smpNum < MAX_SMP_PER_INST)
			{
				sample_t *s = &instr[ch->instrNum]->smp[ch->smpNum];
				if (s->dataPtr != NULL && s->length >= 1)
					smp = s;
			}

			if (smp != NULL)
			{
				scopeTrigger(i, smp, ch->smpStartPos);
				sc->vst3 = false;

				// set some stuff used by Smp. Ed. for sampling position line

				if (ch->instrNum == 130 || (ch->instrNum == editor.curInstr && ch->smpNum == editor.curSmp))
					editor.curSmpChannel = (uint8_t)i;

				lastChInstr[i].instrNum = ch->instrNum;
				lastChInstr[i].smpNum = ch->smpNum;
			}
			else if (vst3GetAssignedInstrument(ch->instrNum) >= 0)
			{
				// slot has no sample data, but it drives a VST3 instrument: show its live output
				sc->active = true;
				sc->vst3 = true;
				sc->vst3Instrument = ch->instrNum;
				sc->vst3SilentFrames = 0;
				sc->sampleEnd = 0;
				sc->wasCleared = false;

				// the plug-in output is shared by every channel that plays this
				// instrument, so only the channel that just keyed the note shows it
				vst3ScopeOwner[ch->instrNum] = (uint8_t)i;

				lastChInstr[i].instrNum = ch->instrNum;
				lastChInstr[i].smpNum = 255;
			}
			else
			{
				// empty instrument, shut down scope
				scope[i].active = false;
				sc->vst3 = false;
				lastChInstr[i].instrNum = 255;
				lastChInstr[i].smpNum = 255;
			}
		}
		else if (sc->vst3 && vst3ScopeOwner[sc->vst3Instrument] != (uint8_t)i)
		{
			// another channel took over the live plug-in output, hide it here
			sc->active = false;
			sc->vst3 = false;
		}
	}
}

static int32_t scopeThreadFunc(void *ptr)
{
	// this is confirmed to be needed for scope stability
	SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);

	hpc_SetDurationInHz(&scopeHpc, SCOPE_HZ);
	hpc_ResetCounters(&scopeHpc);

	while (editor.programRunning)
	{
		editor.scopeThreadBusy = true;
		updateScopes();
		editor.scopeThreadBusy = false;

		hpc_Wait(&scopeHpc);
	}

	(void)ptr;
	return true;
}

bool initScopes(void)
{
	memset(vst3ScopeOwner, 0xFF, sizeof (vst3ScopeOwner));

	scopeThread = SDL_CreateThread(scopeThreadFunc, "scope thread", NULL);
	if (scopeThread == NULL)
	{
		showErrorMsgBox("Couldn't create channel scope thread!");
		return false;
	}

	SDL_DetachThread(scopeThread);
	return true;
}
