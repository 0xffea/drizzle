/* Copyright (C) 2000,2004 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/* This file should be included when using heap_database_functions */
/* Author: Michael Widenius */

#ifndef _heap_h
#define _heap_h
#ifdef	__cplusplus
extern "C" {
#endif

#include <drizzled/base.h>
#include <drizzled/common.h>
#include <mysys/my_pthread.h>
#include <mysys/thr_lock.h>

#include <storage/myisam/my_handler.h>
#include <mysys/my_tree.h>

	/* defines used by heap-funktions */

#define HP_MAX_LEVELS	4		/* 128^5 records is enough */
#define HP_PTRS_IN_NOD	128

	/* struct used with heap_funktions */

typedef struct st_heapinfo		/* Struct from heap_info */
{
  uint32_t records;			/* Records in database */
  uint32_t deleted;			/* Deleted records in database */
  uint32_t max_records;
  uint64_t data_length;
  uint64_t index_length;
  uint32_t reclength;			/* Length of one record */
  int errkey;
  uint64_t auto_increment;
} HEAPINFO;


	/* Structs used by heap-database-handler */

typedef struct st_heap_ptrs
{
  unsigned char *blocks[HP_PTRS_IN_NOD];		/* pointers to HP_PTRS or records */
} HP_PTRS;

struct st_level_info
{
  /* Number of unused slots in *last_blocks HP_PTRS block (0 for 0th level) */
  uint32_t free_ptrs_in_block;

  /*
    Maximum number of records that can be 'contained' inside of each element
    of last_blocks array. For level 0 - 1, for level 1 - HP_PTRS_IN_NOD, for
    level 2 - HP_PTRS_IN_NOD^2 and so forth.
  */
  uint32_t records_under_level;

  /*
    Ptr to last allocated HP_PTRS (or records buffer for level 0) on this
    level.
  */
  HP_PTRS *last_blocks;
};


/*
  Heap table records and hash index entries are stored in HP_BLOCKs.
  HP_BLOCK is used as a 'growable array' of fixed-size records. Size of record
  is recbuffer bytes.
  The internal representation is as follows:
  HP_BLOCK is a hierarchical structure of 'blocks'.
  A block at level 0 is an array records_in_block records.
  A block at higher level is an HP_PTRS structure with pointers to blocks at
  lower levels.
  At the highest level there is one top block. It is stored in HP_BLOCK::root.

  See hp_find_block for a description of how record pointer is obtained from
  its index.
  See hp_get_new_block
*/

typedef struct st_heap_block
{
  HP_PTRS *root;                        /* Top-level block */
  struct st_level_info level_info[HP_MAX_LEVELS+1];
  uint32_t levels;                          /* number of used levels */
  uint32_t records_in_block;		/* Records in one heap-block */
  uint32_t recbuffer;			/* Length of one saved record */
  uint32_t last_allocated; /* number of records there is allocated space for */
} HP_BLOCK;

struct st_heap_info;			/* For referense */

typedef struct st_hp_keydef		/* Key definition with open */
{
  uint32_t flag;				/* HA_NOSAME | HA_NULL_PART_KEY */
  uint32_t keysegs;				/* Number of key-segment */
  uint32_t length;				/* Length of key (automatic) */
  uint8_t algorithm;			/* HASH / BTREE */
  HA_KEYSEG *seg;
  HP_BLOCK block;			/* Where keys are saved */
  /*
    Number of buckets used in hash table. Used only to provide
    #records estimates for heap key scans.
  */
  ha_rows hash_buckets;
  TREE rb_tree;
  int (*write_key)(struct st_heap_info *info, struct st_hp_keydef *keyinfo,
		   const unsigned char *record, unsigned char *recpos);
  int (*delete_key)(struct st_heap_info *info, struct st_hp_keydef *keyinfo,
		   const unsigned char *record, unsigned char *recpos, int flag);
  uint32_t (*get_key_length)(struct st_hp_keydef *keydef, const unsigned char *key);
} HP_KEYDEF;

typedef struct st_heap_columndef              /* column information */
{
  int16_t  type;                                /* en_fieldtype */
  uint32_t length;                      /* length of field */
  uint32_t offset;                      /* Offset to position in row */
  uint8_t  null_bit;                    /* If column may be 0 */
  uint16_t null_pos;                    /* position for null marker */
  uint8_t  length_bytes;  /* length of the size, 1 o 2 bytes */
} HP_COLUMNDEF;

typedef struct st_heap_dataspace   /* control data for data space */
{
  HP_BLOCK block;
  uint32_t chunk_count;             /* Total chunks ever allocated in this dataspace */
  uint32_t del_chunk_count;         /* Deleted chunks count */
  unsigned char *del_link;               /* Link to last deleted chunk */
  uint32_t chunk_length;            /* Total length of one chunk */
  uint32_t chunk_dataspace_length;  /* Length of payload that will be placed into one chunk */
  uint32_t offset_status;           /* Offset of the status flag relative to the chunk start */
  uint32_t offset_link;             /* Offset of the linking pointer relative to the chunk start */
  uint32_t is_variable_size;          /* Test whether records have variable size and so "next" pointer */
  uint64_t total_data_length;  /* Total size allocated within this data space */
} HP_DATASPACE;


typedef struct st_heap_share
{
  HP_KEYDEF  *keydef;
  HP_COLUMNDEF *column_defs;
  HP_DATASPACE recordspace;  /* Describes "block", which contains actual records */

  uint32_t min_records,max_records;	/* Params to open */
  uint64_t index_length,max_table_size;
  uint32_t key_stat_version;                /* version to indicate insert/delete */
  uint32_t records;             /* Actual record (row) count */
  uint32_t blength;                                     /* used_chunk_count rounded up to 2^n */
  uint32_t fixed_data_length;     /* Length of record's fixed part, which contains keys and always fits into the first chunk */
  uint32_t fixed_column_count;  /* Number of columns stored in fixed_data_length */
  uint32_t changed;
  uint32_t keys,max_key_length;
  uint32_t column_count;
  uint32_t currently_disabled_keys;    /* saved value from "keys" when disabled */
  uint32_t open_count;


  char * name;			/* Name of "memory-file" */
  THR_LOCK lock;
  pthread_mutex_t intern_lock;		/* Locking for use with _locking */
  bool delete_on_close;
  LIST open_list;
  uint32_t auto_key;
  uint32_t auto_key_type;			/* real type of the auto key segment */
  uint64_t auto_increment;
} HP_SHARE;

struct st_hp_hash_info;

typedef struct st_heap_info
{
  HP_SHARE *s;
  unsigned char *current_ptr;
  struct st_hp_hash_info *current_hash_ptr;
  uint32_t current_record,next_block;
  int lastinx,errkey;
  int  mode;				/* Mode of file (READONLY..) */
  uint32_t opt_flag,update;
  unsigned char *lastkey;			/* Last used key with rkey */
  unsigned char *recbuf;                         /* Record buffer for rb-tree keys */
  enum ha_rkey_function last_find_flag;
  TREE_ELEMENT *parents[MAX_TREE_HEIGHT+1];
  TREE_ELEMENT **last_pos;
  uint32_t lastkey_len;
  bool implicit_emptied;
  THR_LOCK_DATA lock;
  LIST open_list;
} HP_INFO;


typedef struct st_heap_create_info
{
  uint32_t auto_key;                        /* keynr [1 - maxkey] for auto key */
  uint32_t auto_key_type;
  uint32_t max_chunk_size;
  uint32_t is_dynamic;
  uint64_t max_table_size;
  uint64_t auto_increment;
  bool with_auto_increment;
  bool internal_table;
} HP_CREATE_INFO;

	/* Prototypes for heap-functions */

extern HP_INFO *heap_open(const char *name, int mode);
extern HP_INFO *heap_open_from_share(HP_SHARE *share, int mode);
extern HP_INFO *heap_open_from_share_and_register(HP_SHARE *share, int mode);
extern int heap_close(HP_INFO *info);
extern int heap_write(HP_INFO *info,const unsigned char *record);
extern int heap_update(HP_INFO *info,const unsigned char *old_record,const unsigned char *new_record);
extern int heap_rrnd(HP_INFO *info,unsigned char *buf,unsigned char *pos);
extern int heap_scan_init(HP_INFO *info);
extern int heap_scan(register HP_INFO *info, unsigned char *record);
extern int heap_delete(HP_INFO *info,const unsigned char *buff);
extern int heap_info(HP_INFO *info,HEAPINFO *x,int flag);
extern int heap_create(const char *name, uint32_t keys, HP_KEYDEF *keydef,
           uint32_t columns, HP_COLUMNDEF *columndef,
           uint32_t max_key_fieldnr, uint32_t key_part_size,
           uint32_t reclength, uint32_t keys_memory_size,
           uint32_t max_records, uint32_t min_records,
           HP_CREATE_INFO *create_info, HP_SHARE **share);

extern int heap_delete_table(const char *name);
extern void heap_drop_table(HP_INFO *info);
extern int heap_extra(HP_INFO *info,enum ha_extra_function function);
extern int heap_reset(HP_INFO *info);
extern int heap_rename(const char *old_name,const char *new_name);
extern int heap_panic(enum ha_panic_function flag);
extern int heap_rsame(HP_INFO *info,unsigned char *record,int inx);
extern int heap_rnext(HP_INFO *info,unsigned char *record);
extern int heap_rprev(HP_INFO *info,unsigned char *record);
extern int heap_rfirst(HP_INFO *info,unsigned char *record,int inx);
extern int heap_rlast(HP_INFO *info,unsigned char *record,int inx);
extern void heap_clear(HP_INFO *info);
extern void heap_clear_keys(HP_INFO *info);
extern int heap_disable_indexes(HP_INFO *info);
extern int heap_enable_indexes(HP_INFO *info);
extern int heap_indexes_are_disabled(HP_INFO *info);
extern void heap_update_auto_increment(HP_INFO *info, const unsigned char *record);
ha_rows hp_rb_records_in_range(HP_INFO *info, int inx, key_range *min_key,
                               key_range *max_key);
int hp_panic(enum ha_panic_function flag);
int heap_rkey(HP_INFO *info, unsigned char *record, int inx, const unsigned char *key,
              key_part_map keypart_map, enum ha_rkey_function find_flag);
extern unsigned char * heap_find(HP_INFO *info,int inx,const unsigned char *key);
extern int heap_check_heap(HP_INFO *info, bool print_status);
extern unsigned char *heap_position(HP_INFO *info);

/* The following is for programs that uses the old HEAP interface where
   pointer to rows where a long instead of a (unsigned char*).
*/

#if defined(WANT_OLD_HEAP_VERSION) || defined(OLD_HEAP_VERSION)
extern int heap_rrnd_old(HP_INFO *info,unsigned char *buf,uint32_t pos);
extern uint32_t heap_position_old(HP_INFO *info);
#endif
#ifdef OLD_HEAP_VERSION
typedef uint32_t HEAP_PTR;
#define heap_position(A) heap_position_old(A)
#define heap_rrnd(A,B,C) heap_rrnd_old(A,B,C)
#else
typedef unsigned char *HEAP_PTR;
#endif

#ifdef	__cplusplus
}
#endif
#endif
