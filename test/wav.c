/* Portable WAV reader regression tests. Fixtures are generated in memory.
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

#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#endif

#include "test.h"
#include "wav_internals.h"

static void
put32( unsigned char *p, unsigned long n )
{
  unsigned i;
  for( i = 0; i < 4; i++ ) p[i] = ( n >> ( 8 * i ) ) & 255;
}

static void
put16( unsigned char *p, unsigned n )
{
  p[0] = n & 255; p[1] = n >> 8;
}

/* fmt follows data to exercise order independence. Include odd metadata. */
static size_t
fixture( unsigned char *p, unsigned bytes, unsigned channels, int extensible )
{
  unsigned n = 3 * bytes * channels, i, c;
  size_t f = 30 + n + ( n & 1 ), total = f + ( extensible ? 48 : 24 );
  memset( p, 0, 256 );
  memcpy( p, "RIFF", 4 ); put32( p + 4, total - 8 );
  memcpy( p + 8, "WAVEJUNK", 8 ); put32( p + 16, 1 ); p[20] = 42;
  memcpy( p + 22, "data", 4 ); put32( p + 26, n );
  for( i = 0; i < 3; i++ ) {
    for( c = 0; c < channels; c++ ) {
      unsigned char *s = p + 30 + ( i * channels + c ) * bytes;
      if( bytes == 1 ) s[0] = i == 0 ? 0 : i == 1 ? 128 : 255;
      else s[bytes - 1] = i == 0 ? 128 : i == 1 ? 0 : 64;
    }
  }
  memcpy( p + f, "fmt ", 4 ); put32( p + f + 4, extensible ? 40 : 16 );
  put16( p + f + 8, extensible ? 0xfffe : 1 );
  put16( p + f + 10, channels ); put32( p + f + 12, 12345 );
  put32( p + f + 16, 12345 * channels * bytes );
  put16( p + f + 20, channels * bytes ); put16( p + f + 22, bytes * 8 );
  if( extensible ) {
    static const unsigned char guid[16] = {
      1, 0, 0, 0, 0, 0, 16, 0, 128, 0, 0, 170, 0, 56, 155, 113
    };
    put16( p + f + 24, 22 ); put16( p + f + 26, bytes * 8 );
    memcpy( p + f + 32, guid, 16 );
  }
  return total;
}

static int
try_fixture( const unsigned char *p, size_t size,
             libspectrum_error expected_error, int empty, unsigned bytes )
{
  const char *name = "test-wav-reader.tmp";
  FILE *file = fopen( name, "wb" );
  libspectrum_wav_reader *reader = NULL;
  libspectrum_signed_word samples[4];
  libspectrum_error error;
  size_t rate, count, got;
  int ok;

  if( !file ) return 0;
  ok = fwrite( p, 1, size, file ) == size;
  if( fclose( file ) ) ok = 0;
  if( !ok ) { remove( name ); return 0; }
  error = libspectrum_wav_reader_open( &reader, name, &rate, &count );
  ok = error == expected_error;
  if( !ok )
    fprintf( stderr, "WAV fixture: expected error %d, got %d\n",
             (int)expected_error, (int)error );
  if( !expected_error && !error ) {
    ok = rate == 12345 && count == ( empty ? 0 : 3 );
    if( libspectrum_wav_reader_read( reader, samples, 1, &got ) ||
        got != ( empty ? 0 : 1 ) ) ok = 0;
    if( !empty && samples[0] != -32768 ) ok = 0;
    if( libspectrum_wav_reader_read( reader, samples, 4, &got ) ||
        got != ( empty ? 0 : 2 ) ) ok = 0;
    if( !empty && ( samples[0] != 0 ||
        samples[1] != ( bytes == 1 ? 32512 : 16384 ) ) ) ok = 0;
    if( libspectrum_wav_reader_read( reader, samples, 1, &got ) || got ) ok = 0;
  }
  if( libspectrum_wav_reader_close( reader ) ) ok = 0;
  remove( name );
  return ok;
}

