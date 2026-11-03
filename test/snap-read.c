#include "config.h"

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "internals.h"
#include "test.h"

/* Independent on-disk IDs: do not share the reader's definitions. */
#define TEST_EZX_CHUNK_TAPE "TAPE"
#define TEST_EZX_CHUNK_MULT "MULT"
#define TEST_EZX_CHUNK_JOY  "JOY "
#define TEST_EZX_CHUNK_PORT "PORT"
#define TEST_EZX_CHUNK_RAM  "RAM "
#define TEST_EZX_CHUNK_BETA "BETA"
#define TEST_EZX_CHUNK_NAME "NAME"
#define TEST_EZX_CHUNK_UNKNOWN "????"

/* Fixed TSpecData size from EZXFormat_Eng.htm, including TTapeData. */
#define TEST_EZX_FIXED_LENGTH ( 4 + 10 * 16384 + 1 + 43 + 17 + 4 + 30 )

static int
ezx_read_expect( const libspectrum_byte *buffer, size_t length,
                 libspectrum_error expected, libspectrum_machine machine )
{
  libspectrum_snap *snap = libspectrum_snap_alloc();
  libspectrum_error error = libspectrum_ezx_read( snap, buffer, length );
  int failed = error != expected;

  if( !failed && error == LIBSPECTRUM_ERROR_NONE )
    failed = libspectrum_snap_machine( snap ) != machine;
  libspectrum_snap_free( snap );
  return failed;
}

test_return_t
reading_ezx_bounds( void )
{
  size_t length = TEST_EZX_FIXED_LENGTH;
  libspectrum_byte *buffer = libspectrum_new0( libspectrum_byte, length + 18 );
  libspectrum_byte packed[23] = { 'E', 'Z', 'X', 16, 0, 0, 0 };
  int failed = 0;

  memcpy( buffer, "Emuz", 4 );
  failed |= ezx_read_expect( buffer, 4, LIBSPECTRUM_ERROR_CORRUPT, 0 );
  failed |= ezx_read_expect( buffer, length, LIBSPECTRUM_ERROR_NONE,
                             LIBSPECTRUM_MACHINE_128 );
  buffer[4 + 10 * 16384] = 1;
  failed |= ezx_read_expect( buffer, length, LIBSPECTRUM_ERROR_NONE,
                             LIBSPECTRUM_MACHINE_48 );
  buffer[4 + 10 * 16384] = 0;

  memcpy( buffer + length, TEST_EZX_CHUNK_MULT, 4 );
  buffer[length + 4] = 9;
  /* 71680 tstates, at data offset 5. */
  buffer[length + 14] = 0x18;
  buffer[length + 15] = 1;
  failed |= ezx_read_expect( buffer, length + 17, LIBSPECTRUM_ERROR_NONE,
                             LIBSPECTRUM_MACHINE_PENT );
  failed |= ezx_read_expect( buffer, length + 16,
                             LIBSPECTRUM_ERROR_CORRUPT, 0 );
  failed |= ezx_read_expect( buffer, length + 18,
                             LIBSPECTRUM_ERROR_CORRUPT, 0 );
  buffer[length + 4] = 8;
  failed |= ezx_read_expect( buffer, length + 16,
                             LIBSPECTRUM_ERROR_CORRUPT, 0 );

  failed |= ezx_read_expect( packed, 7, LIBSPECTRUM_ERROR_CORRUPT, 0 );
  /* An unterminated offset code, followed by EOF. */
  failed |= ezx_read_expect( packed, sizeof( packed ),
                             LIBSPECTRUM_ERROR_CORRUPT, 0 );
  memset( packed + 7, 0xff, sizeof( packed ) - 7 );
  failed |= ezx_read_expect( packed, sizeof( packed ),
                             LIBSPECTRUM_ERROR_CORRUPT, 0 );

  libspectrum_free( buffer );
  return failed ? TEST_FAIL : TEST_PASS;
}

/* Append a chunk to a sufficiently large, caller-owned test buffer. */
static libspectrum_byte *
ezx_test_chunk( libspectrum_byte *ptr, const char *id,
                const libspectrum_byte *data, size_t length )
{
  memcpy( ptr, id, 4 );
  ptr += 4;
  libspectrum_write_dword( &ptr, length );
  memcpy( ptr, data, length );
  return ptr + length;
}

