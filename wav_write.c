/* wav_write.c: streaming mono PCM WAV output
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

#include "config.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "internals.h"
#include "wav_format.h"

enum { WAV_NUM_CHANNELS = 1 };

struct libspectrum_wav_writer {
  FILE *file;
  char *filename;
  size_t remaining;
  unsigned bytes, padding;
  libspectrum_error error;
  int owned;
#ifdef _WIN32
  int old_mode;
#endif
};

static libspectrum_error
wav_output_error( const char *filename, const char *operation )
{
  libspectrum_print_error( LIBSPECTRUM_ERROR_UNKNOWN, "WAV: %s: %s: %s",
                          filename, operation,
                          errno ? strerror( errno ) : "output failure" );
  return LIBSPECTRUM_ERROR_UNKNOWN;
}

/* Account for partial writes before retrying an interrupted operation.
   Other failures poison the writer; callers must close, not retry. */
static libspectrum_error
wav_write_all( libspectrum_wav_writer *writer, const void *buffer,
               size_t length, const char *operation )
{
  const libspectrum_byte *ptr = buffer;

  while( length ) {
    size_t written;

    errno = 0;
    written = fwrite( ptr, 1, length, writer->file );
    ptr += written;
    length -= written;
    if( ferror( writer->file ) ) {
      if( errno == EINTR ) {
        clearerr( writer->file );
        continue;
      }
      return wav_output_error( writer->filename, operation );
    }
    if( !written ) return wav_output_error( writer->filename, operation );
  }
  return LIBSPECTRUM_ERROR_NONE;
}

static libspectrum_error
wav_flush_output( FILE *file, const char *filename )
{
  /* A failed flush does not report how many buffered bytes were emitted;
     do not claim a successful retry when bytes may have been lost. */
  errno = 0;
  if( fflush( file ) )
    return wav_output_error( filename, "cannot flush output" );
  return LIBSPECTRUM_ERROR_NONE;
}

libspectrum_error
libspectrum_wav_writer_open( libspectrum_wav_writer **writer,
                             const char *filename, size_t rate,
                             libspectrum_wav_sample_format format,
                             size_t frames )
{
  libspectrum_wav_writer *state;
  libspectrum_byte header[WAV_HEADER_SIZE], *ptr = header;
  unsigned bytes;
  libspectrum_dword size;

  if( !writer ) return LIBSPECTRUM_ERROR_INVALID;
  *writer = NULL;
  if( format != LIBSPECTRUM_WAV_SAMPLE_UNSIGNED_8 &&
      format != LIBSPECTRUM_WAV_SAMPLE_SIGNED_16 )
    return LIBSPECTRUM_ERROR_INVALID;
  bytes = format == LIBSPECTRUM_WAV_SAMPLE_UNSIGNED_8 ? 1 : 2;
  if( !rate || rate > UINT32_MAX / bytes ||
      frames > ( UINT32_MAX - WAV_RIFF_OVERHEAD ) / bytes )
    return LIBSPECTRUM_ERROR_INVALID;
  size = (libspectrum_dword)frames * bytes;
  if( size > UINT32_MAX - WAV_RIFF_OVERHEAD - ( size & 1 ) )
    return LIBSPECTRUM_ERROR_INVALID;

  state = libspectrum_new0( libspectrum_wav_writer, 1 );
  state->owned = filename != NULL;
  state->filename = libspectrum_safe_strdup( filename ? filename : "stdout" );
  state->file = filename ? fopen( filename, "wb" ) : stdout;
  if( !state->file ) {
    wav_output_error( state->filename, "cannot open output" );
    libspectrum_free( state->filename );
    libspectrum_free( state );
    return LIBSPECTRUM_ERROR_UNKNOWN;
  }
#ifdef _WIN32
  state->old_mode = -1;
  if( !state->owned ) {
    /* Flush existing text before changing the shared stream's mode. */
    state->error = wav_flush_output( stdout, state->filename );
    if( !state->error &&
        ( state->old_mode = _setmode( _fileno( stdout ), _O_BINARY ) ) == -1 )
      state->error = wav_output_error( state->filename,
                                       "cannot prepare binary output" );
    if( state->error ) {
      libspectrum_free( state->filename );
      libspectrum_free( state );
      return LIBSPECTRUM_ERROR_UNKNOWN;
    }
  }
#endif
  state->remaining = frames;
  state->bytes = bytes;
  state->padding = size & 1;
  memcpy( ptr, WAV_SIGNATURE_RIFF, WAV_CHUNK_ID_SIZE );
  ptr += WAV_CHUNK_ID_SIZE;
  libspectrum_write_dword( &ptr, WAV_RIFF_OVERHEAD + size + state->padding );
  memcpy( ptr, WAV_SIGNATURE_WAVE, WAV_CHUNK_ID_SIZE );
  ptr += WAV_CHUNK_ID_SIZE;
  memcpy( ptr, WAV_CHUNK_FORMAT, WAV_CHUNK_ID_SIZE );
  ptr += WAV_CHUNK_ID_SIZE;
  libspectrum_write_dword( &ptr, WAV_PCM_FORMAT_SIZE );
  libspectrum_write_word( &ptr, WAV_FORMAT_PCM );
  libspectrum_write_word( &ptr, WAV_NUM_CHANNELS );
  libspectrum_write_dword( &ptr, rate );
  libspectrum_write_dword( &ptr, rate * bytes );
  libspectrum_write_word( &ptr, bytes );
  libspectrum_write_word( &ptr, bytes * WAV_BITS_PER_BYTE );
  memcpy( ptr, WAV_CHUNK_DATA, WAV_CHUNK_ID_SIZE );
  ptr += WAV_CHUNK_ID_SIZE;
  libspectrum_write_dword( &ptr, size );
  state->error = wav_write_all( state, header, sizeof( header ),
                                "cannot write header" );
  if( state->error ) {
    libspectrum_wav_writer_close( state );
    return LIBSPECTRUM_ERROR_UNKNOWN;
  }
  *writer = state;
  return LIBSPECTRUM_ERROR_NONE;
}

