/* Copyright (C) 2000-2006 MySQL AB

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

#include <drizzled/server_includes.h>
#include <drizzled/error.h>
#include <drizzled/table.h>
#include <drizzled/session.h>
#include <drizzled/current_session.h>
#include <drizzled/field/timestamp.h>
#include <drizzled/field/varstring.h>

#include "heap.h"
#include "ha_heap.h"
#include "heapdef.h"

#include <string>

using namespace std;

static const string engine_name("MEMORY");

pthread_mutex_t THR_LOCK_heap= PTHREAD_MUTEX_INITIALIZER;


class HeapEngine : public StorageEngine
{
public:
  HeapEngine(string name_arg) : StorageEngine(name_arg, HTON_CAN_RECREATE)
  {
    addAlias("HEAP");
  }

  virtual handler *create(TABLE_SHARE *table,
                          MEM_ROOT *mem_root)
  {
    return new (mem_root) ha_heap(this, table);
  }
};

static HeapEngine *engine= NULL;
int heap_init(PluginRegistry &registry)
{
  engine= new HeapEngine(engine_name);
  registry.add(engine);
  pthread_mutex_init(&THR_LOCK_heap, MY_MUTEX_INIT_FAST);
  return 0;
}

int heap_deinit(PluginRegistry &registry)
{
  registry.remove(engine);
  delete engine;

  pthread_mutex_destroy(&THR_LOCK_heap);

  return hp_panic(HA_PANIC_CLOSE);
}



/*****************************************************************************
** HEAP tables
*****************************************************************************/

ha_heap::ha_heap(StorageEngine *engine_arg, TABLE_SHARE *table_arg)
  :handler(engine_arg, table_arg), file(0), records_changed(0), key_stat_version(0),
  internal_table(0)
{}


static const char *ha_heap_exts[] = {
  NULL
};

const char **ha_heap::bas_ext() const
{
  return ha_heap_exts;
}

/*
  Hash index statistics is updated (copied from HP_KEYDEF::hash_buckets to
  rec_per_key) after 1/HEAP_STATS_UPDATE_THRESHOLD fraction of table records
  have been inserted/updated/deleted. delete_all_rows() and table flush cause
  immediate update.

  NOTE
   hash index statistics must be updated when number of table records changes
   from 0 to non-zero value and vice versa. Otherwise records_in_range may
   erroneously return 0 and 'range' may miss records.
*/
#define HEAP_STATS_UPDATE_THRESHOLD 10

int ha_heap::open(const char *name, int mode, uint32_t test_if_locked)
{
  if ((test_if_locked & HA_OPEN_INTERNAL_TABLE) || (!(file= heap_open(name, mode)) && my_errno == ENOENT))
  {
    HA_CREATE_INFO create_info;
    internal_table= test(test_if_locked & HA_OPEN_INTERNAL_TABLE);
    memset(&create_info, 0, sizeof(create_info));
    file= 0;
    if (!create(name, table, &create_info))
    {
        file= internal_table ?
          heap_open_from_share(internal_share, mode) :
          heap_open_from_share_and_register(internal_share, mode);
      if (!file)
      {
         /* Couldn't open table; Remove the newly created table */
        pthread_mutex_lock(&THR_LOCK_heap);
        hp_free(internal_share);
        pthread_mutex_unlock(&THR_LOCK_heap);
      }
      implicit_emptied= 1;
    }
  }
  ref_length= sizeof(HEAP_PTR);
  if (file)
  {
    /* Initialize variables for the opened table */
    set_keys_for_scanning();
    /*
      We cannot run update_key_stats() here because we do not have a
      lock on the table. The 'records' count might just be changed
      temporarily at this moment and we might get wrong statistics (Bug
      #10178). Instead we request for update. This will be done in
      ha_heap::info(), which is always called before key statistics are
      used.
    */
    key_stat_version= file->s->key_stat_version-1;
  }
  return (file ? 0 : 1);
}