test_return_t
reading_ezx_fixed_state( void )
{
  const size_t lock = 4 + 10 * 16384, banks = lock + 1 + 24 + 7 + 4;
  libspectrum_byte *buffer = libspectrum_new0( libspectrum_byte,
                                              TEST_EZX_FIXED_LENGTH );
  libspectrum_snap *snap = libspectrum_snap_alloc();
  test_return_t result = TEST_FAIL;

  memcpy( buffer, "Emuz", 4 );
  memset( buffer + 4, 0xa5, 16384 );
  memset( buffer + 4 + 16384, 0x5a, 16384 );
  buffer[banks] = 3; /* Interface 1 ROM paged. */
  buffer[banks + 3] = 6;
  buffer[banks + 4] = 1; /* MIC */
  if( libspectrum_ezx_read( snap, buffer, TEST_EZX_FIXED_LENGTH ) ) goto done;
  if( !libspectrum_snap_custom_rom( snap ) ||
      libspectrum_snap_custom_rom_pages( snap ) != 2 ||
      libspectrum_snap_rom_length( snap, 0 ) != 16384 ||
      libspectrum_snap_rom_length( snap, 1 ) != 16384 ||
      memcmp( libspectrum_snap_roms( snap, 0 ), buffer + 4, 16384 ) ||
      memcmp( libspectrum_snap_roms( snap, 1 ), buffer + 4 + 16384, 16384 ) ||
      libspectrum_snap_out_ula( snap ) != 14 ||
      !libspectrum_snap_interface1_active( snap ) ||
      !libspectrum_snap_interface1_paged( snap ) ) goto done;

  libspectrum_snap_free( snap );
  snap = libspectrum_snap_alloc();
  buffer[lock] = 1;
  buffer[banks] = 1;
  buffer[banks + 4] = 0;
  if( libspectrum_ezx_read( snap, buffer, TEST_EZX_FIXED_LENGTH ) ) goto done;
  if( libspectrum_snap_custom_rom_pages( snap ) != 1 ||
      libspectrum_snap_rom_length( snap, 0 ) != 16384 ||
      memcmp( libspectrum_snap_roms( snap, 0 ), buffer + 4 + 16384, 16384 ) ||
      libspectrum_snap_out_ula( snap ) != 6 ) goto done;

  /* TR-DOS paging is meaningful on non-Pentagon machines as well. */
  libspectrum_snap_free( snap );
  snap = libspectrum_snap_alloc();
  buffer[lock] = 0;
  buffer[banks] = 2;
  if( libspectrum_ezx_read( snap, buffer, TEST_EZX_FIXED_LENGTH ) ) goto done;
  if( !libspectrum_snap_beta_active( snap ) ||
      !libspectrum_snap_beta_paged( snap ) ) goto done;
  result = TEST_PASS;

done:
  libspectrum_snap_free( snap );
  libspectrum_free( buffer );
  return result;
}

test_return_t
reading_ezx_chunks( void )
{
  libspectrum_byte *buffer = libspectrum_new0( libspectrum_byte,
                                     TEST_EZX_FIXED_LENGTH + 0xd000 );
  libspectrum_byte *ram = libspectrum_new( libspectrum_byte, 16385 );
  libspectrum_byte *ptr = buffer + TEST_EZX_FIXED_LENGTH;
  libspectrum_byte joy[5] = { 2, 0, 3, 0, 0 };
  const libspectrum_byte ports[] = {
    0xfd, 0x7f, 0x1b, 0xfd, 0x1f, 0x12, 0x34, 0x12, 0xff
  };
  libspectrum_byte beta[9] = { 0x84, 7, 8, 9, 10, 11, 12, 13, 14 };
  libspectrum_snap *snap = libspectrum_snap_alloc();
  test_return_t result = TEST_FAIL;

  memcpy( buffer, "Emuz", 4 );
  ptr = ezx_test_chunk( ptr, TEST_EZX_CHUNK_JOY, joy, sizeof( joy ) );
  ptr = ezx_test_chunk( ptr, TEST_EZX_CHUNK_PORT, ports, sizeof( ports ) );
  ptr = ezx_test_chunk( ptr, TEST_EZX_CHUNK_BETA, beta, sizeof( beta ) );
  memset( ram, 0x31, 16385 );
  ram[0] = 8;
  ptr = ezx_test_chunk( ptr, TEST_EZX_CHUNK_RAM, ram, 16385 );
  /* Repeated banks replace their previous data without leaking it. */
  memset( ram + 1, 0x32, 16384 );
  ptr = ezx_test_chunk( ptr, TEST_EZX_CHUNK_RAM, ram, 16385 );
  ram[0] = 63;
  ptr = ezx_test_chunk( ptr, TEST_EZX_CHUNK_RAM, ram, 16385 );
  if( libspectrum_ezx_read( snap, buffer, ptr - buffer ) ) goto done;
  if( libspectrum_snap_joystick_active_count( snap ) != 2 ||
      libspectrum_snap_joystick_list( snap, 0 ) != LIBSPECTRUM_JOYSTICK_KEMPSTON ||
      libspectrum_snap_joystick_list( snap, 1 ) != LIBSPECTRUM_JOYSTICK_FULLER ||
      libspectrum_snap_joystick_inputs( snap, 0 ) !=
        LIBSPECTRUM_JOYSTICK_INPUT_JOYSTICK_1 ||
      libspectrum_snap_joystick_inputs( snap, 1 ) !=
        LIBSPECTRUM_JOYSTICK_INPUT_JOYSTICK_2 ||
      libspectrum_snap_out_128_memoryport( snap ) != 0x1b ||
      libspectrum_snap_out_plus3_memoryport( snap ) != 0x12 ||
      !libspectrum_snap_beta_active( snap ) ||
      !libspectrum_snap_beta_paged( snap ) ||
      !libspectrum_snap_beta_direction( snap ) ||
      libspectrum_snap_beta_track( snap ) ||
      libspectrum_snap_beta_sector( snap ) ||
      libspectrum_snap_beta_status( snap ) ||
      !libspectrum_snap_pages( snap, 8 ) ||
      !libspectrum_snap_pages( snap, 63 ) ||
      memcmp( libspectrum_snap_pages( snap, 8 ), ram + 1, 16384 ) ||
      memcmp( libspectrum_snap_pages( snap, 63 ), ram + 1, 16384 ) ) goto done;

  /* Unsupported custom bindings must not consume a snapshot joystick slot. */
  libspectrum_snap_free( snap );
  snap = libspectrum_snap_alloc();
  joy[1] = 5;
  joy[2] = 4;
  beta[0] = 0;
  ptr = ezx_test_chunk( buffer + TEST_EZX_FIXED_LENGTH,
                        TEST_EZX_CHUNK_JOY, joy, sizeof( joy ) );
  ptr = ezx_test_chunk( ptr, TEST_EZX_CHUNK_BETA, beta, sizeof( beta ) );
  if( libspectrum_ezx_read( snap, buffer, ptr - buffer ) ) goto done;
  if( libspectrum_snap_joystick_active_count( snap ) != 1 ||
      libspectrum_snap_joystick_list( snap, 0 ) != LIBSPECTRUM_JOYSTICK_CURSOR ||
      libspectrum_snap_joystick_inputs( snap, 0 ) !=
        LIBSPECTRUM_JOYSTICK_INPUT_JOYSTICK_2 ||
      libspectrum_snap_beta_paged( snap ) ||
      libspectrum_snap_beta_direction( snap ) ) goto done;
  result = TEST_PASS;

done:
  libspectrum_snap_free( snap );
  libspectrum_free( ram );
  libspectrum_free( buffer );
  return result;
}

