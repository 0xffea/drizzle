/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* The old structures from unireg */

#ifndef DRIZZLED_STRUCTS_H
#define DRIZZLED_STRUCTS_H

#include "drizzled/base.h"
#include "drizzled/definitions.h"
#include "drizzled/lex_string.h"
#include "drizzled/thr_lock.h"

namespace drizzled
{

namespace internal
{
typedef struct st_io_cache IO_CACHE;
}

class Table;
class Field;

typedef struct st_keyfile_info {	/* used with ha_info() */
  unsigned char ref[MAX_REFLENGTH];		/* Pointer to current row */
  unsigned char dupp_ref[MAX_REFLENGTH];	/* Pointer to dupp row */
  uint32_t ref_length;			/* Length of ref (1-8) */
  uint32_t block_size;			/* index block size */
  int filenr;				/* (uniq) filenr for table */
  ha_rows records;			/* Records i datafilen */
  ha_rows deleted;			/* Deleted records */
  uint64_t data_file_length;		/* Length off data file */
  uint64_t max_data_file_length;	/* Length off data file */
  uint64_t index_file_length;
  uint64_t max_index_file_length;
  uint64_t delete_length;		/* Free bytes */
  uint64_t auto_increment_value;
  int errkey,sortkey;			/* Last errorkey and sorted by */
  time_t create_time;			/* When table was created */
  time_t check_time;
  time_t update_time;
  uint64_t mean_rec_length;		/* physical reclength */
} KEYFILE_INFO;


typedef struct st_key_part_info {	/* Info about a key part */
  Field *field;
  unsigned int	offset;				/* offset in record (from 0) */
  unsigned int	null_offset;			/* Offset to null_bit in record */
  /* Length of key part in bytes, excluding NULL flag and length bytes */
  uint16_t length;
  /*
    Number of bytes required to store the keypart value. This may be
    different from the "length" field as it also counts
     - possible NULL-flag byte (see HA_KEY_NULL_LENGTH) [if null_bit != 0,
       the first byte stored at offset is 1 if null, 0 if non-null; the
       actual value is stored from offset+1].
     - possible HA_KEY_BLOB_LENGTH bytes needed to store actual value length.
  */
  uint16_t store_length;
  uint16_t key_type;
  uint16_t fieldnr;			/* Fieldnum in UNIREG (1,2,3,...) */
  uint16_t key_part_flag;			/* 0 or HA_REVERSE_SORT */
  uint8_t type;
  uint8_t null_bit;			/* Position to null_bit */
} KEY_PART_INFO ;


typedef struct st_key {
  unsigned int	key_length;		/* Tot length of key */
  enum  ha_key_alg algorithm;
  unsigned long flags;			/* dupp key and pack flags */
  unsigned int key_parts;		/* How many key_parts */
  uint32_t  extra_length;
  unsigned int usable_key_parts;	/* Should normally be = key_parts */
  uint32_t  block_size;
  KEY_PART_INFO *key_part;
  char	*name;				/* Name of key */
  /*
    Array of AVG(#records with the same field value) for 1st ... Nth key part.
    0 means 'not known'.
    For temporary heap tables this member is NULL.
  */
  ulong *rec_per_key;
  Table *table;
  LEX_STRING comment;
} KEY;


class JoinTable;

struct RegInfo {		/* Extra info about reg */
  JoinTable *join_tab;	/* Used by SELECT() */
  enum thr_lock_type lock_type;		/* How database is used */
  bool not_exists_optimize;
  bool impossible_range;
  RegInfo()
    : join_tab(NULL), lock_type(TL_UNLOCK),
      not_exists_optimize(false), impossible_range(false) {}
  void reset()
  {
    join_tab= NULL;
    lock_type= TL_UNLOCK;
    not_exists_optimize= false;
    impossible_range= false;
  }
};

struct st_read_record;				/* For referense later */
class Session;
class Cursor;
namespace optimizer { class SqlSelect; }

typedef struct st_read_record {			/* Parameter to read_record */
  Table *table;			/* Head-form */
  Cursor *cursor;
  Table **forms;			/* head and ref forms */
  int (*read_record)(struct st_read_record *);
  Session *session;
  optimizer::SqlSelect *select;
  uint32_t cache_records;
  uint32_t ref_length,struct_length,reclength,rec_cache_size,error_offset;
  uint32_t index;
  unsigned char *ref_pos;				/* pointer to form->refpos */
  unsigned char *record;
  unsigned char *rec_buf;                /* to read field values  after filesort */
  unsigned char	*cache,*cache_pos,*cache_end,*read_positions;
  internal::IO_CACHE *io_cache;
  bool print_error, ignore_not_found_rows;
  JoinTable *do_insideout_scan;
} READ_RECORD;

typedef int *(*update_var)(Session *, struct drizzle_show_var *);

} /* namespace drizzled */

	/* Bits in form->status */
#define STATUS_NO_RECORD	(1+2)	/* Record isn't usably */
#define STATUS_GARBAGE		1
#define STATUS_NOT_FOUND	2	/* No record in database when needed */
#define STATUS_NO_PARENT	4	/* Parent record wasn't found */
#define STATUS_NOT_READ		8	/* Record isn't read */
#define STATUS_UPDATED		16	/* Record is updated by formula */
#define STATUS_NULL_ROW		32	/* table->null_row is set */
#define STATUS_DELETED		64

#endif /* DRIZZLED_STRUCTS_H */