int ha_heap::close(void)
{
  return internal_table ? hp_close(file) : heap_close(file);
}


/*
  Create a copy of this table

  DESCRIPTION
    Do same as default implementation but use file->s->name instead of
    table->s->path. This is needed by Windows where the clone() call sees
    '/'-delimited path in table->s->path, while ha_peap::open() was called
    with '\'-delimited path.
*/

handler *ha_heap::clone(MEM_ROOT *mem_root)
{
  handler *new_handler= get_new_handler(table->s, mem_root, table->s->db_type());
  if (new_handler && !new_handler->ha_open(table, file->s->name, table->db_stat,
                                           HA_OPEN_IGNORE_IF_LOCKED))
    return new_handler;
  return NULL;  /* purecov: inspected */
}


const char *ha_heap::index_type(uint32_t inx)
{
  return ((table_share->key_info[inx].algorithm == HA_KEY_ALG_BTREE) ?
          "BTREE" : "HASH");
}


uint32_t ha_heap::index_flags(uint32_t inx, uint32_t, bool) const
{
  return ((table_share->key_info[inx].algorithm == HA_KEY_ALG_BTREE) ?
          HA_READ_NEXT | HA_READ_PREV | HA_READ_ORDER | HA_READ_RANGE :
          HA_ONLY_WHOLE_INDEX | HA_KEY_SCAN_NOT_ROR);
}


/*
  Compute which keys to use for scanning

  SYNOPSIS
    set_keys_for_scanning()
    no parameter

  DESCRIPTION
    Set the bitmap btree_keys, which is used when the upper layers ask
    which keys to use for scanning. For each btree index the
    corresponding bit is set.

  RETURN
    void
*/

void ha_heap::set_keys_for_scanning(void)
{
  btree_keys.clear_all();
  for (uint32_t i= 0 ; i < table->s->keys ; i++)
  {
    if (table->key_info[i].algorithm == HA_KEY_ALG_BTREE)
      btree_keys.set_bit(i);
  }
}


void ha_heap::update_key_stats()
{
  for (uint32_t i= 0; i < table->s->keys; i++)
  {
    KEY *key=table->key_info+i;
    if (!key->rec_per_key)
      continue;
    if (key->algorithm != HA_KEY_ALG_BTREE)
    {
      if (key->flags & HA_NOSAME)
        key->rec_per_key[key->key_parts-1]= 1;
      else
      {
        ha_rows hash_buckets= file->s->keydef[i].hash_buckets;
        uint32_t no_records= hash_buckets ? (uint) (file->s->records/hash_buckets) : 2;
        if (no_records < 2)
          no_records= 2;
        key->rec_per_key[key->key_parts-1]= no_records;
      }
    }
  }
  records_changed= 0;
  /* At the end of update_key_stats() we can proudly claim they are OK. */
  key_stat_version= file->s->key_stat_version;
}


int ha_heap::write_row(unsigned char * buf)
{
  int res;
  ha_statistic_increment(&SSV::ha_write_count);
  if (table->next_number_field && buf == table->record[0])
  {
    if ((res= update_auto_increment()))
      return res;
  }
  res= heap_write(file,buf);
  if (!res && (++records_changed*HEAP_STATS_UPDATE_THRESHOLD >
               file->s->records))
  {
    /*
       We can perform this safely since only one writer at the time is
       allowed on the table.
    */
    file->s->key_stat_version++;
  }
  return res;
}

int ha_heap::update_row(const unsigned char * old_data, unsigned char * new_data)
{
  int res;
  ha_statistic_increment(&SSV::ha_update_count);
  if (table->timestamp_field_type & TIMESTAMP_AUTO_SET_ON_UPDATE)
    table->timestamp_field->set_time();
  res= heap_update(file,old_data,new_data);
  if (!res && ++records_changed*HEAP_STATS_UPDATE_THRESHOLD >
              file->s->records)
  {
    /*
       We can perform this safely since only one writer at the time is
       allowed on the table.
    */
    file->s->key_stat_version++;
  }
  return res;
}