test_return_t
reading_ezx_invalid_chunks( void )
{
  static const struct {
    const char *id;
    size_t length;
    libspectrum_byte first;
  } cases[] = {
    { TEST_EZX_CHUNK_JOY, 0, 0 }, { TEST_EZX_CHUNK_JOY, 4, 0 },
    { TEST_EZX_CHUNK_JOY, 4, 0x10 }, { TEST_EZX_CHUNK_PORT, 1, 0 },
    { TEST_EZX_CHUNK_BETA, 8, 0 }, { TEST_EZX_CHUNK_RAM, 0, 0 },
    { TEST_EZX_CHUNK_RAM, 16384, 8 }, { TEST_EZX_CHUNK_RAM, 16385, 7 },
    { TEST_EZX_CHUNK_RAM, 16385, 64 }
  };
  static const char *strict_ids[] = {
    TEST_EZX_CHUNK_MULT, TEST_EZX_CHUNK_JOY, TEST_EZX_CHUNK_PORT,
    TEST_EZX_CHUNK_RAM, TEST_EZX_CHUNK_BETA, TEST_EZX_CHUNK_NAME,
    TEST_EZX_CHUNK_UNKNOWN
  };
  libspectrum_byte *buffer = libspectrum_new0( libspectrum_byte,
                                     TEST_EZX_FIXED_LENGTH + 8 + 16385 );
  libspectrum_byte *data = libspectrum_new0( libspectrum_byte, 16385 );
  libspectrum_byte *end;
  size_t i;
  int failed = 0;

  memcpy( buffer, "Emuz", 4 );
  for( i = 0; i < ARRAY_SIZE( cases ); i++ ) {
    data[0] = cases[i].first;
    end = ezx_test_chunk( buffer + TEST_EZX_FIXED_LENGTH,
                          cases[i].id, data, cases[i].length );
    failed |= ezx_read_expect( buffer, end - buffer,
                               LIBSPECTRUM_ERROR_CORRUPT, 0 );
  }
  /* A driver slot count of 0x10 is masked to zero. */
  data[0] = 0x10;
  end = ezx_test_chunk( buffer + TEST_EZX_FIXED_LENGTH,
                        TEST_EZX_CHUNK_JOY, data, 5 );
  failed |= ezx_read_expect( buffer, end - buffer, LIBSPECTRUM_ERROR_NONE,
                             LIBSPECTRUM_MACHINE_128 );
  /* Only the observed overlong TAPE tail is tolerated. */
  end = ezx_test_chunk( buffer + TEST_EZX_FIXED_LENGTH,
                        TEST_EZX_CHUNK_TAPE, data, 16 );
  buffer[TEST_EZX_FIXED_LENGTH + 4] = 0xff;
  failed |= ezx_read_expect( buffer, end - buffer, LIBSPECTRUM_ERROR_NONE,
                             LIBSPECTRUM_MACHINE_128 );
  for( i = 0; i < ARRAY_SIZE( strict_ids ); i++ ) {
    end = ezx_test_chunk( buffer + TEST_EZX_FIXED_LENGTH,
                          strict_ids[i], data, 16 );
    buffer[TEST_EZX_FIXED_LENGTH + 4] = 0xff;
    failed |= ezx_read_expect( buffer, end - buffer,
                               LIBSPECTRUM_ERROR_CORRUPT, 0 );
  }
  /* Even a TAPE tail needs a complete eight-byte chunk header. */
  end = ezx_test_chunk( buffer + TEST_EZX_FIXED_LENGTH,
                        TEST_EZX_CHUNK_TAPE, data, 16 );
  for( i = 1; i < 8; i++ )
    failed |= ezx_read_expect( buffer, TEST_EZX_FIXED_LENGTH + i,
                               LIBSPECTRUM_ERROR_CORRUPT, 0 );
  libspectrum_free( data );
  libspectrum_free( buffer );
  return failed ? TEST_FAIL : TEST_PASS;
}