test_return_t
read_portable_wav_formats( void )
{
  unsigned char p[256];
  unsigned bytes, channels, ext, mutation;
  size_t size, f, cut;
  libspectrum_error expected_error;

  for( bytes = 1; bytes <= 4; bytes++ )
    for( channels = 1; channels <= 3; channels++ )
      for( ext = 0; ext < 2; ext++ ) {
        size = fixture( p, bytes, channels, ext );
        if( !try_fixture( p, size, LIBSPECTRUM_ERROR_NONE, 0, bytes ) )
          return TEST_FAIL;
        /* Redundant byte rate may be zero or incorrectly expressed in bits. */
        for( mutation = 0; mutation < 2; mutation++ ) {
          f = 30 + 3 * bytes * channels + ( ( 3 * bytes * channels ) & 1 );
          put32( p + f + 16,
                 mutation ? 12345 * channels * bytes * 8 : 0 );
          if( !try_fixture( p, size, LIBSPECTRUM_ERROR_NONE, 0, bytes ) )
            return TEST_FAIL;
        }
      }

  /* Equal positive/negative channels cancel, including at full scale. */
  for( bytes = 1; bytes <= 4; bytes++ ) {
    size = fixture( p, bytes, 2, 0 );
    memset( p + 30, 0, 6 * bytes );
    if( bytes == 1 ) {
      p[30] = 0; p[31] = 0; /* Initial negative frame. */
      p[32] = 64; p[33] = 192; /* Exact cancellation. */
      p[34] = 255; p[35] = 255;
    } else {
      p[30 + bytes - 1] = p[30 + 2 * bytes - 1] = 128;
      p[30 + 3 * bytes - 1] = 192;
      p[30 + 4 * bytes - 1] = 64;
      p[30 + 5 * bytes - 1] = p[30 + 6 * bytes - 1] = 64;
    }
    if( !try_fixture( p, size, LIBSPECTRUM_ERROR_NONE, 0, bytes ) )
      return TEST_FAIL;
  }

  size = fixture( p, 2, 1, 1 );
  for( cut = 0; cut < size; cut++ )
    if( !try_fixture( p, cut, LIBSPECTRUM_ERROR_CORRUPT, 0, 2 ) )
      return TEST_FAIL;

  /* Header, boundaries, padding, format, alignment and extensible validation. */
  for( mutation = 0; mutation < 23; mutation++ ) {
    size = fixture( p, 2, 1, 1 ); f = 36;
    expected_error = LIBSPECTRUM_ERROR_CORRUPT;
    switch( mutation ) {
    case 0: p[0] = 'X'; expected_error = LIBSPECTRUM_ERROR_SIGNATURE; break;
    case 1: p[8] = 'X'; expected_error = LIBSPECTRUM_ERROR_SIGNATURE; break;
    case 2: put32( p + 4, 0xffffffffUL ); break;
    case 3: size--; break;
    case 4: put32( p + 26, 5 ); break;
    case 5: put32( p + 16, 0xffffffffUL ); break;
    case 6: /* IEEE float deliberately unsupported. */
      put16( p + f + 8, 3 ); expected_error = LIBSPECTRUM_ERROR_UNKNOWN; break;
    case 7: put16( p + f + 10, 0 ); break;
    case 8: put32( p + f + 12, 0 ); break;
    case 9: put16( p + f + 20, 3 ); break;
    case 10: /* Reduced valid bits: well-formed but unsupported. */
      put16( p + f + 26, 12 ); expected_error = LIBSPECTRUM_ERROR_UNKNOWN; break;
    case 11: /* Non-PCM extensible subformat. */
      p[f + 32] = 3; expected_error = LIBSPECTRUM_ERROR_UNKNOWN; break;
    case 12: /* Incorrect byte rate does not affect PCM decoding. */
      put32( p + f + 16, 1 ); expected_error = LIBSPECTRUM_ERROR_NONE; break;
    case 13: memcpy( p + 22, "JUNK", 4 ); break; /* Missing data. */
    case 14: memcpy( p + f, "JUNK", 4 ); break; /* Missing fmt. */
    case 15: put32( p + f + 4, 15 ); break;
    case 16: /* Duplicate data chunk, even when empty. */
      memcpy( p + size, "data", 4 ); put32( p + size + 4, 0 );
      size += 8; put32( p + 4, size - 8 ); break;
    case 17: /* Duplicate format chunk. */
      memcpy( p + size, p + f, 48 ); size += 48;
      put32( p + 4, size - 8 ); break;
    case 18: /* Unsupported container width. */
      put16( p + f + 22, 12 ); put16( p + f + 26, 12 );
      expected_error = LIBSPECTRUM_ERROR_UNKNOWN; break;
    case 19: put16( p + f + 26, 17 ); break; /* Too many valid bits. */
    case 20: put16( p + f + 26, 0 ); break;
    case 21: put16( p + f + 24, 21 ); break; /* Short extension. */
    case 22: put16( p + f + 24, 23 ); break; /* Extension exceeds fmt size. */
    }
    if( !try_fixture( p, size, expected_error, 0, 2 ) ) return TEST_FAIL;
  }
  size = fixture( p, 1, 1, 0 );
  /* Remove the final odd data padding by shortening the RIFF envelope. */
  put32( p + 4, size - 9 );
  if( !try_fixture( p, size, LIBSPECTRUM_ERROR_CORRUPT, 0, 1 ) ) return TEST_FAIL;

  size = fixture( p, 2, 1, 0 );
  memmove( p + 30, p + 36, size - 36 ); size -= 6;
  put32( p + 4, size - 8 ); put32( p + 26, 0 );
  if( !try_fixture( p, size, LIBSPECTRUM_ERROR_NONE, 1, 2 ) ) return TEST_FAIL;
  return TEST_PASS;
}

