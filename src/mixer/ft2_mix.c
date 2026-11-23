#include <stdint.h>
#include <stdbool.h>
#include "../ft2_header.h"
#include "ft2_mix.h"
#include "ft2_mix_macros.h"
#include "ft2_windowed_sinc.h"

/*
** ------------ 32-bit floating-point audio channel mixer ------------
**       (Note: Mixing macros can be found in ft2_mix_macros.h)
**
** Specifications:
** - Interpolation: None, 2-tap linear, 3-tap quadratic spline, 4-tap cubic spline, 8-tap/16-tap windowed-sinc
** - FT2-styled linear volume ramping (can be turned off)
** - 32.32 fixed-point precision for resampling delta/position
** - 32-bit floating-point precision for mixing and interpolation
**
** This file has separate routines for EVERY possible sampling variation:
** Interpolation type, volume ramp on/off, 8-bit/16-bit sample, loop type.
**
** Every voice has a function pointer set to the according mixing routine on
** sample trigger (from replayer, but set in audio thread), using a function
** pointer look-up table. All voices & pointers are always thread-safely cleared
** when changing any of the above attributes from the GUI, to prevent possible
** thread-related issues.
**
** -----------------------------------------------------------------------------
*/

/* ----------------------------------------------------------------------- */
/*                          8-BIT MIXING ROUTINES                          */
/* ----------------------------------------------------------------------- */

static void mix8bNoLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_BACK_MIXER_POS
}

static void mix8bLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP
			INC_POS
		}

		WRAP_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix8bPingpongLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *revBase, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8_PINGPONG

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		START_PINGPONG
		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP
			INC_POS_PINGPONG
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}
	SET_BACK_MIXER_POS
}

static void mix8bNoLoopS8Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_S8INTRP
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_BACK_MIXER_POS
}

static void mix8bLoopS8Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S8INTRP_TAP_FIX
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S8INTRP
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix8bPingpongLoopS8Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *revBase, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8_PINGPONG
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S8INTRP_TAP_FIX
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S8INTRP
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix8bNoLoopLIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_LINTRP
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_BACK_MIXER_POS
}

static void mix8bLoopLIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_LINTRP
			INC_POS
		}

		WRAP_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix8bPingpongLoopLIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *revBase, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8_PINGPONG

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		START_PINGPONG
		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_LINTRP
			INC_POS_PINGPONG
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix8bNoLoopS16Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_S16INTRP
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_BACK_MIXER_POS
}

static void mix8bLoopS16Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S16INTRP_TAP_FIX
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S16INTRP
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix8bPingpongLoopS16Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *revBase, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8_PINGPONG
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S16INTRP_TAP_FIX
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S16INTRP
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix8bNoLoopCIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_CINTRP
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_BACK_MIXER_POS
}

static void mix8bLoopCIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_CINTRP_TAP_FIX
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_CINTRP
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix8bPingpongLoopCIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *revBase, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8_PINGPONG
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_CINTRP_TAP_FIX
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_CINTRP
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix8bNoLoopQIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_QINTRP
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_BACK_MIXER_POS
}