test_return_t
reading_ezx_compatibility_state( void )
{
  const size_t lock = 4 + 10 * 16384, registers = lock + 1;
  const size_t banks = registers + 24 + 7 + 4, ay = banks + 8;
  const size_t page5 = 4 + 2 * 16384 + 5 * 16384;
  const libspectrum_byte joy[] = { 1, 0, 0, 0, 0 };
  const libspectrum_byte driver_joy[] = { 0x10, 3, 4, 0, 0 };
  const libspectrum_byte ports[] = { 0xfd, 0x7f, 0x1b };
  const libspectrum_byte mult[] = { 0, 0, 0, 0, 0, 0, 0x18, 1, 0 };
  const libspectrum_byte tape[16] = { 0 };
  libspectrum_byte *buffer = libspectrum_new0( libspectrum_byte,
                                              TEST_EZX_FIXED_LENGTH + 128 );
  libspectrum_byte *ptr = buffer + TEST_EZX_FIXED_LENGTH, *tail_length;
  libspectrum_snap *snap = libspectrum_snap_alloc();
  test_return_t result = TEST_FAIL;

  memcpy( buffer, "Emuz", 4 );
  buffer[registers + 1] = 0x56; /* A */
  buffer[registers + 20] = 0x34; /* PC = 0x1234 */
  buffer[registers + 21] = 0x12;
  buffer[banks + 3] = 5;
  buffer[banks + 4] = 1; /* MIC */
  buffer[ay] = 7;
  buffer[ay + 1 + 9] = 0xab;
  memset( buffer + page5, 0x6b, 16384 );
  ptr = ezx_test_chunk( ptr, TEST_EZX_CHUNK_JOY, joy, sizeof( joy ) );
  ptr = ezx_test_chunk( ptr, TEST_EZX_CHUNK_JOY,
                        driver_joy, sizeof( driver_joy ) );
  ptr = ezx_test_chunk( ptr, TEST_EZX_CHUNK_PORT, ports, sizeof( ports ) );
  ptr = ezx_test_chunk( ptr, TEST_EZX_CHUNK_MULT, mult, sizeof( mult ) );
  /* Well-formed unknown chunks are still skipped normally. */
  ptr = ezx_test_chunk( ptr, TEST_EZX_CHUNK_UNKNOWN, tape, sizeof( tape ) );
  tail_length = ptr + 4;
  ptr = ezx_test_chunk( ptr, TEST_EZX_CHUNK_TAPE, tape, sizeof( tape ) );
  libspectrum_write_dword( &tail_length, 0xffffffffUL );

  if( libspectrum_snap_read( snap, buffer, ptr - buffer,
                             LIBSPECTRUM_ID_SNAPSHOT_EZX, NULL ) ) goto done;
  if( libspectrum_snap_machine( snap ) != LIBSPECTRUM_MACHINE_PENT ||
      libspectrum_snap_a( snap ) != 0x56 ||
      libspectrum_snap_pc( snap ) != 0x1234 ||
      libspectrum_snap_out_ula( snap ) != 13 ||
      libspectrum_snap_out_ay_registerport( snap ) != 7 ||
      libspectrum_snap_ay_registers( snap, 9 ) != 0xab ||
      libspectrum_snap_out_128_memoryport( snap ) != 0x1b ||
      libspectrum_snap_joystick_active_count( snap ) != 0 ||
      !libspectrum_snap_pages( snap, 5 ) ||
      memcmp( libspectrum_snap_pages( snap, 5 ), buffer + page5, 16384 ) )
    goto done;
  result = TEST_PASS;

done:
  libspectrum_snap_free( snap );
  libspectrum_free( buffer );
  return result;
}

/* Exercise every stored count, including high bits and masked value three.
   Precede it with a valid setup to distinguish clearing from ignoring it. */