/* A pipe proves stdout output does not depend on seeking. Restore stdout
   before returning so test diagnostics cannot corrupt the captured bytes. */
static int
wav_stdout_test( void )
{
#ifdef _WIN32
  return 1; /* Descriptor redirection below is POSIX-only. */
#else
  int fds[2], saved, ok = 1;
  unsigned char bytes[48], sample = 128;
  ssize_t count;
  libspectrum_wav_writer *writer = NULL;
  if( fflush( stdout ) || pipe( fds ) ) return 0;
  saved = dup( STDOUT_FILENO );
  if( saved < 0 ) { close( fds[0] ); close( fds[1] ); return 0; }
  if( dup2( fds[1], STDOUT_FILENO ) < 0 ) {
    close( saved ); close( fds[0] ); close( fds[1] ); return 0;
  }
  close( fds[1] );
  if( libspectrum_wav_writer_open( &writer, NULL, 8000,
        LIBSPECTRUM_WAV_SAMPLE_UNSIGNED_8, 1 ) ) ok = 0;
  if( ok && libspectrum_wav_writer_write( writer, &sample, 1 ) ) ok = 0;
  if( libspectrum_wav_writer_close( writer ) ) ok = 0;
  /* Writer must not have closed stdout. */
  {
    int fd = dup( STDOUT_FILENO );
    if( fd < 0 ) ok = 0;
    else close( fd );
  }
  if( dup2( saved, STDOUT_FILENO ) < 0 ) ok = 0;
  close( saved );
  count = read( fds[0], bytes, sizeof( bytes ) );
  close( fds[0] );
  if( count != 46 || memcmp( bytes, "RIFF", 4 ) ||
      bytes[4] != 38 || bytes[40] != 1 || bytes[44] != 128 || bytes[45] )
    ok = 0;
  /* A read-only descriptor forces output failure without SIGPIPE. The
     failure may surface at open, write, or the final stdout flush. */
  if( pipe( fds ) ) return 0;
  saved = dup( STDOUT_FILENO );
  if( saved < 0 ) { close( fds[0] ); close( fds[1] ); return 0; }
  if( dup2( fds[0], STDOUT_FILENO ) < 0 ) {
    close( saved ); close( fds[0] ); close( fds[1] ); return 0;
  }
  close( fds[0] ); close( fds[1] );
  {
    libspectrum_error error = libspectrum_wav_writer_open( &writer, NULL,
      8000, LIBSPECTRUM_WAV_SAMPLE_UNSIGNED_8, 1 );
    if( !error ) {
      libspectrum_error write_error =
        libspectrum_wav_writer_write( writer, &sample, 1 );
      libspectrum_error close_error = libspectrum_wav_writer_close( writer );
      if( ( write_error && write_error != LIBSPECTRUM_ERROR_UNKNOWN ) ||
          close_error != LIBSPECTRUM_ERROR_UNKNOWN ) ok = 0;
    } else if( error != LIBSPECTRUM_ERROR_UNKNOWN || writer ) ok = 0;
  }
  /* Some stdio implementations retain buffered bytes after failed flush.
     Discard them through /dev/null before restoring the test log. */
  {
    int sink = open( "/dev/null", O_WRONLY );
    if( sink < 0 ) ok = 0;
    else {
      if( dup2( sink, STDOUT_FILENO ) < 0 ) ok = 0;
      clearerr( stdout );
      if( fflush( stdout ) ) ok = 0;
      close( sink );
    }
  }
  if( dup2( saved, STDOUT_FILENO ) < 0 ) ok = 0;
  close( saved );
  clearerr( stdout );
  /* /dev/full is available on Linux, but not required by other hosts. */
  if( access( "/dev/full", W_OK ) == 0 ) {
    libspectrum_error error = libspectrum_wav_writer_open( &writer,
      "/dev/full", 8000, LIBSPECTRUM_WAV_SAMPLE_UNSIGNED_8, 0 );
    if( !error ) {
      if( libspectrum_wav_writer_close( writer ) != LIBSPECTRUM_ERROR_UNKNOWN )
        ok = 0;
    } else if( error != LIBSPECTRUM_ERROR_UNKNOWN || writer ) ok = 0;
  }
  return ok;
#endif
}

