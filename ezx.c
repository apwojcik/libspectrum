/* ezx.c: Routines for reading EmuZWin .ezx snapshots (read only)
   Copyright (c) 2026 XJ

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

   Format: EZXFormat_Eng.htm by Vladimir Kladov (EmuZWin 2.7 package).
   Plain file starts with "Emuz"; compressed one with "EZX", a 32-bit
   compressed length and a UCL NRV2E (8-bit) stream of the plain file.
*/

#include "config.h"

#include <string.h>

#include "internals.h"

#define EZX_BANK 0x4000
/* Resource limit, not a format limit: snapshots may embed disks and graphics. */
#define EZX_MAX_UNPACKED ( (size_t)16 * 1024 * 1024 )

/* Offsets inside the fixed TSpecData record */
#define EZX_OFS_ROM0   4
#define EZX_OFS_RAM    ( EZX_OFS_ROM0 + 2 * EZX_BANK )
#define EZX_OFS_LOCK   ( EZX_OFS_RAM + 8 * EZX_BANK )
#define EZX_OFS_REGS   ( EZX_OFS_LOCK + 1 )
#define EZX_OFS_I      ( EZX_OFS_REGS + 24 )
#define EZX_OFS_TACTS  ( EZX_OFS_I + 7 )
#define EZX_OFS_BANKS  ( EZX_OFS_TACTS + 4 )
#define EZX_OFS_AY     ( EZX_OFS_BANKS + 8 )
#define EZX_FIXED_LEN  ( EZX_OFS_AY + 17 + 4 + 30 )

/* ---- UCL NRV2E decompressor, 8-bit bit buffer ---- */

typedef struct ezx_bits {
  const libspectrum_byte *src;
  size_t pos, len;
  unsigned int bb, n;
  int error;
} ezx_bits;

static int
ezx_getbit( ezx_bits *s )
{
  if( !s->n ) {
    if( s->pos >= s->len ) {
      s->error = 1;
      return 0;
    }
    s->bb = s->src[ s->pos++ ];
    s->n = 8;
  }
  s->n--;
  return ( s->bb >> s->n ) & 1;
}

static libspectrum_byte
ezx_getbyte( ezx_bits *s )
{
  if( s->pos >= s->len ) {
    s->error = 1;
    return 0;
  }
  return s->src[ s->pos++ ];
}

static int
ezx_put( libspectrum_byte **output, size_t *length, size_t *allocated,
         libspectrum_byte value )
{
  /* Keep bounded doubling here: make_room does not enforce a size limit. */
  if( *length >= *allocated ) {
    if( *allocated >= EZX_MAX_UNPACKED ) return 1;
    *allocated *= 2;
    *output = libspectrum_renew( libspectrum_byte, *output, *allocated );
  }
  ( *output )[ ( *length )++ ] = value;
  return 0;
}

/* Decode an offset and the first length bit. 0xffffffff marks end of input. */
static int
ezx_read_offset( ezx_bits *bits, libspectrum_dword *offset,
                 libspectrum_dword *length, libspectrum_dword *last_offset )
{
  libspectrum_dword value = 1;

  for( ;; ) {
    if( value > 0x7fffffffUL ) return 1;
    value = value * 2 + ezx_getbit( bits );
    if( ezx_getbit( bits ) ) break;
    if( value - 1 > 0x7fffffffUL ) return 1;
    value = ( value - 1 ) * 2 + ezx_getbit( bits );
    if( bits->error ) return 1;
  }

  if( value == 2 ) {
    *offset = *last_offset;
    *length = ezx_getbit( bits );
    return bits->error;
  }

  if( value < 3 || value - 3 > 0x00ffffffUL ) return 1;
  value = ( value - 3 ) * 256 + ezx_getbyte( bits );
  if( bits->error ) return 1;
  *offset = value;
  if( value == 0xffffffffUL ) return 0;

  *length = ( value ^ 0xffffffffUL ) & 1;
  *offset = ( value >> 1 ) + 1;
  *last_offset = *offset;
  return 0;
}