static void mix8bLoopQIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_QINTRP_TAP_FIX
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_QINTRP
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix8bPingpongLoopQIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *revBase, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE8_PINGPONG
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_QINTRP_TAP_FIX
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_QINTRP
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix8bRampNoLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP
			VOLUME_RAMPING
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP
			VOLUME_RAMPING
			INC_POS
		}

		WRAP_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampPingpongLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *revBase, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8_PINGPONG

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		START_PINGPONG
		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP
			VOLUME_RAMPING
			INC_POS_PINGPONG
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampNoLoopS8Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_S8INTRP
			VOLUME_RAMPING
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampLoopS8Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S8INTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S8INTRP
				VOLUME_RAMPING
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampPingpongLoopS8Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *revBase, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8_PINGPONG
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S8INTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S8INTRP
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}
	
	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampNoLoopLIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_LINTRP
			VOLUME_RAMPING
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampLoopLIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_LINTRP
			VOLUME_RAMPING
			INC_POS
		}

		WRAP_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampPingpongLoopLIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *revBase, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8_PINGPONG

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		START_PINGPONG
		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_LINTRP
			VOLUME_RAMPING
			INC_POS_PINGPONG
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}
	
	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampNoLoopS16Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_S16INTRP
			VOLUME_RAMPING
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampLoopS16Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S16INTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S16INTRP
				VOLUME_RAMPING
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampPingpongLoopS16Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *revBase, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8_PINGPONG
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S16INTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_S16INTRP
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}
	
	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampNoLoopCIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_CINTRP
			VOLUME_RAMPING
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampLoopCIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_CINTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_CINTRP
				VOLUME_RAMPING
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampPingpongLoopCIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *revBase, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8_PINGPONG
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_CINTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_CINTRP
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}
	
	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampNoLoopQIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_8BIT_SMP_QINTRP
			VOLUME_RAMPING
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampLoopQIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_QINTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_QINTRP
				VOLUME_RAMPING
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix8bRampPingpongLoopQIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int8_t *base, *revBase, *smpPtr;
	int8_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE8_PINGPONG
	PREPARE_TAP_FIX8

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_QINTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_8BIT_SMP_QINTRP
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}
	
	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

/* ----------------------------------------------------------------------- */
/*                          16-BIT MIXING ROUTINES                         */
/* ----------------------------------------------------------------------- */

static void mix16bNoLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_BACK_MIXER_POS
}

static void mix16bLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP
			INC_POS
		}

		WRAP_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix16bPingpongLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *revBase, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16_PINGPONG

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		START_PINGPONG
		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP
			INC_POS_PINGPONG
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix16bNoLoopS8Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_S8INTRP
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_BACK_MIXER_POS
}

static void mix16bLoopS8Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S8INTRP_TAP_FIX
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S8INTRP
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix16bPingpongLoopS8Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *revBase, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16_PINGPONG
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S8INTRP_TAP_FIX
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S8INTRP
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix16bNoLoopLIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_LINTRP
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_BACK_MIXER_POS
}

static void mix16bLoopLIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_LINTRP
			INC_POS
		}

		WRAP_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix16bPingpongLoopLIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *revBase, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16_PINGPONG

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		START_PINGPONG
		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_LINTRP
			INC_POS_PINGPONG
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix16bNoLoopS16Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_S16INTRP
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_BACK_MIXER_POS
}

static void mix16bLoopS16Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S16INTRP_TAP_FIX
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S16INTRP
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix16bPingpongLoopS16Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *revBase, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16_PINGPONG
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S16INTRP_TAP_FIX
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S16INTRP
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix16bNoLoopCIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_CINTRP
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_BACK_MIXER_POS
}

static void mix16bLoopCIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_CINTRP_TAP_FIX
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_CINTRP
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix16bPingpongLoopCIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *revBase, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16_PINGPONG
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_CINTRP_TAP_FIX
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_CINTRP
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix16bNoLoopQIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_QINTRP
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_BACK_MIXER_POS
}

static void mix16bLoopQIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_QINTRP_TAP_FIX
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_QINTRP
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix16bPingpongLoopQIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *revBase, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL
	GET_MIXER_VARS
	SET_BASE16_PINGPONG
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_QINTRP_TAP_FIX
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_QINTRP
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_BACK_MIXER_POS
}