/* Byte-level output checks, public-reader round trips and failure states. */
test_return_t
write_pcm_wav( void )
{
  const char *name = "test-wav-writer.tmp";
  libspectrum_wav_writer *writer = NULL;
  libspectrum_wav_reader *reader = NULL;
  libspectrum_signed_word input[3] = { -32768, 0, 32767 }, output[4];
  unsigned char u8[3] = { 0, 128, 255 }, actual[64], expected[50];
  size_t rate, count, got, n;
  unsigned format, frames;
  FILE *file;
  int ok = wav_stdout_test();

  for( format = 0; format < 2; format++ ) {
    for( frames = 0; frames <= 3; frames++ ) {
      unsigned bytes = format + 1, size = frames * bytes;
      n = 44 + size + ( size & 1 );
      if( libspectrum_wav_writer_open( &writer, name, 12345,
            (libspectrum_wav_sample_format)format, frames ) ) return TEST_FAIL;
      /* Exercise incremental writes as well as the empty-output case. */
      if( frames ) {
        if( libspectrum_wav_writer_write( writer,
              format ? (void *)input : u8, 1 ) ) ok = 0;
        if( libspectrum_wav_writer_write( writer,
              format ? (void *)( input + 1 ) : u8 + 1, frames - 1 ) ) ok = 0;
      } else if( libspectrum_wav_writer_write( writer, NULL, 0 ) ) ok = 0;
      if( libspectrum_wav_writer_close( writer ) ) ok = 0;
      file = fopen( name, "rb" );
      if( !file ) return TEST_FAIL;
      got = fread( actual, 1, sizeof( actual ), file );
      if( fclose( file ) ) ok = 0;
      memset( expected, 0, sizeof( expected ) );
      memcpy( expected, "RIFF", 4 ); put32( expected + 4, n - 8 );
      memcpy( expected + 8, "WAVEfmt ", 8 ); put32( expected + 16, 16 );
      put16( expected + 20, 1 ); put16( expected + 22, 1 );
      put32( expected + 24, 12345 ); put32( expected + 28, 12345 * bytes );
      put16( expected + 32, bytes ); put16( expected + 34, bytes * 8 );
      memcpy( expected + 36, "data", 4 ); put32( expected + 40, size );
      if( format ) {
        unsigned i;
        for( i = 0; i < frames; i++ )
          put16( expected + 44 + 2 * i, (libspectrum_word)input[i] );
      } else memcpy( expected + 44, u8, frames );
      if( got != n || memcmp( expected, actual, n ) ) ok = 0;
      if( libspectrum_wav_reader_open( &reader, name, &rate, &count ) )
        return TEST_FAIL;
      if( rate != 12345 || count != frames ||
          libspectrum_wav_reader_read( reader, output, 4, &got ) ||
          got != frames ) ok = 0;
      if( frames && output[0] != -32768 ) ok = 0;
      if( frames > 1 && output[1] != 0 ) ok = 0;
      if( frames > 2 && output[2] != ( format ? 32767 : 32512 ) ) ok = 0;
      if( libspectrum_wav_reader_close( reader ) ) ok = 0;
    }
  }
  if( libspectrum_wav_writer_open( &writer, name, 1,
        LIBSPECTRUM_WAV_SAMPLE_UNSIGNED_8, 1 ) ) return TEST_FAIL;
  if( libspectrum_wav_writer_write( writer, u8, 2 ) !=
        LIBSPECTRUM_ERROR_INVALID ||
      libspectrum_wav_writer_write( writer, u8, 1 ) !=
        LIBSPECTRUM_ERROR_INVALID ) ok = 0;
  if( libspectrum_wav_writer_close( writer ) != LIBSPECTRUM_ERROR_INVALID )
    ok = 0;
  if( libspectrum_wav_writer_open( &writer, name, 1,
        LIBSPECTRUM_WAV_SAMPLE_SIGNED_16, 2 ) ) return TEST_FAIL;
  if( libspectrum_wav_writer_close( writer ) != LIBSPECTRUM_ERROR_INVALID )
    ok = 0;
  if( !libspectrum_wav_writer_open( &writer, name, 1,
        LIBSPECTRUM_WAV_SAMPLE_SIGNED_16, (size_t)-1 ) || writer ) ok = 0;
  if( !libspectrum_wav_writer_open( &writer, name, (size_t)-1,
        LIBSPECTRUM_WAV_SAMPLE_SIGNED_16, 0 ) || writer ) ok = 0;
  if( !libspectrum_wav_writer_open( &writer, name, 1,
        (libspectrum_wav_sample_format)99, 0 ) || writer ) ok = 0;
  /* UINT32_MAX - 36 is odd: its required padding also overflows RIFF. */
  if( sizeof( size_t ) >= 4 &&
      ( !libspectrum_wav_writer_open( &writer, name, 1,
          LIBSPECTRUM_WAV_SAMPLE_UNSIGNED_8, (size_t)0xffffffdbUL ) || writer ) )
    ok = 0;
  remove( name );
  if( libspectrum_wav_writer_open( &writer, "test-wav-no-directory/output.wav",
        8000, LIBSPECTRUM_WAV_SAMPLE_UNSIGNED_8, 0 ) !=
      LIBSPECTRUM_ERROR_UNKNOWN || writer ) ok = 0;
  if( !libspectrum_wav_reader_open( &reader, NULL, &rate, &count ) || reader )
    ok = 0;
  return ok ? TEST_PASS : TEST_FAIL;
}
