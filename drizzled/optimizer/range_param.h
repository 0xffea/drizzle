/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008-2009 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
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

#ifndef DRIZZLED_OPTIMIZER_RANGE_PARAM_H
#define DRIZZLED_OPTIMIZER_RANGE_PARAM_H

#include "drizzled/field.h"

typedef class Item COND;
typedef struct st_key_part KEY_PART;

namespace drizzled
{

namespace optimizer
{

class RangeParameter
{
public:
  Session	*session;   /* Current thread handle */
  Table *table; /* Table being analyzed */
  COND *cond;   /* Used inside get_mm_tree(). */
  table_map prev_tables;
  table_map read_tables;
  table_map current_table; /* Bit of the table being analyzed */

  /* Array of parts of all keys for which range analysis is performed */
  KEY_PART *key_parts;
  KEY_PART *key_parts_end;
  drizzled::memory::Root *mem_root; /* Memory that will be freed when range analysis completes */
  drizzled::memory::Root *old_root; /* Memory that will last until the query end */
  /*
    Number of indexes used in range analysis (In SEL_TREE::keys only first
    #keys elements are not empty)
  */
  uint32_t keys;

  /*
    If true, the index descriptions describe real indexes (and it is ok to
    call field->optimize_range(real_keynr[...], ...).
    Otherwise index description describes fake indexes.
  */
  bool using_real_indexes;

  bool remove_jump_scans;

  /*
    used_key_no -> table_key_no translation table. Only makes sense if
    using_real_indexes==true
  */
  uint32_t real_keynr[MAX_KEY];
  /* Number of SEL_ARG objects allocated by optimizer::SEL_ARG::clone_tree operations */
  uint32_t alloced_sel_args;
  bool force_default_mrr;
};

class Parameter : public RangeParameter
{
public:
  KEY_PART *key[MAX_KEY]; /* First key parts of keys used in the query */
  uint32_t max_key_part;
  /* Number of ranges in the last checked tree->key */
  uint32_t range_count;
  unsigned char min_key[MAX_KEY_LENGTH+MAX_FIELD_WIDTH],
    max_key[MAX_KEY_LENGTH+MAX_FIELD_WIDTH];
  bool quick;				// Don't calulate possible keys

  uint32_t fields_bitmap_size;
  MyBitmap needed_fields;    /* bitmask of fields needed by the query */
  MyBitmap tmp_covered_fields;

  key_map *needed_reg;        /* ptr to SqlSelect::needed_reg */

  uint32_t *imerge_cost_buff;     /* buffer for index_merge cost estimates */
  uint32_t imerge_cost_buff_size; /* size of the buffer */

  /* true if last checked tree->key can be used for ROR-scan */
  bool is_ror_scan;
  /* Number of ranges in the last checked tree->key */
  uint32_t n_ranges;
};

} /* namespace optimizer */

} /* namespace drizzled */

#endif /* DRIZZLED_OPTIMIZER_RANGE_PARAM_H */