test_return_t
reading_ezx_joystick_count_mask( void )
{
  const libspectrum_byte initial[] = { 1, 0, 0, 0, 0 };
  libspectrum_byte joy[] = { 0, 3, 4, 0, 0 };
  libspectrum_byte *buffer = libspectrum_new0( libspectrum_byte,
                                              TEST_EZX_FIXED_LENGTH + 26 );
  libspectrum_byte *ptr, *end;
  int value, failed = 0;

  memcpy( buffer, "Emuz", 4 );
  ptr = ezx_test_chunk( buffer + TEST_EZX_FIXED_LENGTH,
                        TEST_EZX_CHUNK_JOY, initial, sizeof( initial ) );
  for( value = 0; value < 256; value++ ) {
    int masked = value & 3;
    size_t expected = masked == 3 ? 1 : masked;
    libspectrum_snap *snap = libspectrum_snap_alloc();
    joy[0] = value;
    end = ezx_test_chunk( ptr, TEST_EZX_CHUNK_JOY, joy, sizeof( joy ) );
    if( libspectrum_ezx_read( snap, buffer, end - buffer ) ||
        libspectrum_snap_joystick_active_count( snap ) != expected ) {
      failed = 1;
    } else if( expected ) {
      libspectrum_joystick first = masked == 3 ? LIBSPECTRUM_JOYSTICK_KEMPSTON :
                                                LIBSPECTRUM_JOYSTICK_FULLER;
      if( libspectrum_snap_joystick_list( snap, 0 ) != first ||
          libspectrum_snap_joystick_inputs( snap, 0 ) !=
            LIBSPECTRUM_JOYSTICK_INPUT_JOYSTICK_1 ) failed = 1;
      if( expected == 2 &&
          ( libspectrum_snap_joystick_list( snap, 1 ) !=
              LIBSPECTRUM_JOYSTICK_CURSOR ||
            libspectrum_snap_joystick_inputs( snap, 1 ) !=
              LIBSPECTRUM_JOYSTICK_INPUT_JOYSTICK_2 ) ) failed = 1;
    }
    libspectrum_snap_free( snap );
    if( failed ) {
      fprintf( stderr, "EZX joystick count 0x%02x failed\n", value );
      break;
    }
  }
  libspectrum_free( buffer );
  return failed ? TEST_FAIL : TEST_PASS;
}

/* Test for bug #102: invalid compressed file causes crash */
test_return_t
invalid_compressed_file_1( void )
{
  #ifndef HAVE_ZLIB_H
    return TEST_SKIPPED; /* gzip not enabled in build */
  #endif

  const char *filename = STATIC_TEST_PATH( "invalid.gz" );
  return read_snap( filename, filename, LIBSPECTRUM_ERROR_UNKNOWN );
}

/* Further test for bug #102: invalid compressed file causes crash */
test_return_t
invalid_compressed_file_2( void )
{
  #ifndef HAVE_ZLIB_H
    return TEST_SKIPPED; /* gzip not enabled in build */
  #endif

  return read_snap( STATIC_TEST_PATH( "invalid.gz" ), NULL, LIBSPECTRUM_ERROR_UNKNOWN );
}

/* Test for bug #103: pointer wraparound causes segfault */
test_return_t
pointer_wraparound_in_szx_file( void )
{
  const char *filename = STATIC_TEST_PATH( "invalid.szx" );
  return read_snap( filename, filename, LIBSPECTRUM_ERROR_CORRUPT );
}

/* Tests for bug #129: SP not sanity checked when reading .sna files;
   also tests bug #130: compressed snapshots cause segfault */
test_return_t
sna_file_with_sp_0x4000( void )
{
  #ifndef HAVE_ZLIB_H
    return TEST_SKIPPED; /* gzip not enabled in build */
  #endif

  const char *filename = STATIC_TEST_PATH( "sp-2000.sna.gz" );
  return read_snap( filename, filename, LIBSPECTRUM_ERROR_CORRUPT );
}

test_return_t
sna_file_with_sp_0xffff( void )
{
  #ifndef HAVE_ZLIB_H
    return TEST_SKIPPED; /* gzip not enabled in build */
  #endif

  const char *filename = STATIC_TEST_PATH( "sp-ffff.sna.gz" );
  return read_snap( filename, filename, LIBSPECTRUM_ERROR_CORRUPT );
}

test_return_t
writing_sna_file( void )
{
  const char *filename = STATIC_TEST_PATH( "empty.z80" );
  libspectrum_byte *buffer = NULL;
  size_t filesize = 0, length = 0;
  libspectrum_snap *snap;
  int flags;
  test_return_t r = TEST_INCOMPLETE;

  if( read_file( &buffer, &filesize, filename ) ) return TEST_INCOMPLETE;

  snap = libspectrum_snap_alloc();

  if( libspectrum_snap_read( snap, buffer, filesize, LIBSPECTRUM_ID_UNKNOWN,
                             filename ) != LIBSPECTRUM_ERROR_NONE ) {
    fprintf( stderr, "%s: reading `%s' failed\n", progname, filename );
    libspectrum_snap_free( snap );
    libspectrum_free( buffer );
    return TEST_INCOMPLETE;
  }

  libspectrum_free( buffer );
  buffer = NULL;

  if( libspectrum_snap_write( &buffer, &length, &flags, snap,
                              LIBSPECTRUM_ID_SNAPSHOT_SNA, NULL, 0 ) !=
      LIBSPECTRUM_ERROR_NONE ) {
    fprintf( stderr, "%s: serialising to SNA failed\n", progname );
    libspectrum_snap_free( snap );
    return TEST_INCOMPLETE;
  }

  libspectrum_snap_free( snap );
  snap = libspectrum_snap_alloc();

  if( libspectrum_snap_read( snap, buffer, length, LIBSPECTRUM_ID_SNAPSHOT_SNA,
                             NULL ) != LIBSPECTRUM_ERROR_NONE ) {
    fprintf( stderr, "%s: restoring from SNA failed\n", progname );
    libspectrum_snap_free( snap );
    libspectrum_free( buffer );
    return TEST_INCOMPLETE;
  }

  libspectrum_free( buffer );

  if( libspectrum_snap_pc( snap ) != 0x1234 ) {
    fprintf( stderr, "%s: PC is 0x%04x, not the expected 0x1234\n", progname,
             libspectrum_snap_pc( snap ) );
    r = TEST_FAIL;
  } else if( libspectrum_snap_sp( snap ) != 0x8000 ) {
    fprintf( stderr, "%s: SP is 0x%04x, not the expected 0x8000\n", progname,
             libspectrum_snap_sp( snap ) );
    r = TEST_FAIL;
  } else {
    r = TEST_PASS;
  }

  libspectrum_snap_free( snap );

  return r;
}