static void mix16bRampNoLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP
			VOLUME_RAMPING
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP
			VOLUME_RAMPING
			INC_POS
		}

		WRAP_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampPingpongLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *revBase, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16_PINGPONG

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		START_PINGPONG
		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP
			VOLUME_RAMPING
			INC_POS_PINGPONG
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampNoLoopS8Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_S8INTRP
			VOLUME_RAMPING
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampLoopS8Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S8INTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S8INTRP
				VOLUME_RAMPING
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampPingpongLoopS8Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *revBase, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16_PINGPONG
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S8INTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S8INTRP
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampNoLoopLIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_LINTRP
			VOLUME_RAMPING
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampLoopLIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_LINTRP
			VOLUME_RAMPING
			INC_POS
		}

		WRAP_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampPingpongLoopLIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *revBase, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16_PINGPONG

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		START_PINGPONG
		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_LINTRP
			VOLUME_RAMPING
			INC_POS_PINGPONG
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampNoLoopS16Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_S16INTRP
			VOLUME_RAMPING
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampLoopS16Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S16INTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S16INTRP
				VOLUME_RAMPING
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampPingpongLoopS16Intrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *revBase, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16_PINGPONG
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S16INTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_S16INTRP
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampNoLoopCIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_CINTRP
			VOLUME_RAMPING
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampLoopCIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_CINTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_CINTRP
				VOLUME_RAMPING
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampPingpongLoopCIntrp(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *revBase, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16_PINGPONG
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_CINTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_CINTRP
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampNoLoopQIntrp(voice_t* v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		for (i = 0; i < samplesToMix; i++)
		{
			RENDER_16BIT_SMP_QINTRP
			VOLUME_RAMPING
			INC_POS
		}

		HANDLE_SAMPLE_END
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampLoopQIntrp(voice_t* v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, *fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_QINTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_QINTRP
				VOLUME_RAMPING
				INC_POS
			}
		}

		WRAP_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

static void mix16bRampPingpongLoopQIntrp(voice_t* v, uint32_t bufferPos, uint32_t numSamples)
{
	const int16_t *base, *revBase, *smpPtr;
	int16_t *smpTapPtr;
	float fSample, * fMixBufferL, *fMixBufferR;
	int32_t position;
	float fVolumeLDelta, fVolumeRDelta, fVolumeL, fVolumeR;
	uint32_t i, samplesToMix, samplesLeft;
	uint64_t positionFrac, tmpDelta;

	GET_VOL_RAMP
	GET_MIXER_VARS_RAMP
	SET_BASE16_PINGPONG
	PREPARE_TAP_FIX16

	samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		LIMIT_MIX_NUM
		LIMIT_MIX_NUM_RAMP
		samplesLeft -= samplesToMix;

		START_PINGPONG
		if (v->hasLooped) // the negative interpolation taps need a special case after the sample has looped once
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_QINTRP_TAP_FIX
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		else
		{
			for (i = 0; i < samplesToMix; i++)
			{
				RENDER_16BIT_SMP_QINTRP
				VOLUME_RAMPING
				INC_POS_PINGPONG
			}
		}
		END_PINGPONG

		WRAP_PINGPONG_LOOP
	}

	SET_VOL_BACK
	SET_BACK_MIXER_POS
}

// -----------------------------------------------------------------------

/* ----------------------------------------------------------------------- */
/*                    INTERLEAVED STEREO MIXING ROUTINES                   */
/* ----------------------------------------------------------------------- */
/* Stereo samples store interleaved L/R frames (SAMPLE_STEREO), and their
** length/loop values are frame counts. Each channel is mixed to its own side,
** so only one routine per loop type is needed (interpolation is selected at
** runtime here, unlike the mono routines which are specialized per mode).
*/

static inline float stReadTap(const int8_t *p, int32_t frameOffset, int32_t ch, int32_t frameStride, bool is16)
{
	const int8_t *smp = p + (frameOffset * frameStride) + (ch * (is16 ? 2 : 1));

	if (is16)
		return (float)(*(const int16_t *)smp) * (1.0f / 32768.0f);

	return (float)(*smp) * (1.0f / 128.0f);
}

static inline float stSincTap(voice_t *v, const int8_t *tapPtr, uint32_t positionFrac, int32_t frameStride, bool is16, int32_t ch, int32_t numTaps, int32_t numTapsBits)
{
	const uint32_t phaseInt = positionFrac >> SINC_PHASE_SHIFT;
	const float phaseFrac = (int32_t)(positionFrac & SINC_PHASE_MASK) * (1.0f / SINC_PHASE_SCALE);
	const float *fSinc = v->fSincLUT + (phaseInt << numTapsBits);

	float fSum = 0.0f;
	for (int32_t tap = 0; tap < numTaps; tap++)
	{
		const float fY1 = fSinc[tap];
		const float fY2 = fSinc[numTaps+tap];
		fSum += stReadTap(tapPtr, tap-((numTaps/2)-1), ch, frameStride, is16) * (fY1 + ((fY2-fY1)*phaseFrac));
	}

	return fSum;
}