int ha_heap::delete_row(const unsigned char * buf)
{
  int res;
  ha_statistic_increment(&SSV::ha_delete_count);
  res= heap_delete(file,buf);
  if (!res && table->s->tmp_table == NO_TMP_TABLE &&
      ++records_changed*HEAP_STATS_UPDATE_THRESHOLD > file->s->records)
  {
    /*
       We can perform this safely since only one writer at the time is
       allowed on the table.
    */
    file->s->key_stat_version++;
  }
  return res;
}

int ha_heap::index_read_map(unsigned char *buf, const unsigned char *key,
                            key_part_map keypart_map,
                            enum ha_rkey_function find_flag)
{
  assert(inited==INDEX);
  ha_statistic_increment(&SSV::ha_read_key_count);
  int error = heap_rkey(file,buf,active_index, key, keypart_map, find_flag);
  table->status = error ? STATUS_NOT_FOUND : 0;
  return error;
}

int ha_heap::index_read_last_map(unsigned char *buf, const unsigned char *key,
                                 key_part_map keypart_map)
{
  assert(inited==INDEX);
  ha_statistic_increment(&SSV::ha_read_key_count);
  int error= heap_rkey(file, buf, active_index, key, keypart_map,
		       HA_READ_PREFIX_LAST);
  table->status= error ? STATUS_NOT_FOUND : 0;
  return error;
}

int ha_heap::index_read_idx_map(unsigned char *buf, uint32_t index, const unsigned char *key,
                                key_part_map keypart_map,
                                enum ha_rkey_function find_flag)
{
  ha_statistic_increment(&SSV::ha_read_key_count);
  int error = heap_rkey(file, buf, index, key, keypart_map, find_flag);
  table->status = error ? STATUS_NOT_FOUND : 0;
  return error;
}

int ha_heap::index_next(unsigned char * buf)
{
  assert(inited==INDEX);
  ha_statistic_increment(&SSV::ha_read_next_count);
  int error=heap_rnext(file,buf);
  table->status=error ? STATUS_NOT_FOUND: 0;
  return error;
}

int ha_heap::index_prev(unsigned char * buf)
{
  assert(inited==INDEX);
  ha_statistic_increment(&SSV::ha_read_prev_count);
  int error=heap_rprev(file,buf);
  table->status=error ? STATUS_NOT_FOUND: 0;
  return error;
}

int ha_heap::index_first(unsigned char * buf)
{
  assert(inited==INDEX);
  ha_statistic_increment(&SSV::ha_read_first_count);
  int error=heap_rfirst(file, buf, active_index);
  table->status=error ? STATUS_NOT_FOUND: 0;
  return error;
}

int ha_heap::index_last(unsigned char * buf)
{
  assert(inited==INDEX);
  ha_statistic_increment(&SSV::ha_read_last_count);
  int error=heap_rlast(file, buf, active_index);
  table->status=error ? STATUS_NOT_FOUND: 0;
  return error;
}

int ha_heap::rnd_init(bool scan)
{
  return scan ? heap_scan_init(file) : 0;
}

int ha_heap::rnd_next(unsigned char *buf)
{
  ha_statistic_increment(&SSV::ha_read_rnd_next_count);
  int error=heap_scan(file, buf);
  table->status=error ? STATUS_NOT_FOUND: 0;
  return error;
}

int ha_heap::rnd_pos(unsigned char * buf, unsigned char *pos)
{
  int error;
  HEAP_PTR heap_position;
  ha_statistic_increment(&SSV::ha_read_rnd_count);
  memcpy(&heap_position, pos, sizeof(HEAP_PTR));
  error=heap_rrnd(file, buf, heap_position);
  table->status=error ? STATUS_NOT_FOUND: 0;
  return error;
}