/* Tests for bug #198: last out to 0x1ffd is not serialised into .z80 files */
test_return_t
writing_plus_3_z80_file( void )
{
  const char *filename = STATIC_TEST_PATH( "plus3.z80" );
  libspectrum_byte *buffer = NULL;
  size_t filesize = 0, length = 0;
  libspectrum_snap *snap;
  int flags;
  test_return_t r = TEST_INCOMPLETE;

  if( read_file( &buffer, &filesize, filename ) ) return TEST_INCOMPLETE;

  snap = libspectrum_snap_alloc();

  if( libspectrum_snap_read( snap, buffer, filesize, LIBSPECTRUM_ID_UNKNOWN,
                             filename ) != LIBSPECTRUM_ERROR_NONE ) {
    fprintf( stderr, "%s: reading `%s' failed\n", progname, filename );
    libspectrum_snap_free( snap );
    libspectrum_free( buffer );
    return TEST_INCOMPLETE;
  }

  libspectrum_free( buffer );
  buffer = NULL;

  if( libspectrum_snap_write( &buffer, &length, &flags, snap,
                              LIBSPECTRUM_ID_SNAPSHOT_Z80, NULL, 0 ) !=
      LIBSPECTRUM_ERROR_NONE ) {
    fprintf( stderr, "%s: serialising to Z80 failed\n", progname );
    libspectrum_snap_free( snap );
    return TEST_INCOMPLETE;
  }

  libspectrum_snap_free( snap );
  snap = libspectrum_snap_alloc();

  if( libspectrum_snap_read( snap, buffer, length, LIBSPECTRUM_ID_SNAPSHOT_Z80,
                             NULL ) != LIBSPECTRUM_ERROR_NONE ) {
    fprintf( stderr, "%s: restoring from Z80 failed\n", progname );
    libspectrum_snap_free( snap );
    libspectrum_free( buffer );
    return TEST_INCOMPLETE;
  }

  if( libspectrum_snap_out_plus3_memoryport( snap ) == 0xaa ) {
    r = TEST_PASS;
  } else {
    fprintf( stderr,
             "%s: Last out to 0x1ffd is 0x%02x, not the expected 0xaa\n",
             progname, libspectrum_snap_out_plus3_memoryport( snap ) );
    r = TEST_FAIL;
  }

  libspectrum_snap_free( snap );

  return r;
}

static test_return_t
check_scorpion_z80_pages( int first_page, int page_count,
                           libspectrum_error expected_error )
{
  const size_t header_length = 87;
  const size_t compressed_length = 260;
  size_t length, offset;
  libspectrum_byte *buffer;
  libspectrum_snap *snap;
  libspectrum_error error;
  test_return_t r = TEST_PASS;
  int i, j;

  length = header_length + page_count * ( 3 + compressed_length );
  buffer = libspectrum_new0( libspectrum_byte, length );

  /* A minimal version 3 Z80 header for a Scorpion ZS 256. */
  buffer[30] = 55;
  buffer[32] = 0x34;
  buffer[33] = 0x12;
  buffer[34] = 10;

  offset = header_length;
  for( i = 0; i < page_count; i++ ) {
    int bank = ( i * 5 ) % 16;
    int remaining = 0x4000;

    buffer[offset    ] = compressed_length & 0xff;
    buffer[offset + 1] = compressed_length >> 8;
    buffer[offset + 2] = first_page + bank;
    offset += 3;

    for( j = 0; j < 65; j++ ) {
      int run_length = remaining > 255 ? 255 : remaining;

      buffer[offset++] = 0xed;
      buffer[offset++] = 0xed;
      buffer[offset++] = run_length;
      buffer[offset++] = bank;
      remaining -= run_length;
    }
  }

  snap = libspectrum_snap_alloc();
  error = libspectrum_snap_read( snap, buffer, length,
                                 LIBSPECTRUM_ID_SNAPSHOT_Z80, NULL );

  if( error != expected_error ) {
    fprintf( stderr, "%s: reading Scorpion Z80 returned %d, expected %d\n",
             progname, error, expected_error );
    r = TEST_FAIL;
  } else if( error == LIBSPECTRUM_ERROR_NONE ) {
    for( i = 0; i < 16; i++ ) {
      libspectrum_byte *page = libspectrum_snap_pages( snap, i );

      if( !page || page[0] != i || page[0x3fff] != i ) {
        fprintf( stderr, "%s: Scorpion RAM page %d was mapped incorrectly\n",
                 progname, i );
        r = TEST_FAIL;
        break;
      }
    }
  }

  libspectrum_snap_free( snap );
  libspectrum_free( buffer );
  return r;
}