static inline void stRenderSample(voice_t *v, const int8_t *tapPtr, uint32_t positionFrac, int32_t frameStride, bool is16, float *outL, float *outR)
{
	const float x = (float)(positionFrac >> (MIXER_FRAC_BITS-31)) * (float)(1.0 / (INT32_MAX+1.0));

	float l1, l2, l3, l4;
	float r1, r2, r3, r4;
	float a, b, c, d;

	switch (audio.interpolationType)
	{
		case INTERPOLATION_SINC8:
			*outL = stSincTap(v, tapPtr, positionFrac, frameStride, is16, 0, SINC8_TAPS, SINC8_TAPS_BITS);
			*outR = stSincTap(v, tapPtr, positionFrac, frameStride, is16, 1, SINC8_TAPS, SINC8_TAPS_BITS);
			break;

		case INTERPOLATION_LINEAR:
			l1 = stReadTap(tapPtr, 0, 0, frameStride, is16);
			l2 = stReadTap(tapPtr, 1, 0, frameStride, is16);
			r1 = stReadTap(tapPtr, 0, 1, frameStride, is16);
			r2 = stReadTap(tapPtr, 1, 1, frameStride, is16);

			*outL = l1 + ((l2 - l1) * x);
			*outR = r1 + ((r2 - r1) * x);
			break;

		case INTERPOLATION_SINC16:
			*outL = stSincTap(v, tapPtr, positionFrac, frameStride, is16, 0, SINC16_TAPS, SINC16_TAPS_BITS);
			*outR = stSincTap(v, tapPtr, positionFrac, frameStride, is16, 1, SINC16_TAPS, SINC16_TAPS_BITS);
			break;

		case INTERPOLATION_CUBIC:
			l1 = stReadTap(tapPtr, -1, 0, frameStride, is16);
			l2 = stReadTap(tapPtr,  0, 0, frameStride, is16);
			l3 = stReadTap(tapPtr,  1, 0, frameStride, is16);
			l4 = stReadTap(tapPtr,  2, 0, frameStride, is16);

			r1 = stReadTap(tapPtr, -1, 1, frameStride, is16);
			r2 = stReadTap(tapPtr,  0, 1, frameStride, is16);
			r3 = stReadTap(tapPtr,  1, 1, frameStride, is16);
			r4 = stReadTap(tapPtr,  2, 1, frameStride, is16);

			a = ((l4 - l1) * 0.5f) + ((l2 - l3) * 1.5f);
			b = ((l1 - (l2 * 2.5f)) + (l3 * 2.0f)) - (l4 * 0.5f);
			c = (l3 - l1) * 0.5f;
			d = l2;
			*outL = (((a * x + b) * x + c) * x + d);

			a = ((r4 - r1) * 0.5f) + ((r2 - r3) * 1.5f);
			b = ((r1 - (r2 * 2.5f)) + (r3 * 2.0f)) - (r4 * 0.5f);
			c = (r3 - r1) * 0.5f;
			d = r2;
			*outR = (((a * x + b) * x + c) * x + d);
			break;

		case INTERPOLATION_QUADRATIC:
			l1 = stReadTap(tapPtr, -1, 0, frameStride, is16);
			l2 = stReadTap(tapPtr,  0, 0, frameStride, is16);
			l3 = stReadTap(tapPtr,  1, 0, frameStride, is16);

			r1 = stReadTap(tapPtr, -1, 1, frameStride, is16);
			r2 = stReadTap(tapPtr,  0, 1, frameStride, is16);
			r3 = stReadTap(tapPtr,  1, 1, frameStride, is16);

			a = ((l1 + l3) * 0.5f) - l2;
			b = (l3 - l1) * 0.5f;
			c = l2;
			*outL = ((a * x + b) * x + c);

			a = ((r1 + r3) * 0.5f) - r2;
			b = (r3 - r1) * 0.5f;
			c = r2;
			*outR = ((a * x + b) * x + c);
			break;

		default: // INTERPOLATION_DISABLED
			*outL = stReadTap(tapPtr, 0, 0, frameStride, is16);
			*outR = stReadTap(tapPtr, 0, 1, frameStride, is16);
			break;
	}
}