/* Expand the length prefix, including the offset-dependent adjustment and
   the final byte counted by the match-copy loop. */
static int
ezx_read_length( ezx_bits *bits, libspectrum_dword offset,
                 libspectrum_dword *length )
{
  libspectrum_dword value = *length;

  if( value ) {
    value = 1 + ezx_getbit( bits );
  } else if( ezx_getbit( bits ) ) {
    value = 3 + ezx_getbit( bits );
  } else {
    value = 1;
    do {
      if( value > EZX_MAX_UNPACKED / 2 ) return 1;
      value = value * 2 + ezx_getbit( bits );
      if( bits->error ) return 1;
    } while( !ezx_getbit( bits ) );
    value += 3;
  }

  *length = value + ( offset > 0x500 ) + 1;
  return bits->error;
}

/* Copy the literal run preceding each match or end marker. */
static int
ezx_read_literals( ezx_bits *bits, libspectrum_byte **output,
                   size_t *length, size_t *allocated )
{
  while( ezx_getbit( bits ) ) {
    libspectrum_byte value = ezx_getbyte( bits );
    if( bits->error || ezx_put( output, length, allocated, value ) )
      return 1;
  }
  return bits->error;
}

static libspectrum_error
ezx_nrv2e( const libspectrum_byte *src, size_t len,
           libspectrum_byte **out, size_t *out_len )
{
  ezx_bits s = { src, 0, len, 0, 0, 0 };
  /* Enough for the fixed snapshot, with room for small optional chunks. */
  size_t osize = 256 * 1024, olen = 0;
  libspectrum_dword m_off, m_len, last_m_off = 1;
  libspectrum_byte *dst = libspectrum_new( libspectrum_byte, osize );

  for( ;; ) {
    if( ezx_read_literals( &s, &dst, &olen, &osize ) ) goto fail;

    if( ezx_read_offset( &s, &m_off, &m_len, &last_m_off ) ) goto fail;
    if( m_off == 0xffffffffUL ) break;
    if( ezx_read_length( &s, m_off, &m_len ) ) goto fail;
    if( !m_off || m_off > olen ||
        m_len > EZX_MAX_UNPACKED - olen ) goto fail;

    for( ; m_len; m_len-- ) {
      if( ezx_put( &dst, &olen, &osize, dst[ olen - m_off ] ) ) goto fail;
    }
  }

  *out = dst;
  *out_len = olen;
  return LIBSPECTRUM_ERROR_NONE;

fail:
  libspectrum_free( dst );
  libspectrum_print_error( LIBSPECTRUM_ERROR_CORRUPT,
                          "libspectrum_ezx_read: corrupt compressed data" );
  return LIBSPECTRUM_ERROR_CORRUPT;
}

/* ---- snapshot ---- */

static libspectrum_word
ezx_word( const libspectrum_byte *p )
{
  return libspectrum_read_word( &p );
}

static int
ezx_chunk_known( const libspectrum_byte *id )
{
  static const char *ids[] = { "MULT", "JOY ", "PORT", "RAM ", "BETA" };
  size_t i;
  for( i = 0; i < ARRAY_SIZE( ids ); i++ )
    if( !memcmp( id, ids[i], 4 ) ) return 1;
  return 0;
}

/* Machine from Lock7FFD and the frame length in the 'MULT' chunk */
static libspectrum_error
ezx_machine( const libspectrum_byte *d, size_t *length,
             libspectrum_machine *machine )
{
  size_t p = EZX_FIXED_LEN, len = *length;
  libspectrum_dword frame = 0;

  while( p < len ) {
    libspectrum_dword chunk_len;
    if( len - p < 8 ) goto corrupt;
    chunk_len = libspectrum_read_dword_le( d + p + 4 );
    if( chunk_len > len - p - 8 ) {
      /* EmuZWin may write an overlong trailing TAPE chunk; drop it. */
      if( !ezx_chunk_known( d + p ) ) { *length = p; break; }
      goto corrupt;
    }
    if( !memcmp( d + p, "MULT", 4 ) ) {
      if( chunk_len < 9 ) goto corrupt;
      frame = libspectrum_read_dword_le( d + p + 8 + 5 );
    }
    p += 8;
    p += chunk_len;
  }

  /* Approximate custom timings: 128K uses 70908 tstates, Pentagon 71680. */
  *machine = d[ EZX_OFS_LOCK ] ? LIBSPECTRUM_MACHINE_48 :
             frame >= 71000 ? LIBSPECTRUM_MACHINE_PENT :
                              LIBSPECTRUM_MACHINE_128;
  return LIBSPECTRUM_ERROR_NONE;

corrupt:
  libspectrum_print_error( LIBSPECTRUM_ERROR_CORRUPT,
                          "libspectrum_ezx_read: truncated chunk" );
  return LIBSPECTRUM_ERROR_CORRUPT;
}