/* Spectaculator 6.1 used page identifiers 0--15 for Scorpion snapshots. */
test_return_t
reading_spectaculator_61_scorpion_z80_pages( void )
{
  test_return_t r;

  r = check_scorpion_z80_pages( 0, 16, LIBSPECTRUM_ERROR_NONE );
  if( r != TEST_PASS ) return r;

  /* The conventional 3--18 mapping must remain unchanged. */
  r = check_scorpion_z80_pages( 3, 16, LIBSPECTRUM_ERROR_NONE );
  if( r != TEST_PASS ) return r;

  /* Page 0 alone is not enough to identify the Spectaculator workaround. */
  return check_scorpion_z80_pages( 0, 15, LIBSPECTRUM_ERROR_UNKNOWN );
}

/* Tests for bug #184: SZX files were written with A and F reversed */
test_return_t
reading_old_szx_file( void )
{
  #ifndef HAVE_ZLIB_H
    return TEST_SKIPPED; /* gzip not enabled in build */
  #endif

  const char *filename = STATIC_TEST_PATH( "empty.szx" );
  libspectrum_byte *buffer = NULL;
  size_t filesize = 0;
  libspectrum_snap *snap;
  test_return_t r = TEST_INCOMPLETE;

  if( read_file( &buffer, &filesize, filename ) ) return TEST_INCOMPLETE;

  snap = libspectrum_snap_alloc();

  if( libspectrum_snap_read( snap, buffer, filesize, LIBSPECTRUM_ID_UNKNOWN,
                             filename ) != LIBSPECTRUM_ERROR_NONE ) {
    fprintf( stderr, "%s: reading `%s' failed\n", progname, filename );
    libspectrum_snap_free( snap );
    libspectrum_free( buffer );
    return TEST_INCOMPLETE;
  }

  libspectrum_free( buffer );

  if( libspectrum_snap_a( snap ) != 0x12 ) {
    fprintf( stderr, "%s: A is 0x%02x, not the expected 0x12\n", progname,
             libspectrum_snap_a( snap ) );
    r = TEST_FAIL;
  } else if( libspectrum_snap_f( snap ) != 0x34 ) {
    fprintf( stderr, "%s: F is 0x%02x, not the expected 0x34\n", progname,
             libspectrum_snap_f( snap ) );
    r = TEST_FAIL;
  } else if( libspectrum_snap_a_( snap ) != 0x56 ) {
    fprintf( stderr, "%s: A' is 0x%02x, not the expected 0x56\n", progname,
             libspectrum_snap_a_( snap ) );
    r = TEST_FAIL;
  } else if( libspectrum_snap_f_( snap ) != 0x78 ) {
    fprintf( stderr, "%s: F' is 0x%02x, not the expected 0x78\n", progname,
             libspectrum_snap_f_( snap ) );
    r = TEST_FAIL;
  } else {
    r = TEST_PASS;
  }

  return r;
}


/* libspectrum_split_to_48k_pages: each 16K region goes to the correct page */
test_return_t
split_to_48k_pages_distributes_memory_to_correct_pages( void )
{
  libspectrum_snap *snap;
  libspectrum_byte data[0xc000];
  test_return_t r = TEST_FAIL;

  memset( &data[0x0000], 0x11, 0x4000 );
  memset( &data[0x4000], 0x22, 0x4000 );
  memset( &data[0x8000], 0x33, 0x4000 );

  snap = libspectrum_snap_alloc();
  if( !snap ) {
    fprintf( stderr, "%s: split_to_48k_pages_distributes_memory_to_correct_pages: snap_alloc failed\n", progname );
    return TEST_INCOMPLETE;
  }

  if( libspectrum_split_to_48k_pages( snap, data ) != LIBSPECTRUM_ERROR_NONE ) {
    fprintf( stderr, "%s: split_to_48k_pages_distributes_memory_to_correct_pages: split failed\n", progname );
    goto done;
  }

  if( !libspectrum_snap_pages( snap, 5 ) || libspectrum_snap_pages( snap, 5 )[0] != 0x11 ) {
    fprintf( stderr, "%s: split_to_48k_pages_distributes_memory_to_correct_pages: page 5 mismatch\n", progname );
    goto done;
  }

  if( !libspectrum_snap_pages( snap, 2 ) || libspectrum_snap_pages( snap, 2 )[0] != 0x22 ) {
    fprintf( stderr, "%s: split_to_48k_pages_distributes_memory_to_correct_pages: page 2 mismatch\n", progname );
    goto done;
  }

  if( !libspectrum_snap_pages( snap, 0 ) || libspectrum_snap_pages( snap, 0 )[0] != 0x33 ) {
    fprintf( stderr, "%s: split_to_48k_pages_distributes_memory_to_correct_pages: page 0 mismatch\n", progname );
    goto done;
  }

  r = TEST_PASS;

done:
  libspectrum_snap_free( snap );
  return r;
}