static void mixStereoVoiceNoLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const bool is16 = v->is16Bit;
	const int32_t frameStride = is16 ? 4 : 2;
	const bool ramp = (v->volumeRampLength > 0);
	const uint64_t delta = v->delta;

	const int8_t *base = (const int8_t *)(is16 ? (const void *)v->base16 : (const void *)v->base8);
	const int8_t *loopStartPtr = base + ((int64_t)v->loopStart * frameStride);
	const int8_t *leftEdgePtr = loopStartPtr + (MAX_LEFT_TAPS * frameStride);
	const int8_t *smpPtr = base + ((int64_t)v->position * frameStride);

	float *fMixBufferL = audio.fMixBufferL + bufferPos;
	float *fMixBufferR = audio.fMixBufferR + bufferPos;

	float fVolumeL = v->fCurrVolumeL;
	float fVolumeR = v->fCurrVolumeR;
	const float fVolumeLDelta = ramp ? v->fVolumeLDelta : 0.0f;
	const float fVolumeRDelta = ramp ? v->fVolumeRDelta : 0.0f;

	uint64_t positionFrac = v->positionFrac;
	int32_t position = v->position;

	uint32_t samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		uint32_t samplesToMix = INT32_MAX;
		if (delta != 0)
		{
			const int64_t i = (v->sampleEnd-1) - position;
			const uint64_t dividend = ((uint64_t)i << MIXER_FRAC_BITS) | ((uint32_t)positionFrac ^ MIXER_FRAC_MASK);
			samplesToMix = (uint32_t)(dividend / delta) + 1;
		}

		if (ramp && samplesToMix > v->volumeRampLength)
			samplesToMix = v->volumeRampLength;

		if (samplesToMix > samplesLeft)
			samplesToMix = samplesLeft;

		if (samplesToMix == 0) // volume ramp finished in the middle of the block
			break;

		if (ramp)
			v->volumeRampLength -= samplesToMix;

		for (uint32_t i = 0; i < samplesToMix; i++)
		{
			const int8_t *tapPtr = smpPtr;
			if (v->hasLooped && smpPtr <= leftEdgePtr && audio.interpolationType != INTERPOLATION_DISABLED)
				tapPtr = (is16 ? (const int8_t *)v->leftEdgeTaps16 : (const int8_t *)v->leftEdgeTaps8) + (smpPtr - loopStartPtr);

			float fSampleL, fSampleR;
			stRenderSample(v, tapPtr, (uint32_t)positionFrac, frameStride, is16, &fSampleL, &fSampleR);

			*fMixBufferL++ += fSampleL * fVolumeL;
			*fMixBufferR++ += fSampleR * fVolumeR;

			fVolumeL += fVolumeLDelta;
			fVolumeR += fVolumeRDelta;

			positionFrac += delta;
			smpPtr += ((int64_t)(positionFrac >> MIXER_FRAC_BITS)) * frameStride;
			positionFrac &= MIXER_FRAC_MASK;
		}

		samplesLeft -= samplesToMix;

		position = (int32_t)((smpPtr - base) / frameStride);
		if (position >= v->sampleEnd)
		{
			v->active = false;
			break;
		}
	}

	v->positionFrac = positionFrac;
	v->position = position;
	v->fCurrVolumeL = fVolumeL;
	v->fCurrVolumeR = fVolumeR;
}