static libspectrum_byte *
ezx_copy( const libspectrum_byte *data, size_t length )
{
  libspectrum_byte *copy = libspectrum_new( libspectrum_byte, length );
  memcpy( copy, data, length );
  return copy;
}

static void
ezx_read_roms( libspectrum_snap *snap, const libspectrum_byte *data )
{
  int i, count = libspectrum_snap_machine( snap ) == LIBSPECTRUM_MACHINE_48 ?
                 1 : 2;

  libspectrum_snap_set_custom_rom( snap, 1 );
  libspectrum_snap_set_custom_rom_pages( snap, count );
  for( i = 0; i < count; i++ ) {
    int bank = count == 1 ? 1 : i;
    libspectrum_free( libspectrum_snap_roms( snap, i ) );
    libspectrum_snap_set_roms( snap, i,
      ezx_copy( data + EZX_OFS_ROM0 + bank * EZX_BANK, EZX_BANK ) );
    libspectrum_snap_set_rom_length( snap, i, EZX_BANK );
  }
}

static int
ezx_read_joysticks( libspectrum_snap *snap, const libspectrum_byte *data,
                    size_t length )
{
  static const libspectrum_joystick types[] = {
    LIBSPECTRUM_JOYSTICK_KEMPSTON, LIBSPECTRUM_JOYSTICK_SINCLAIR_1,
    LIBSPECTRUM_JOYSTICK_SINCLAIR_2, LIBSPECTRUM_JOYSTICK_FULLER,
    LIBSPECTRUM_JOYSTICK_CURSOR
  };
  int i, count = 0;

  if( length < 5 ) return 1;
  /* Real EmuZWin files have 0x10 here; ignore the setup, not the file. */
  if( data[ 0 ] > 2 ) return 0;
  for( i = 0; i < data[ 0 ]; i++ ) {
    size_t type = data[ 1 + i ];
    /* Custom key mappings and axis inversion have no snapshot equivalent. */
    if( type >= ARRAY_SIZE( types ) ) continue;
    libspectrum_snap_set_joystick_list( snap, count, types[ type ] );
    libspectrum_snap_set_joystick_inputs( snap, count,
      i ? LIBSPECTRUM_JOYSTICK_INPUT_JOYSTICK_2 :
          LIBSPECTRUM_JOYSTICK_INPUT_JOYSTICK_1 );
    count++;
  }
  libspectrum_snap_set_joystick_active_count( snap, count );
  return 0;
}

static int
ezx_read_ports( libspectrum_snap *snap, const libspectrum_byte *data,
                size_t length )
{
  if( length % 3 ) return 1;
  while( length ) {
    libspectrum_word port = libspectrum_read_word( &data );
    switch( port ) {
    case 0x7ffd:
      libspectrum_snap_set_out_128_memoryport( snap, *data );
      break;
    case 0x1ffd:
      libspectrum_snap_set_out_plus3_memoryport( snap, *data );
      break;
    }
    data++;
    length -= 3;
  }
  return 0;
}