void ha_heap::position(const unsigned char *)
{
  *(HEAP_PTR*) ref= heap_position(file);	// Ref is aligned
}

int ha_heap::info(uint32_t flag)
{
  HEAPINFO hp_info;
  (void) heap_info(file,&hp_info,flag);

  errkey=                     hp_info.errkey;
  stats.records=              hp_info.records;
  stats.deleted=              hp_info.deleted;
  stats.mean_rec_length=      hp_info.reclength;
  stats.data_file_length=     hp_info.data_length;
  stats.index_file_length=    hp_info.index_length;
  stats.max_data_file_length= hp_info.max_records * hp_info.reclength;
  stats.delete_length=        hp_info.deleted * hp_info.reclength;
  if (flag & HA_STATUS_AUTO)
    stats.auto_increment_value= hp_info.auto_increment;
  /*
    If info() is called for the first time after open(), we will still
    have to update the key statistics. Hoping that a table lock is now
    in place.
  */
  if (key_stat_version != file->s->key_stat_version)
    update_key_stats();
  return 0;
}


enum row_type ha_heap::get_row_type() const
{
  if (file->s->recordspace.is_variable_size)
    return ROW_TYPE_DYNAMIC;

  return ROW_TYPE_FIXED;
}

int ha_heap::extra(enum ha_extra_function operation)
{
  return heap_extra(file,operation);
}


int ha_heap::reset()
{
  return heap_reset(file);
}


int ha_heap::delete_all_rows()
{
  heap_clear(file);
  if (table->s->tmp_table == NO_TMP_TABLE)
  {
    /*
       We can perform this safely since only one writer at the time is
       allowed on the table.
    */
    file->s->key_stat_version++;
  }
  return 0;
}

int ha_heap::external_lock(Session *, int)
{
  return 0;					// No external locking
}


/*
  Disable indexes.

  SYNOPSIS
    disable_indexes()
    mode        mode of operation:
                HA_KEY_SWITCH_NONUNIQ      disable all non-unique keys
                HA_KEY_SWITCH_ALL          disable all keys
                HA_KEY_SWITCH_NONUNIQ_SAVE dis. non-uni. and make persistent
                HA_KEY_SWITCH_ALL_SAVE     dis. all keys and make persistent

  DESCRIPTION
    Disable indexes and clear keys to use for scanning.

  IMPLEMENTATION
    HA_KEY_SWITCH_NONUNIQ       is not implemented.
    HA_KEY_SWITCH_NONUNIQ_SAVE  is not implemented with HEAP.
    HA_KEY_SWITCH_ALL_SAVE      is not implemented with HEAP.

  RETURN
    0  ok
    HA_ERR_WRONG_COMMAND  mode not implemented.
*/

int ha_heap::disable_indexes(uint32_t mode)
{
  int error;

  if (mode == HA_KEY_SWITCH_ALL)
  {
    if (!(error= heap_disable_indexes(file)))
      set_keys_for_scanning();
  }
  else
  {
    /* mode not implemented */
    error= HA_ERR_WRONG_COMMAND;
  }
  return error;
}


/*
  Enable indexes.

  SYNOPSIS
    enable_indexes()
    mode        mode of operation:
                HA_KEY_SWITCH_NONUNIQ      enable all non-unique keys
                HA_KEY_SWITCH_ALL          enable all keys
                HA_KEY_SWITCH_NONUNIQ_SAVE en. non-uni. and make persistent
                HA_KEY_SWITCH_ALL_SAVE     en. all keys and make persistent

  DESCRIPTION
    Enable indexes and set keys to use for scanning.
    The indexes might have been disabled by disable_index() before.
    The function works only if both data and indexes are empty,
    since the heap storage engine cannot repair the indexes.
    To be sure, call handler::delete_all_rows() before.

  IMPLEMENTATION
    HA_KEY_SWITCH_NONUNIQ       is not implemented.
    HA_KEY_SWITCH_NONUNIQ_SAVE  is not implemented with HEAP.
    HA_KEY_SWITCH_ALL_SAVE      is not implemented with HEAP.

  RETURN
    0  ok
    HA_ERR_CRASHED  data or index is non-empty. Delete all rows and retry.
    HA_ERR_WRONG_COMMAND  mode not implemented.
*/