static void mixStereoVoiceLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const bool is16 = v->is16Bit;
	const int32_t frameStride = is16 ? 4 : 2;
	const bool ramp = (v->volumeRampLength > 0);
	const uint64_t delta = v->delta;

	const int8_t *base = (const int8_t *)(is16 ? (const void *)v->base16 : (const void *)v->base8);
	const int8_t *loopStartPtr = base + ((int64_t)v->loopStart * frameStride);
	const int8_t *leftEdgePtr = loopStartPtr + (MAX_LEFT_TAPS * frameStride);
	const int8_t *smpPtr = base + ((int64_t)v->position * frameStride);

	float *fMixBufferL = audio.fMixBufferL + bufferPos;
	float *fMixBufferR = audio.fMixBufferR + bufferPos;

	float fVolumeL = v->fCurrVolumeL;
	float fVolumeR = v->fCurrVolumeR;
	const float fVolumeLDelta = ramp ? v->fVolumeLDelta : 0.0f;
	const float fVolumeRDelta = ramp ? v->fVolumeRDelta : 0.0f;

	uint64_t positionFrac = v->positionFrac;
	int32_t position = v->position;

	uint32_t samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		uint32_t samplesToMix = INT32_MAX;
		if (delta != 0)
		{
			const int64_t i = (v->sampleEnd-1) - position;
			const uint64_t dividend = ((uint64_t)i << MIXER_FRAC_BITS) | ((uint32_t)positionFrac ^ MIXER_FRAC_MASK);
			samplesToMix = (uint32_t)(dividend / delta) + 1;
		}

		if (ramp && samplesToMix > v->volumeRampLength)
			samplesToMix = v->volumeRampLength;

		if (samplesToMix > samplesLeft)
			samplesToMix = samplesLeft;

		if (samplesToMix == 0)
			break;

		if (ramp)
			v->volumeRampLength -= samplesToMix;

		for (uint32_t i = 0; i < samplesToMix; i++)
		{
			const int8_t *tapPtr = smpPtr;
			if (v->hasLooped && smpPtr <= leftEdgePtr && audio.interpolationType != INTERPOLATION_DISABLED)
				tapPtr = (is16 ? (const int8_t *)v->leftEdgeTaps16 : (const int8_t *)v->leftEdgeTaps8) + (smpPtr - loopStartPtr);

			float fSampleL, fSampleR;
			stRenderSample(v, tapPtr, (uint32_t)positionFrac, frameStride, is16, &fSampleL, &fSampleR);

			*fMixBufferL++ += fSampleL * fVolumeL;
			*fMixBufferR++ += fSampleR * fVolumeR;

			fVolumeL += fVolumeLDelta;
			fVolumeR += fVolumeRDelta;

			positionFrac += delta;
			smpPtr += ((int64_t)(positionFrac >> MIXER_FRAC_BITS)) * frameStride;
			positionFrac &= MIXER_FRAC_MASK;
		}

		samplesLeft -= samplesToMix;

		position = (int32_t)((smpPtr - base) / frameStride);
		if (position >= v->sampleEnd)
		{
			do
			{
				position -= v->loopLength;
			}
			while (position >= v->sampleEnd);

			smpPtr = base + ((int64_t)position * frameStride);
			v->hasLooped = true;
		}
	}

	v->positionFrac = positionFrac;
	v->position = position;
	v->fCurrVolumeL = fVolumeL;
	v->fCurrVolumeR = fVolumeR;
}