/* libspectrum_split_to_48k_pages: returns LOGIC error if any page already occupied */
test_return_t
split_to_48k_pages_fails_when_page_already_occupied( void )
{
  libspectrum_snap *snap;
  libspectrum_byte data[0xc000];
  libspectrum_byte *page5;
  libspectrum_error_function_t error_function;
  test_return_t r = TEST_FAIL;

  memset( data, 0, sizeof( data ) );

  snap = libspectrum_snap_alloc();
  if( !snap ) {
    fprintf( stderr, "%s: split_to_48k_pages_fails_when_page_already_occupied: snap_alloc failed\n", progname );
    return TEST_INCOMPLETE;
  }

  page5 = libspectrum_new( libspectrum_byte, 0x4000 );
  libspectrum_snap_set_pages( snap, 5, page5 );

  error_function = libspectrum_error_function;
  libspectrum_error_function = NULL;
  if( libspectrum_split_to_48k_pages( snap, data ) != LIBSPECTRUM_ERROR_LOGIC ) {
    libspectrum_error_function = error_function;
    fprintf( stderr, "%s: split_to_48k_pages_fails_when_page_already_occupied: expected LIBSPECTRUM_ERROR_LOGIC\n", progname );
    goto done;
  }
  libspectrum_error_function = error_function;

  r = TEST_PASS;

done:
  libspectrum_snap_free( snap );
  return r;
}

/* libspectrum_write_snap_page: writes the page RAM data to buffer */
test_return_t
write_snap_page_writes_ram_data_to_buffer( void )
{
  libspectrum_snap *snap;
  libspectrum_buffer *buf;
  libspectrum_byte *page;
  const libspectrum_byte *data;
  size_t size;
  test_return_t r = TEST_FAIL;

  snap = libspectrum_snap_alloc();
  if( !snap ) {
    fprintf( stderr, "%s: write_snap_page_writes_ram_data_to_buffer: snap_alloc failed\n", progname );
    return TEST_INCOMPLETE;
  }

  page = libspectrum_new( libspectrum_byte, 0x4000 );
  memset( page, 0xab, 0x4000 );
  libspectrum_snap_set_pages( snap, 5, page );

  buf = libspectrum_buffer_alloc();
  libspectrum_write_snap_page( buf, snap, 5 );

  size = libspectrum_buffer_get_data_size( buf );
  data = libspectrum_buffer_get_data( buf );

  if( size != 0x4000 ) {
    fprintf( stderr, "%s: write_snap_page_writes_ram_data_to_buffer: expected 0x4000 bytes, got %lu\n",
             progname, (unsigned long)size );
    goto done;
  }

  if( data[0] != 0xab || data[0x3fff] != 0xab ) {
    fprintf( stderr, "%s: write_snap_page_writes_ram_data_to_buffer: expected 0xab fill, got 0x%02x/0x%02x\n",
             progname, data[0], data[0x3fff] );
    goto done;
  }

  r = TEST_PASS;

done:
  libspectrum_buffer_free( buf );
  libspectrum_snap_free( snap );
  return r;
}

/* libspectrum_write_snap_page: fills 0xff when the page pointer is NULL */
test_return_t
write_snap_page_fills_0xff_for_absent_page( void )
{
  libspectrum_snap *snap;
  libspectrum_buffer *buf;
  const libspectrum_byte *data;
  size_t size;
  test_return_t r = TEST_FAIL;

  snap = libspectrum_snap_alloc();
  if( !snap ) {
    fprintf( stderr, "%s: write_snap_page_fills_0xff_for_absent_page: snap_alloc failed\n", progname );
    return TEST_INCOMPLETE;
  }

  /* Page 3 is never set -> NULL -> should fill 0xff */
  buf = libspectrum_buffer_alloc();
  libspectrum_write_snap_page( buf, snap, 3 );

  size = libspectrum_buffer_get_data_size( buf );
  data = libspectrum_buffer_get_data( buf );

  if( size != 0x4000 ) {
    fprintf( stderr, "%s: write_snap_page_fills_0xff_for_absent_page: expected 0x4000 bytes, got %lu\n",
             progname, (unsigned long)size );
    goto done;
  }

  if( data[0] != 0xff || data[0x3fff] != 0xff ) {
    fprintf( stderr, "%s: write_snap_page_fills_0xff_for_absent_page: expected 0xff fill, got 0x%02x/0x%02x\n",
             progname, data[0], data[0x3fff] );
    goto done;
  }

  r = TEST_PASS;

done:
  libspectrum_buffer_free( buf );
  libspectrum_snap_free( snap );
  return r;
}