libspectrum_error
libspectrum_wav_writer_write( libspectrum_wav_writer *writer,
                              const void *samples, size_t frames )
{
  if( !writer ) return LIBSPECTRUM_ERROR_INVALID;
  if( writer->error ) return writer->error;
  if( frames > writer->remaining || ( frames && !samples ) ) {
    writer->error = LIBSPECTRUM_ERROR_INVALID;
    return writer->error;
  }
  if( !frames ) return LIBSPECTRUM_ERROR_NONE;
  if( writer->bytes == 1 ) {
    writer->error = wav_write_all( writer, samples, frames,
                                   "cannot write samples" );
  } else {
    const libspectrum_signed_word *input = samples;
    size_t i;

    for( i = 0; i < frames; i++ ) {
      libspectrum_byte raw[2], *ptr = raw;

      libspectrum_write_word( &ptr, (libspectrum_word)input[i] );
      writer->error = wav_write_all( writer, raw, sizeof( raw ),
                                     "cannot write samples" );
      if( writer->error ) break;
    }
  }
  if( writer->error ) return writer->error;
  writer->remaining -= frames;
  return LIBSPECTRUM_ERROR_NONE;
}

libspectrum_error
libspectrum_wav_writer_close( libspectrum_wav_writer *writer )
{
  libspectrum_error error;

  if( !writer ) return LIBSPECTRUM_ERROR_NONE;
  error = writer->error;
  if( !error && writer->remaining ) {
    libspectrum_print_error( LIBSPECTRUM_ERROR_INVALID,
                            "WAV: %s: fewer frames than declared",
                            writer->filename );
    error = LIBSPECTRUM_ERROR_INVALID;
  }
  if( !error && writer->padding ) {
    const libspectrum_byte padding = 0;

    error = wav_write_all( writer, &padding, 1, "cannot write padding" );
  }
  /* Flush separately: fclose releases the stream even on EINTR and cannot
     safely be retried. Still close owned streams after a failed flush. */
  {
    libspectrum_error flush_error =
      wav_flush_output( writer->file, writer->filename );
    if( flush_error ) error = flush_error;
  }
  if( writer->owned ) {
    if( fclose( writer->file ) )
      error = wav_output_error( writer->filename, "cannot close output" );
  } else {
#ifdef _WIN32
    if( writer->old_mode != -1 &&
        _setmode( _fileno( stdout ), writer->old_mode ) == -1 )
      error = wav_output_error( writer->filename, "cannot restore stdout mode" );
#endif
  }
  libspectrum_free( writer->filename );
  libspectrum_free( writer );
  return error;
}
