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

#include "drizzled/server_includes.h"
#include "drizzled/session.h"
#include "drizzled/records.h"
#include "drizzled/optimizer/quick_range_select.h"
#include "drizzled/optimizer/quick_index_merge_select.h"

using namespace drizzled;

static int refpos_order_cmp(void *arg, const void *a, const void *b)
{
  Cursor *cursor= (Cursor*)arg;
  return cursor->cmp_ref((const unsigned char *) a, (const unsigned char *) b);
}

optimizer::QuickIndexMergeSelect::QuickIndexMergeSelect(Session *session_param,
                                                        Table *table)
  :
    pk_quick_select(NULL),
    session(session_param)
{
  index= MAX_KEY;
  head= table;
  memset(&read_record, 0, sizeof(read_record));
  init_sql_alloc(&alloc, session->variables.range_alloc_block_size, 0);
  return;
}

int optimizer::QuickIndexMergeSelect::init()
{
  return 0;
}

int optimizer::QuickIndexMergeSelect::reset()
{
  return (read_keys_and_merge());
}

bool
optimizer::QuickIndexMergeSelect::push_quick_back(optimizer::QuickRangeSelect *quick_sel_range)
{
  /*
    Save quick_select that does scan on clustered primary key as it will be
    processed separately.
  */
  if (head->cursor->primary_key_is_clustered() &&
      quick_sel_range->index == head->s->primary_key)
  {
    pk_quick_select= quick_sel_range;
  }
  else
  {
    return quick_selects.push_back(quick_sel_range);
  }
  return 0;
}

optimizer::QuickIndexMergeSelect::~QuickIndexMergeSelect()
{
  List_iterator_fast<optimizer::QuickRangeSelect> quick_it(quick_selects);
  optimizer::QuickRangeSelect* quick;
  quick_it.rewind();
  while ((quick= quick_it++))
  {
    quick->cursor= NULL;
  }
  quick_selects.delete_elements();
  delete pk_quick_select;
  free_root(&alloc,MYF(0));
  return;
}


int optimizer::QuickIndexMergeSelect::read_keys_and_merge()
{
  List_iterator_fast<optimizer::QuickRangeSelect> cur_quick_it(quick_selects);
  optimizer::QuickRangeSelect* cur_quick;
  int result;
  Unique *unique;
  Cursor *cursor= head->cursor;

  cursor->extra(HA_EXTRA_KEYREAD);
  head->prepare_for_position();

  cur_quick_it.rewind();
  cur_quick= cur_quick_it++;
  assert(cur_quick != 0);

  /*
    We reuse the same instance of Cursor so we need to call both init and
    reset here.
  */
  if (cur_quick->init() || cur_quick->reset())
    return 0;

  unique= new Unique(refpos_order_cmp,
                     (void *)cursor,
                     cursor->ref_length,
                     session->variables.sortbuff_size);
  if (!unique)
    return 0;
  for (;;)
  {
    while ((result= cur_quick->get_next()) == HA_ERR_END_OF_FILE)
    {
      cur_quick->range_end();
      cur_quick= cur_quick_it++;
      if (!cur_quick)
        break;

      if (cur_quick->cursor->inited != Cursor::NONE)
        cur_quick->cursor->ha_index_end();
      if (cur_quick->init() || cur_quick->reset())
        return 0;
    }

    if (result)
    {
      if (result != HA_ERR_END_OF_FILE)
      {
        cur_quick->range_end();
        return result;
      }
      break;
    }

    if (session->killed)
      return 0;

    /* skip row if it will be retrieved by clustered PK scan */
    if (pk_quick_select && pk_quick_select->row_in_ranges())
      continue;

    cur_quick->cursor->position(cur_quick->record);
    result= unique->unique_add((char*)cur_quick->cursor->ref);
    if (result)
      return 0;

  }

  /* ok, all row ids are in Unique */
  result= unique->get(head);
  delete unique;
  doing_pk_scan= false;
  /* index_merge currently doesn't support "using index" at all */
  cursor->extra(HA_EXTRA_NO_KEYREAD);
  /* start table scan */
  init_read_record(&read_record, session, head, (optimizer::SqlSelect*) 0, 1, 1);
  return result;
}


int optimizer::QuickIndexMergeSelect::get_next()
{
  int result;

  if (doing_pk_scan)
    return(pk_quick_select->get_next());

  if ((result= read_record.read_record(&read_record)) == -1)
  {
    result= HA_ERR_END_OF_FILE;
    end_read_record(&read_record);
    /* All rows from Unique have been retrieved, do a clustered PK scan */
    if (pk_quick_select)
    {
      doing_pk_scan= true;
      if ((result= pk_quick_select->init()) ||
          (result= pk_quick_select->reset()))
        return result;
      return(pk_quick_select->get_next());
    }
  }

  return result;
}

bool optimizer::QuickIndexMergeSelect::is_keys_used(const MyBitmap *fields)
{
  optimizer::QuickRangeSelect *quick= NULL;
  List_iterator_fast<QuickRangeSelect> it(quick_selects);
  while ((quick= it++))
  {
    if (is_key_used(head, quick->index, fields))
      return 1;
  }
  return 0;
}


void optimizer::QuickIndexMergeSelect::add_info_string(String *str)
{
  optimizer::QuickRangeSelect *quick= NULL;
  bool first= true;
  List_iterator_fast<optimizer::QuickRangeSelect> it(quick_selects);
  str->append(STRING_WITH_LEN("sort_union("));
  while ((quick= it++))
  {
    if (! first)
      str->append(',');
    else
      first= false;
    quick->add_info_string(str);
  }
  if (pk_quick_select)
  {
    str->append(',');
    pk_quick_select->add_info_string(str);
  }
  str->append(')');
}


void optimizer::QuickIndexMergeSelect::add_keys_and_lengths(String *key_names,
                                                            String *used_lengths)
{
  char buf[64];
  uint32_t length;
  bool first= true;
  optimizer::QuickRangeSelect *quick= NULL;

  List_iterator_fast<optimizer::QuickRangeSelect> it(quick_selects);
  while ((quick= it++))
  {
    if (first)
      first= false;
    else
    {
      key_names->append(',');
      used_lengths->append(',');
    }

    KEY *key_info= head->key_info + quick->index;
    key_names->append(key_info->name);
    length= int64_t2str(quick->max_used_key_length, buf, 10) - buf;
    used_lengths->append(buf, length);
  }
  if (pk_quick_select)
  {
    KEY *key_info= head->key_info + pk_quick_select->index;
    key_names->append(',');
    key_names->append(key_info->name);
    length= int64_t2str(pk_quick_select->max_used_key_length, buf, 10) - buf;
    used_lengths->append(',');
    used_lengths->append(buf, length);
  }
}