int ha_heap::enable_indexes(uint32_t mode)
{
  int error;

  if (mode == HA_KEY_SWITCH_ALL)
  {
    if (!(error= heap_enable_indexes(file)))
      set_keys_for_scanning();
  }
  else
  {
    /* mode not implemented */
    error= HA_ERR_WRONG_COMMAND;
  }
  return error;
}


/*
  Test if indexes are disabled.

  SYNOPSIS
    indexes_are_disabled()
    no parameters

  RETURN
    0  indexes are not disabled
    1  all indexes are disabled
   [2  non-unique indexes are disabled - NOT YET IMPLEMENTED]
*/

int ha_heap::indexes_are_disabled(void)
{
  return heap_indexes_are_disabled(file);
}

THR_LOCK_DATA **ha_heap::store_lock(Session *,
                                    THR_LOCK_DATA **to,
                                    enum thr_lock_type lock_type)
{
  if (lock_type != TL_IGNORE && file->lock.type == TL_UNLOCK)
    file->lock.type=lock_type;
  *to++= &file->lock;
  return to;
}

/*
  We have to ignore ENOENT entries as the HEAP table is created on open and
  not when doing a CREATE on the table.
*/

int ha_heap::delete_table(const char *name)
{
  return heap_delete_table(name);
}


void ha_heap::drop_table(const char *)
{
  file->s->delete_on_close= 1;
  close();
}


int ha_heap::rename_table(const char * from, const char * to)
{
  return heap_rename(from,to);
}


ha_rows ha_heap::records_in_range(uint32_t inx, key_range *min_key,
                                  key_range *max_key)
{
  KEY *key=table->key_info+inx;
  if (key->algorithm == HA_KEY_ALG_BTREE)
    return hp_rb_records_in_range(file, inx, min_key, max_key);

  if (!min_key || !max_key ||
      min_key->length != max_key->length ||
      min_key->length != key->key_length ||
      min_key->flag != HA_READ_KEY_EXACT ||
      max_key->flag != HA_READ_AFTER_KEY)
    return HA_POS_ERROR;			// Can only use exact keys

  if (stats.records <= 1)
    return stats.records;

  /* Assert that info() did run. We need current statistics here. */
  assert(key_stat_version == file->s->key_stat_version);
  return key->rec_per_key[key->key_parts-1];
}