static int
ezx_read_chunk( libspectrum_snap *snap, const libspectrum_byte *id,
                const libspectrum_byte *data, size_t length )
{
  if( !memcmp( id, "JOY ", 4 ) )
    return ezx_read_joysticks( snap, data, length );
  if( !memcmp( id, "PORT", 4 ) )
    return ezx_read_ports( snap, data, length );
  if( !memcmp( id, "RAM ", 4 ) ) {
    int bank;
    if( length != EZX_BANK + 1 ) return 1;
    bank = data[ 0 ];
    if( bank < 8 || bank >= SNAPSHOT_RAM_PAGES ) return 1;
    libspectrum_free( libspectrum_snap_pages( snap, bank ) );
    /* Preserve extra banks without guessing an extended machine model. */
    libspectrum_snap_set_pages( snap, bank, ezx_copy( data + 1, EZX_BANK ) );
  } else if( !memcmp( id, "BETA", 4 ) ) {
    if( length < 9 ) return 1;
    libspectrum_snap_set_beta_active( snap, 1 );
    libspectrum_snap_set_beta_paged( snap, !!( data[ 0 ] & 0x80 ) );
    libspectrum_snap_set_beta_direction( snap, !!( data[ 0 ] & 0x04 ) );
    /* Operation coordinates are not WD1793 registers; the status byte is
       reserved. Neither can safely populate the corresponding snap fields. */
  }
  return 0;
}

/* Chunk framing has already been validated by ezx_machine. */
static libspectrum_error
ezx_read_chunks( libspectrum_snap *snap, const libspectrum_byte *data,
                 size_t length )
{
  size_t position = EZX_FIXED_LEN;

  while( position < length ) {
    const libspectrum_byte *id = data + position;
    size_t chunk_length = libspectrum_read_dword_le( id + 4 );
    if( ezx_read_chunk( snap, id, id + 8, chunk_length ) ) {
      libspectrum_print_error( LIBSPECTRUM_ERROR_CORRUPT,
                              "libspectrum_ezx_read: invalid %.4s chunk", id );
      return LIBSPECTRUM_ERROR_CORRUPT;
    }
    position += 8;
    position += chunk_length;
  }
  return LIBSPECTRUM_ERROR_NONE;
}

