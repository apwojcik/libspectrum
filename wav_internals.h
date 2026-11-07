#ifndef LIBSPECTRUM_WAV_INTERNALS_H
#define LIBSPECTRUM_WAV_INTERNALS_H

#include "libspectrum.h"

#ifdef WAV_INTERNAL_MACOS
typedef struct internal_wav_reader internal_wav_reader;
#else
typedef libspectrum_wav_reader internal_wav_reader;
#endif

/* open returns the original sample rate and frame count; the caller owns the
   reader until close. read produces mono signed samples (positive is high,
   zero/negative low), reports short reads at EOF, and never resamples. */

libspectrum_error
internal_wav_open( internal_wav_reader **reader, const char *filename,
                   size_t *sample_rate, size_t *frame_count );

libspectrum_error
internal_wav_read( internal_wav_reader *reader,
                   libspectrum_signed_word *buffer, size_t frames,
                   size_t *frames_read );

void
internal_wav_close( internal_wav_reader *reader );

#endif
