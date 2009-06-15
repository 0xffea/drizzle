/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
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


#ifndef DRIZZLED_TMP_TABLE_PARAM_H
#define DRIZZLED_TMP_TABLE_PARAM_H

/*
  Param to create temporary tables when doing SELECT:s
  NOTE
    This structure is copied using memcpy as a part of JOIN.
*/

class Tmp_Table_Param :public Sql_alloc
{
private:
  /* Prevent use of these (not safe because of lists and copy_field) */
  Tmp_Table_Param(const Tmp_Table_Param &);
  void operator=(Tmp_Table_Param &);

public:
  KEY *keyinfo;
  List<Item> copy_funcs;
  List<Item> save_copy_funcs;
  CopyField *copy_field, *copy_field_end;
  CopyField *save_copy_field, *save_copy_field_end;
  unsigned char	    *group_buff;
  Item	    **items_to_copy;			/* Fields in tmp table */
  MI_COLUMNDEF *recinfo,*start_recinfo;
  ha_rows end_write_records;
  uint32_t	field_count;
  uint32_t	sum_func_count;
  uint32_t	func_count;
  uint32_t  hidden_field_count;
  uint32_t	group_parts,group_length,group_null_parts;
  uint32_t	quick_group;
  bool using_indirect_summary_function;
  bool schema_table;

  /*
    True if GROUP BY and its aggregate functions are already computed
    by a table access method (e.g. by loose index scan). In this case
    query execution should not perform aggregation and should treat
    aggregate functions as normal functions.
  */
  bool precomputed_group_by;

  bool force_copy_fields;

  /* If >0 convert all blob fields to varchar(convert_blob_length) */
  uint32_t  convert_blob_length;

  const CHARSET_INFO *table_charset;
  /*
    If true, create_tmp_field called from create_tmp_table will convert
    all BIT fields to 64-bit longs. This is a workaround the limitation
    that MEMORY tables cannot index BIT columns.
  */
  bool bit_fields_as_long;

  Tmp_Table_Param()
    :copy_field(0),
    group_parts(0),
    group_length(0),
    group_null_parts(0),
    schema_table(false),
    precomputed_group_by(false),
    force_copy_fields(false),
    convert_blob_length(0),
    bit_fields_as_long(false)
  {}
  ~Tmp_Table_Param()
  {
    cleanup();
  }
  void init(void);
  void cleanup(void);
};

#endif /* DRIZZLED_TMP_TABLE_PARAM_H */