static void mixStereoVoicePingpongLoop(voice_t *v, uint32_t bufferPos, uint32_t numSamples)
{
	const bool is16 = v->is16Bit;
	const int32_t frameStride = is16 ? 4 : 2;
	const bool ramp = (v->volumeRampLength > 0);
	const uint64_t delta = v->delta;

	const int8_t *base = (const int8_t *)(is16 ? (const void *)v->base16 : (const void *)v->base8);
	const int8_t *revBase = (const int8_t *)(is16 ? (const void *)v->revBase16 : (const void *)v->revBase8);
	const int8_t *loopStartPtr = base + ((int64_t)v->loopStart * frameStride);
	const int8_t *leftEdgePtr = loopStartPtr + (MAX_LEFT_TAPS * frameStride);
	const int8_t *smpPtr = base + ((int64_t)v->position * frameStride);

	float *fMixBufferL = audio.fMixBufferL + bufferPos;
	float *fMixBufferR = audio.fMixBufferR + bufferPos;

	float fVolumeL = v->fCurrVolumeL;
	float fVolumeR = v->fCurrVolumeR;
	const float fVolumeLDelta = ramp ? v->fVolumeLDelta : 0.0f;
	const float fVolumeRDelta = ramp ? v->fVolumeRDelta : 0.0f;

	uint64_t positionFrac = v->positionFrac;
	int32_t position = v->position;

	uint32_t samplesLeft = numSamples;
	while (samplesLeft > 0)
	{
		uint32_t samplesToMix = INT32_MAX;
		if (delta != 0)
		{
			const int64_t i = (v->sampleEnd-1) - position;
			const uint64_t dividend = ((uint64_t)i << MIXER_FRAC_BITS) | ((uint32_t)positionFrac ^ MIXER_FRAC_MASK);
			samplesToMix = (uint32_t)(dividend / delta) + 1;
		}

		if (ramp && samplesToMix > v->volumeRampLength)
			samplesToMix = v->volumeRampLength;

		if (samplesToMix > samplesLeft)
			samplesToMix = samplesLeft;

		if (samplesToMix == 0)
			break;

		if (ramp)
			v->volumeRampLength -= samplesToMix;

		uint64_t tmpDelta;
		if (v->samplingBackwards)
		{
			tmpDelta = (uint64_t)0 - delta;
			position = ~position;
			smpPtr = revBase + ((int64_t)position * frameStride);
			positionFrac ^= MIXER_FRAC_MASK;
		}
		else
		{
			tmpDelta = delta;
			smpPtr = base + ((int64_t)position * frameStride);
		}

		const int64_t deltaHi = (int64_t)tmpDelta >> MIXER_FRAC_BITS;
		const uint32_t deltaLo = (uint32_t)tmpDelta & MIXER_FRAC_MASK;

		for (uint32_t i = 0; i < samplesToMix; i++)
		{
			const int8_t *tapPtr = smpPtr;
			if (v->hasLooped && smpPtr <= leftEdgePtr && audio.interpolationType != INTERPOLATION_DISABLED)
				tapPtr = (is16 ? (const int8_t *)v->leftEdgeTaps16 : (const int8_t *)v->leftEdgeTaps8) + (smpPtr - loopStartPtr);

			float fSampleL, fSampleR;
			stRenderSample(v, tapPtr, (uint32_t)positionFrac, frameStride, is16, &fSampleL, &fSampleR);

			*fMixBufferL++ += fSampleL * fVolumeL;
			*fMixBufferR++ += fSampleR * fVolumeR;

			fVolumeL += fVolumeLDelta;
			fVolumeR += fVolumeRDelta;

			positionFrac += deltaLo;
			smpPtr += ((int64_t)(positionFrac >> MIXER_FRAC_BITS)) * frameStride;
			smpPtr += deltaHi * frameStride;
			positionFrac &= MIXER_FRAC_MASK;
		}

		samplesLeft -= samplesToMix;

		if (v->samplingBackwards)
		{
			positionFrac ^= MIXER_FRAC_MASK;
			position = ~(int32_t)((smpPtr - revBase) / frameStride);
		}
		else
		{
			position = (int32_t)((smpPtr - base) / frameStride);
		}

		if (position >= v->sampleEnd)
		{
			do
			{
				position -= v->loopLength;
				v->samplingBackwards ^= 1;
			}
			while (position >= v->sampleEnd);

			v->hasLooped = true;
		}
	}

	v->positionFrac = positionFrac;
	v->position = position;
	v->fCurrVolumeL = fVolumeL;
	v->fCurrVolumeR = fVolumeR;
}

void mixStereoVoice(void *v, uint32_t bufferPos, uint32_t numSamples)
{
	voice_t *voice = (voice_t *)v;

	switch (voice->loopType)
	{
		case LOOP_FORWARD: mixStereoVoiceLoop(voice, bufferPos, numSamples); break;
		case LOOP_PINGPONG: mixStereoVoicePingpongLoop(voice, bufferPos, numSamples); break;
		default: mixStereoVoiceNoLoop(voice, bufferPos, numSamples); break;
	}
}