static libspectrum_error
ezx_read_plain( libspectrum_snap *snap, const libspectrum_byte *d, size_t len )
{
  const libspectrum_byte *r, *b;
  libspectrum_error error;
  libspectrum_machine machine;
  libspectrum_byte port7ffd;
  int i;

  if( len < EZX_FIXED_LEN ) {
    libspectrum_print_error( LIBSPECTRUM_ERROR_CORRUPT,
                            "libspectrum_ezx_read: file too short" );
    return LIBSPECTRUM_ERROR_CORRUPT;
  }

  r = d + EZX_OFS_REGS;
  b = d + EZX_OFS_BANKS;
  error = ezx_machine( d, &len, &machine );
  if( error != LIBSPECTRUM_ERROR_NONE ) return error;
  libspectrum_snap_set_machine( snap, machine );

  libspectrum_snap_set_a  ( snap, r[ 1 ] );
  libspectrum_snap_set_f  ( snap, r[ 0 ] );
  libspectrum_snap_set_bc ( snap, ezx_word( r +  2 ) );
  libspectrum_snap_set_de ( snap, ezx_word( r +  4 ) );
  libspectrum_snap_set_hl ( snap, ezx_word( r +  6 ) );
  libspectrum_snap_set_ix ( snap, ezx_word( r +  8 ) );
  libspectrum_snap_set_iy ( snap, ezx_word( r + 10 ) );
  libspectrum_snap_set_a_ ( snap, r[ 13 ] );
  libspectrum_snap_set_f_ ( snap, r[ 12 ] );
  libspectrum_snap_set_bc_( snap, ezx_word( r + 14 ) );
  libspectrum_snap_set_de_( snap, ezx_word( r + 16 ) );
  libspectrum_snap_set_hl_( snap, ezx_word( r + 18 ) );
  libspectrum_snap_set_pc ( snap, ezx_word( r + 20 ) );
  libspectrum_snap_set_sp ( snap, ezx_word( r + 22 ) );

  r = d + EZX_OFS_I;
  libspectrum_snap_set_i   ( snap, r[ 0 ] );
  libspectrum_snap_set_r   ( snap, r[ 1 ] );
  libspectrum_snap_set_iff1( snap, r[ 3 ] ? 1 : 0 );
  libspectrum_snap_set_iff2( snap, r[ 4 ] ? 1 : 0 );
  libspectrum_snap_set_im  ( snap, r[ 6 ] & 3 );
  libspectrum_snap_set_tstates( snap, libspectrum_read_dword_le( d + EZX_OFS_TACTS ) );

  /* b[0] ROM at 0000 (0=128, 1=48, 2=TR-DOS, 3=Interface 1),
     b[1] RAM at C000, b[2] video (1=bank 7), b[3] border,
     b[4] MIC, b[5] just after EI */
  libspectrum_snap_set_out_ula( snap, ( b[3] & 0x07 ) |
                                     ( b[4] ? 0x08 : 0 ) );
  libspectrum_snap_set_last_instruction_ei( snap, b[5] ? 1 : 0 );

  port7ffd = ( b[1] & 0x07 ) | ( b[2] ? 0x08 : 0 ) |
             ( b[0] == 0 ? 0 : 0x10 ) | ( d[ EZX_OFS_LOCK ] ? 0x20 : 0 );
  libspectrum_snap_set_out_128_memoryport( snap, port7ffd );

  if( b[0] == 2 ) {
    libspectrum_snap_set_beta_active( snap, 1 );
    libspectrum_snap_set_beta_paged( snap, 1 );
  } else if( b[0] == 3 ) {
    libspectrum_snap_set_interface1_active( snap, 1 );
    libspectrum_snap_set_interface1_paged( snap, 1 );
  }

  libspectrum_snap_set_out_ay_registerport( snap, d[ EZX_OFS_AY ] & 0x0f );
  for( i = 0; i < 16; i++ )
    libspectrum_snap_set_ay_registers( snap, i, d[ EZX_OFS_AY + 1 + i ] );

  for( i = 0; i < 8; i++ ) {
    libspectrum_byte *ram;
    if( machine == LIBSPECTRUM_MACHINE_48 && i != 0 && i != 2 && i != 5 )
      continue;
    ram = libspectrum_new( libspectrum_byte, EZX_BANK );
    memcpy( ram, d + EZX_OFS_RAM + i * EZX_BANK, EZX_BANK );
    libspectrum_snap_set_pages( snap, i, ram );
  }

  ezx_read_roms( snap, d );
  return ezx_read_chunks( snap, d, len );
}

libspectrum_error
libspectrum_ezx_read( libspectrum_snap *snap, const libspectrum_byte *buffer,
                      size_t length )
{
  libspectrum_byte *plain = NULL;
  size_t plain_len = 0, packed_len;
  libspectrum_error error;

  if( length >= 4 && !memcmp( buffer, "Emuz", 4 ) )
    return ezx_read_plain( snap, buffer, length );

  if( length < 7 || memcmp( buffer, "EZX", 3 ) ) {
    libspectrum_print_error( LIBSPECTRUM_ERROR_SIGNATURE,
                            "libspectrum_ezx_read: not an EZX file" );
    return LIBSPECTRUM_ERROR_SIGNATURE;
  }

  packed_len = libspectrum_read_dword_le( buffer + 3 );
  if( packed_len > length - 7 ) {
    libspectrum_print_error( LIBSPECTRUM_ERROR_CORRUPT,
                            "libspectrum_ezx_read: truncated compressed data" );
    return LIBSPECTRUM_ERROR_CORRUPT;
  }

  error = ezx_nrv2e( buffer + 7, packed_len, &plain, &plain_len );
  if( error != LIBSPECTRUM_ERROR_NONE ) return error;

  if( plain_len < 4 || memcmp( plain, "Emuz", 4 ) ) {
    libspectrum_free( plain );
    libspectrum_print_error( LIBSPECTRUM_ERROR_CORRUPT,
                            "libspectrum_ezx_read: bad unpacked signature" );
    return LIBSPECTRUM_ERROR_CORRUPT;
  }

  error = ezx_read_plain( snap, plain, plain_len );
  libspectrum_free( plain );
  return error;
}
