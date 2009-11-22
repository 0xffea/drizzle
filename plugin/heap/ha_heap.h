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



/* class for the the heap Cursor */

#ifndef PLUGIN_HEAP_HA_HEAP_H
#define PLUGIN_HEAP_HA_HEAP_H

#include <drizzled/cursor.h>
#include <mysys/thr_lock.h>

typedef struct st_heap_info HP_INFO;
typedef unsigned char *HEAP_PTR;


class ha_heap: public Cursor
{
  HP_INFO *file;
  key_map btree_keys;
  /* number of records changed since last statistics update */
  uint32_t    records_changed;
  uint32_t    key_stat_version;
  bool internal_table;
public:
  ha_heap(drizzled::plugin::StorageEngine &engine, TableShare &table);
  ~ha_heap() {}
  Cursor *clone(MEM_ROOT *mem_root);

  const char *index_type(uint32_t inx);
  enum row_type get_row_type() const;
  uint32_t index_flags(uint32_t inx, uint32_t part, bool all_parts) const;
  const key_map *keys_to_use_for_scanning() { return &btree_keys; }
  uint32_t max_supported_keys()          const { return MAX_KEY; }
  uint32_t max_supported_key_part_length() const { return MAX_KEY_LENGTH; }
  double scan_time()
  { return (double) (stats.records+stats.deleted) / 20.0+10; }
  double read_time(uint32_t, uint32_t,
                   ha_rows rows)
  { return (double) rows /  20.0+1; }

  int open(const char *name, int mode, uint32_t test_if_locked);
  int close(void);
  void set_keys_for_scanning(void);
  int write_row(unsigned char * buf);
  int update_row(const unsigned char * old_data, unsigned char * new_data);
  int delete_row(const unsigned char * buf);
  virtual void get_auto_increment(uint64_t offset, uint64_t increment,
                                  uint64_t nb_desired_values,
                                  uint64_t *first_value,
                                  uint64_t *nb_reserved_values);
  int index_read_map(unsigned char * buf, const unsigned char * key,
                     key_part_map keypart_map,
                     enum ha_rkey_function find_flag);
  int index_read_last_map(unsigned char *buf, const unsigned char *key,
                          key_part_map keypart_map);
  int index_read_idx_map(unsigned char * buf, uint32_t index,
                         const unsigned char * key,
                         key_part_map keypart_map,
                         enum ha_rkey_function find_flag);
  int index_next(unsigned char * buf);
  int index_prev(unsigned char * buf);
  int index_first(unsigned char * buf);
  int index_last(unsigned char * buf);
  int rnd_init(bool scan);
  int rnd_next(unsigned char *buf);
  int rnd_pos(unsigned char * buf, unsigned char *pos);
  void position(const unsigned char *record);
  int info(uint);
  int extra(enum ha_extra_function operation);
  int reset();
  int delete_all_rows(void);
  int disable_indexes(uint32_t mode);
  int enable_indexes(uint32_t mode);
  int indexes_are_disabled(void);
  ha_rows records_in_range(uint32_t inx, key_range *min_key, key_range *max_key);
  void drop_table(const char *name);

  int cmp_ref(const unsigned char *ref1, const unsigned char *ref2);
  int reset_auto_increment(uint64_t value)
  {
    file->s->auto_increment= value;
    return 0;
  }
private:
  void update_key_stats();
};

#endif /* PLUGIN_HEAP_HA_HEAP_H */