int ha_heap::create(const char *name, Table *table_arg,
		    HA_CREATE_INFO *create_info)
{
  uint32_t key, parts, mem_per_row_keys= 0, keys= table_arg->s->keys;
  uint32_t auto_key= 0, auto_key_type= 0;
  uint32_t max_key_fieldnr = 0, key_part_size = 0, next_field_pos = 0;
  uint32_t column_idx, column_count= table_arg->s->fields;
  HP_COLUMNDEF *columndef;
  HP_KEYDEF *keydef;
  HA_KEYSEG *seg;
  char buff[FN_REFLEN];
  int error;
  TABLE_SHARE *share= table_arg->s;
  bool found_real_auto_increment= 0;

  if (!(columndef= (HP_COLUMNDEF*) malloc(column_count * sizeof(HP_COLUMNDEF))))
    return my_errno;

  for (column_idx= 0; column_idx < column_count; column_idx++)
  {
    Field* field= *(table_arg->field + column_idx);
    HP_COLUMNDEF* column= columndef + column_idx;
    column->type= (uint16_t)field->type();
    column->length= field->pack_length();
    column->offset= field->offset(field->table->record[0]);

    if (field->null_bit)
    {
      column->null_bit= field->null_bit;
      column->null_pos= (uint) (field->null_ptr - (unsigned char*) table_arg->record[0]);
    }
    else
    {
      column->null_bit= 0;
      column->null_pos= 0;
    }

    if (field->type() == DRIZZLE_TYPE_VARCHAR)
    {
      column->length_bytes= (uint8_t)(((Field_varstring*)field)->length_bytes);
    }
    else
    {
      column->length_bytes= 0;
    }
  }

  for (key= parts= 0; key < keys; key++)
    parts+= table_arg->key_info[key].key_parts;

  if (!(keydef= (HP_KEYDEF*) malloc(keys * sizeof(HP_KEYDEF) +
				    parts * sizeof(HA_KEYSEG))))
  {
    free((void *) columndef);
    return my_errno;
  }

  seg= reinterpret_cast<HA_KEYSEG*> (keydef + keys);
  for (key= 0; key < keys; key++)
  {
    KEY *pos= table_arg->key_info+key;
    KEY_PART_INFO *key_part=     pos->key_part;
    KEY_PART_INFO *key_part_end= key_part + pos->key_parts;

    keydef[key].keysegs=   (uint) pos->key_parts;
    keydef[key].flag=      (pos->flags & (HA_NOSAME | HA_NULL_ARE_EQUAL));
    keydef[key].seg=       seg;

    switch (pos->algorithm) {
    case HA_KEY_ALG_UNDEF:
    case HA_KEY_ALG_HASH:
      keydef[key].algorithm= HA_KEY_ALG_HASH;
      mem_per_row_keys+= sizeof(char*) * 2; // = sizeof(HASH_INFO)
      break;
    case HA_KEY_ALG_BTREE:
      keydef[key].algorithm= HA_KEY_ALG_BTREE;
      mem_per_row_keys+=sizeof(TREE_ELEMENT)+pos->key_length+sizeof(char*);
      break;
    default:
      assert(0); // cannot happen
    }

    for (; key_part != key_part_end; key_part++, seg++)
    {
      Field *field= key_part->field;

      if (pos->algorithm == HA_KEY_ALG_BTREE)
	seg->type= field->key_type();
      else
      {
        if ((seg->type = field->key_type()) != (int) HA_KEYTYPE_TEXT &&
            seg->type != HA_KEYTYPE_VARTEXT1 &&
            seg->type != HA_KEYTYPE_VARTEXT2 &&
            seg->type != HA_KEYTYPE_VARBINARY1 &&
            seg->type != HA_KEYTYPE_VARBINARY2)
          seg->type= HA_KEYTYPE_BINARY;
      }
      seg->start=   (uint) key_part->offset;
      seg->length=  (uint) key_part->length;
      seg->flag=    key_part->key_part_flag;

      next_field_pos= seg->start + seg->length;
      if (field->type() == DRIZZLE_TYPE_VARCHAR)
      {
        next_field_pos+= (uint8_t)(((Field_varstring*)field)->length_bytes);
      }

      if (next_field_pos > key_part_size) {
        key_part_size= next_field_pos;
      }

      if (field->flags & (ENUM_FLAG | SET_FLAG))
        seg->charset= &my_charset_bin;
      else
        seg->charset= field->charset();
      if (field->null_ptr)
      {
	seg->null_bit= field->null_bit;
	seg->null_pos= (uint) (field->null_ptr - (unsigned char*) table_arg->record[0]);
      }
      else
      {
	seg->null_bit= 0;
	seg->null_pos= 0;
      }
      if (field->flags & AUTO_INCREMENT_FLAG &&
          table_arg->found_next_number_field &&
          key == share->next_number_index)
      {
        /*
          Store key number and type for found auto_increment key
          We have to store type as seg->type can differ from it
        */
        auto_key= key+ 1;
	auto_key_type= field->key_type();
      }
      if ((uint)field->field_index + 1 > max_key_fieldnr)
      {
        /* Do not use seg->fieldnr as it's not reliable in case of temp tables */
        max_key_fieldnr= field->field_index + 1;
      }
    }
  }

  if (key_part_size < share->null_bytes + ((share->last_null_bit_pos+7) >> 3))
  {
    /* Make sure to include null fields regardless of the presense of keys */
    key_part_size = share->null_bytes + ((share->last_null_bit_pos+7) >> 3);
  }



  if (table_arg->found_next_number_field)
  {
    keydef[share->next_number_index].flag|= HA_AUTO_KEY;
    found_real_auto_increment= share->next_number_key_offset == 0;
  }
  HP_CREATE_INFO hp_create_info;
  hp_create_info.auto_key= auto_key;
  hp_create_info.auto_key_type= auto_key_type;
  hp_create_info.auto_increment= (create_info->auto_increment_value ?
				  create_info->auto_increment_value - 1 : 0);
  hp_create_info.max_table_size=current_session->variables.max_heap_table_size;
  hp_create_info.with_auto_increment= found_real_auto_increment;
  hp_create_info.internal_table= internal_table;
  hp_create_info.max_chunk_size= share->block_size;
  hp_create_info.is_dynamic= (share->row_type == ROW_TYPE_DYNAMIC);
  error= heap_create(fn_format(buff,name,"","",
                               MY_REPLACE_EXT|MY_UNPACK_FILENAME),
                   keys, keydef,
         column_count, columndef,
         max_key_fieldnr, key_part_size,
         share->reclength, mem_per_row_keys,
         (uint32_t) share->max_rows, (uint32_t) share->min_rows,
         &hp_create_info, &internal_share);

  free((unsigned char*) keydef);
  free((void *) columndef);
  assert(file == 0);
  return (error);
}