const mixFunc mixFuncTab[] =
{
	// no volume ramping

	// 8-bit
	(mixFunc)mix8bNoLoop,
	(mixFunc)mix8bLoop,
	(mixFunc)mix8bPingpongLoop,
	(mixFunc)mix8bNoLoopS8Intrp,
	(mixFunc)mix8bLoopS8Intrp,
	(mixFunc)mix8bPingpongLoopS8Intrp,
	(mixFunc)mix8bNoLoopLIntrp,
	(mixFunc)mix8bLoopLIntrp,
	(mixFunc)mix8bPingpongLoopLIntrp,
	(mixFunc)mix8bNoLoopS16Intrp,
	(mixFunc)mix8bLoopS16Intrp,
	(mixFunc)mix8bPingpongLoopS16Intrp,
	(mixFunc)mix8bNoLoopCIntrp,
	(mixFunc)mix8bLoopCIntrp,
	(mixFunc)mix8bPingpongLoopCIntrp,
	(mixFunc)mix8bNoLoopQIntrp,
	(mixFunc)mix8bLoopQIntrp,
	(mixFunc)mix8bPingpongLoopQIntrp,

	// 16-bit
	(mixFunc)mix16bNoLoop,
	(mixFunc)mix16bLoop,
	(mixFunc)mix16bPingpongLoop,
	(mixFunc)mix16bNoLoopS8Intrp,
	(mixFunc)mix16bLoopS8Intrp,
	(mixFunc)mix16bPingpongLoopS8Intrp,
	(mixFunc)mix16bNoLoopLIntrp,
	(mixFunc)mix16bLoopLIntrp,
	(mixFunc)mix16bPingpongLoopLIntrp,
	(mixFunc)mix16bNoLoopS16Intrp,
	(mixFunc)mix16bLoopS16Intrp,
	(mixFunc)mix16bPingpongLoopS16Intrp,
	(mixFunc)mix16bNoLoopCIntrp,
	(mixFunc)mix16bLoopCIntrp,
	(mixFunc)mix16bPingpongLoopCIntrp,
	(mixFunc)mix16bNoLoopQIntrp,
	(mixFunc)mix16bLoopQIntrp,
	(mixFunc)mix16bPingpongLoopQIntrp,

	// volume ramping

	// 8-bit
	(mixFunc)mix8bRampNoLoop,
	(mixFunc)mix8bRampLoop,
	(mixFunc)mix8bRampPingpongLoop,
	(mixFunc)mix8bRampNoLoopS8Intrp,
	(mixFunc)mix8bRampLoopS8Intrp,
	(mixFunc)mix8bRampPingpongLoopS8Intrp,
	(mixFunc)mix8bRampNoLoopLIntrp,
	(mixFunc)mix8bRampLoopLIntrp,
	(mixFunc)mix8bRampPingpongLoopLIntrp,
	(mixFunc)mix8bRampNoLoopS16Intrp,
	(mixFunc)mix8bRampLoopS16Intrp,
	(mixFunc)mix8bRampPingpongLoopS16Intrp,
	(mixFunc)mix8bRampNoLoopCIntrp,
	(mixFunc)mix8bRampLoopCIntrp,
	(mixFunc)mix8bRampPingpongLoopCIntrp,
	(mixFunc)mix8bRampNoLoopQIntrp,
	(mixFunc)mix8bRampLoopQIntrp,
	(mixFunc)mix8bRampPingpongLoopQIntrp,

	// 16-bit
	(mixFunc)mix16bRampNoLoop,
	(mixFunc)mix16bRampLoop,
	(mixFunc)mix16bRampPingpongLoop,
	(mixFunc)mix16bRampNoLoopS8Intrp,
	(mixFunc)mix16bRampLoopS8Intrp,
	(mixFunc)mix16bRampPingpongLoopS8Intrp,
	(mixFunc)mix16bRampNoLoopLIntrp,
	(mixFunc)mix16bRampLoopLIntrp,
	(mixFunc)mix16bRampPingpongLoopLIntrp,
	(mixFunc)mix16bRampNoLoopS16Intrp,
	(mixFunc)mix16bRampLoopS16Intrp,
	(mixFunc)mix16bRampPingpongLoopS16Intrp,
	(mixFunc)mix16bRampNoLoopCIntrp,
	(mixFunc)mix16bRampLoopCIntrp,
	(mixFunc)mix16bRampPingpongLoopCIntrp,
	(mixFunc)mix16bRampNoLoopQIntrp,
	(mixFunc)mix16bRampLoopQIntrp,
	(mixFunc)mix16bRampPingpongLoopQIntrp
};
