/* wav_format.h: shared RIFF/WAVE format constants
   Copyright (c) 2026 Fredrick Meunier

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.
*/

#ifndef LIBSPECTRUM_WAV_FORMAT_H
#define LIBSPECTRUM_WAV_FORMAT_H

#define WAV_SIGNATURE_RIFF "RIFF"
#define WAV_SIGNATURE_WAVE "WAVE"
#define WAV_CHUNK_FORMAT "fmt "
#define WAV_CHUNK_DATA "data"

enum {
  WAV_CHUNK_ID_SIZE = 4,
  WAV_CHUNK_HEADER_SIZE = 8,
  WAV_RIFF_HEADER_SIZE = 12,
  WAV_PCM_FORMAT_SIZE = 16,
  WAV_FORMAT_EX_SIZE = 18,       /* Base format plus cbSize. */
  WAV_EXTENSIBLE_EXTRA_SIZE = 22,
  WAV_EXTENSIBLE_FORMAT_SIZE = WAV_FORMAT_EX_SIZE + WAV_EXTENSIBLE_EXTRA_SIZE,
  /* Minimal PCM header: RIFF/WAVE, fmt chunk and data chunk header. */
  WAV_HEADER_SIZE = WAV_RIFF_HEADER_SIZE + 2 * WAV_CHUNK_HEADER_SIZE +
                    WAV_PCM_FORMAT_SIZE,
  WAV_RIFF_OVERHEAD = WAV_HEADER_SIZE - WAV_CHUNK_HEADER_SIZE,
  WAV_FORMAT_PCM = 0x0001,
  WAV_FORMAT_EXTENSIBLE = 0xfffe,
  WAV_BITS_PER_BYTE = 8
};

#endif