void ha_heap::update_create_info(HA_CREATE_INFO *create_info)
{
  table->file->info(HA_STATUS_AUTO);
  if (!(create_info->used_fields & HA_CREATE_USED_AUTO))
    create_info->auto_increment_value= stats.auto_increment_value;
  if (!(create_info->used_fields & HA_CREATE_USED_BLOCK_SIZE))
  {
    if (file->s->recordspace.is_variable_size)
      create_info->block_size= file->s->recordspace.chunk_length;
    else
      create_info->block_size= 0;
  }
}

void ha_heap::get_auto_increment(uint64_t, uint64_t, uint64_t,
                                 uint64_t *first_value,
                                 uint64_t *nb_reserved_values)
{
  ha_heap::info(HA_STATUS_AUTO);
  *first_value= stats.auto_increment_value;
  /* such table has only table-level locking so reserves up to +inf */
  *nb_reserved_values= UINT64_MAX;
}


int ha_heap::cmp_ref(const unsigned char *ref1, const unsigned char *ref2)
{
  return memcmp(ref1, ref2, sizeof(HEAP_PTR));
}


bool ha_heap::check_if_incompatible_data(HA_CREATE_INFO *info_in,
					 uint32_t table_changes)
{
  /* Check that auto_increment value was not changed */
  if ((info_in->used_fields & HA_CREATE_USED_AUTO &&
       info_in->auto_increment_value != 0) ||
      table_changes == IS_EQUAL_NO ||
      table_changes & IS_EQUAL_PACK_LENGTH) // Not implemented yet
    return COMPATIBLE_DATA_NO;
  return COMPATIBLE_DATA_YES;
}

drizzle_declare_plugin(heap)
{
  "MEMORY",
  "1.0",
  "MySQL AB",
  "Hash based, stored in memory, useful for temporary tables",
  PLUGIN_LICENSE_GPL,
  heap_init,
  heap_deinit,
  NULL,                       /* status variables                */
  NULL,                       /* system variables                */
  NULL                        /* config options                  */
}
drizzle_declare_plugin_end;
