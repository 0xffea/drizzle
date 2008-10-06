/* Copyright (C) 2000-2003 MySQL AB

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

/*
  UNION  of select's
  UNION's  were introduced by Monty and Sinisa <sinisa@mysql.com>
*/
#include <drizzled/server_includes.h>
#include <drizzled/sql_select.h>
#include <drizzled/drizzled_error_messages.h>

bool mysql_union(THD *thd,
                 LEX *lex __attribute__((unused)),
                 select_result *result,
                 SELECT_LEX_UNIT *unit, ulong setup_tables_done_option)
{
  bool res;
  if (!(res= unit->prepare(thd, result, SELECT_NO_UNLOCK |
                           setup_tables_done_option)))
    res= unit->exec();
  if (res)
    res|= unit->cleanup();
  return(res);
}


/***************************************************************************
** store records in temporary table for UNION
***************************************************************************/

int select_union::prepare(List<Item> &list __attribute__((unused)),
                          SELECT_LEX_UNIT *u)
{
  unit= u;
  return 0;
}


bool select_union::send_data(List<Item> &values)
{
  int error= 0;
  if (unit->offset_limit_cnt)
  {						// using limit offset,count
    unit->offset_limit_cnt--;
    return 0;
  }
  fill_record(thd, table->field, values, 1);
  if (thd->is_error())
    return 1;

  if ((error= table->file->ha_write_row(table->record[0])))
  {
    /* create_myisam_from_heap will generate error if needed */
    if (table->file->is_fatal_error(error, HA_CHECK_DUP) &&
        create_myisam_from_heap(thd, table, tmp_table_param.start_recinfo, 
                                &tmp_table_param.recinfo, error, 1))
      return 1;
  }
  return 0;
}


bool select_union::send_eof()
{
  return 0;
}


bool select_union::flush()
{
  int error;
  if ((error=table->file->extra(HA_EXTRA_NO_CACHE)))
  {
    table->file->print_error(error, MYF(0));
    return 1;
  }
  return 0;
}

/*
  Create a temporary table to store the result of select_union.

  SYNOPSIS
    select_union::create_result_table()
      thd                thread handle
      column_types       a list of items used to define columns of the
                         temporary table
      is_union_distinct  if set, the temporary table will eliminate
                         duplicates on insert
      options            create options
      table_alias        name of the temporary table
      bit_fields_as_long convert bit fields to uint64_t

  DESCRIPTION
    Create a temporary table that is used to store the result of a UNION,
    derived table, or a materialized cursor.

  RETURN VALUE
    0                    The table has been created successfully.
    1                    create_tmp_table failed.
*/

bool
select_union::create_result_table(THD *thd_arg, List<Item> *column_types,
                                  bool is_union_distinct, uint64_t options,
                                  const char *table_alias,
                                  bool bit_fields_as_long)
{
  assert(table == 0);
  tmp_table_param.init();
  tmp_table_param.field_count= column_types->elements;
  tmp_table_param.bit_fields_as_long= bit_fields_as_long;

  if (! (table= create_tmp_table(thd_arg, &tmp_table_param, *column_types,
                                 (order_st*) 0, is_union_distinct, 1,
                                 options, HA_POS_ERROR, (char*) table_alias)))
    return true;
  table->file->extra(HA_EXTRA_WRITE_CACHE);
  table->file->extra(HA_EXTRA_IGNORE_DUP_KEY);
  return false;
}


/**
  Reset and empty the temporary table that stores the materialized query result.

  @note The cleanup performed here is exactly the same as for the two temp
  tables of JOIN - exec_tmp_table_[1 | 2].
*/

void select_union::cleanup()
{
  table->file->extra(HA_EXTRA_RESET_STATE);
  table->file->ha_delete_all_rows();
  free_io_cache(table);
  filesort_free_buffers(table,0);
}


/*
  initialization procedures before fake_select_lex preparation()

  SYNOPSIS
    st_select_lex_unit::init_prepare_fake_select_lex()
    thd		- thread handler

  RETURN
    options of SELECT
*/

void
st_select_lex_unit::init_prepare_fake_select_lex(THD *thd_arg) 
{
  thd_arg->lex->current_select= fake_select_lex;
  fake_select_lex->table_list.link_in_list((unsigned char *)&result_table_list,
					   (unsigned char **)
					   &result_table_list.next_local);
  fake_select_lex->context.table_list= 
    fake_select_lex->context.first_name_resolution_table= 
    fake_select_lex->get_table_list();

  for (order_st *order= (order_st *) global_parameters->order_list.first;
       order;
       order= order->next)
    order->item= &order->item_ptr;

  for (order_st *order= (order_st *)global_parameters->order_list.first;
       order;
       order=order->next)
  {
    (*order->item)->walk(&Item::change_context_processor, 0,
                         (unsigned char*) &fake_select_lex->context);
  }
}


bool st_select_lex_unit::prepare(THD *thd_arg, select_result *sel_result,
                                 uint32_t additional_options)
{
  SELECT_LEX *lex_select_save= thd_arg->lex->current_select;
  SELECT_LEX *sl, *first_sl= first_select();
  select_result *tmp_result;
  bool is_union_select;
  Table *empty_table= 0;

  describe= test(additional_options & SELECT_DESCRIBE);

  /*
    result object should be reassigned even if preparing already done for
    max/min subquery (ALL/ANY optimization)
  */
  result= sel_result;

  if (prepared)
  {
    if (describe)
    {
      /* fast reinit for EXPLAIN */
      for (sl= first_sl; sl; sl= sl->next_select())
      {
	sl->join->result= result;
	select_limit_cnt= HA_POS_ERROR;
	offset_limit_cnt= 0;
	if (result->prepare(sl->join->fields_list, this))
	{
	  return(true);
	}
	sl->join->select_options|= SELECT_DESCRIBE;
	sl->join->reinit();
      }
    }
    return(false);
  }
  prepared= 1;
  saved_error= false;
  
  thd_arg->lex->current_select= sl= first_sl;
  found_rows_for_union= first_sl->options & OPTION_FOUND_ROWS;
  is_union_select= is_union() || fake_select_lex;

  /* Global option */

  if (is_union_select)
  {
    if (!(tmp_result= union_result= new select_union))
      goto err;
    if (describe)
      tmp_result= sel_result;
  }
  else
    tmp_result= sel_result;

  sl->context.resolve_in_select_list= true;

  for (;sl; sl= sl->next_select())
  {
    bool can_skip_order_by;
    sl->options|=  SELECT_NO_UNLOCK;
    JOIN *join= new JOIN(thd_arg, sl->item_list, 
			 sl->options | thd_arg->options | additional_options,
			 tmp_result);
    /*
      setup_tables_done_option should be set only for very first SELECT,
      because it protect from secont setup_tables call for select-like non
      select commands (DELETE/INSERT/...) and they use only very first
      SELECT (for union it can be only INSERT ... SELECT).
    */
    additional_options&= ~OPTION_SETUP_TABLES_DONE;
    if (!join)
      goto err;

    thd_arg->lex->current_select= sl;

    can_skip_order_by= is_union_select && !(sl->braces && sl->explicit_limit);

    saved_error= join->prepare(&sl->ref_pointer_array,
                               (TableList*) sl->table_list.first,
                               sl->with_wild,
                               sl->where,
                               (can_skip_order_by ? 0 :
                                sl->order_list.elements) +
                               sl->group_list.elements,
                               can_skip_order_by ?
                               (order_st*) 0 : (order_st *)sl->order_list.first,
                               (order_st*) sl->group_list.first,
                               sl->having,
                               (is_union_select ? (order_st*) 0 :
                                (order_st*) thd_arg->lex->proc_list.first),
                               sl, this);
    /* There are no * in the statement anymore (for PS) */
    sl->with_wild= 0;

    if (saved_error || (saved_error= thd_arg->is_fatal_error))
      goto err;
    /*
      Use items list of underlaid select for derived tables to preserve
      information about fields lengths and exact types
    */
    if (!is_union_select)
      types= first_sl->item_list;
    else if (sl == first_sl)
    {
      /*
        We need to create an empty table object. It is used
        to create tmp_table fields in Item_type_holder.
        The main reason of this is that we can't create
        field object without table.
      */
      assert(!empty_table);
      empty_table= (Table*) thd->calloc(sizeof(Table));
      types.empty();
      List_iterator_fast<Item> it(sl->item_list);
      Item *item_tmp;
      while ((item_tmp= it++))
      {
	/* Error's in 'new' will be detected after loop */
	types.push_back(new Item_type_holder(thd_arg, item_tmp));
      }

      if (thd_arg->is_fatal_error)
	goto err; // out of memory
    }
    else
    {
      if (types.elements != sl->item_list.elements)
      {
	my_message(ER_WRONG_NUMBER_OF_COLUMNS_IN_SELECT,
		   ER(ER_WRONG_NUMBER_OF_COLUMNS_IN_SELECT),MYF(0));
	goto err;
      }
      List_iterator_fast<Item> it(sl->item_list);
      List_iterator_fast<Item> tp(types);	
      Item *type, *item_tmp;
      while ((type= tp++, item_tmp= it++))
      {
        if (((Item_type_holder*)type)->join_types(thd_arg, item_tmp))
	  return(true);
      }
    }
  }

  if (is_union_select)
  {
    /*
      Check that it was possible to aggregate
      all collations together for UNION.
    */
    List_iterator_fast<Item> tp(types);
    Item *type;
    uint64_t create_options;

    while ((type= tp++))
    {
      if (type->result_type() == STRING_RESULT &&
          type->collation.derivation == DERIVATION_NONE)
      {
        my_error(ER_CANT_AGGREGATE_NCOLLATIONS, MYF(0), "UNION");
        goto err;
      }
    }
    
    create_options= (first_sl->options | thd_arg->options |
                     TMP_TABLE_ALL_COLUMNS);

    if (union_result->create_result_table(thd, &types, test(union_distinct),
                                          create_options, "", false))
      goto err;
    memset(&result_table_list, 0, sizeof(result_table_list));
    result_table_list.db= (char*) "";
    result_table_list.table_name= result_table_list.alias= (char*) "union";
    result_table_list.table= table= union_result->table;

    thd_arg->lex->current_select= lex_select_save;
    if (!item_list.elements)
    {
      saved_error= table->fill_item_list(&item_list);
      if (saved_error)
        goto err;
    }
    else
    {
      /*
        We're in execution of a prepared statement or stored procedure:
        reset field items to point at fields from the created temporary table.
      */
      assert(1);
    }
  }

  thd_arg->lex->current_select= lex_select_save;

  return(saved_error || thd_arg->is_fatal_error);

err:
  thd_arg->lex->current_select= lex_select_save;
  return(true);
}


bool st_select_lex_unit::exec()
{
  SELECT_LEX *lex_select_save= thd->lex->current_select;
  SELECT_LEX *select_cursor=first_select();
  uint64_t add_rows=0;
  ha_rows examined_rows= 0;

  if (executed && !uncacheable && !describe)
    return(false);
  executed= 1;
  
  if (uncacheable || !item || !item->assigned() || describe)
  {
    if (item)
      item->reset_value_registration();
    if (optimized && item)
    {
      if (item->assigned())
      {
        item->assigned(0); // We will reinit & rexecute unit
        item->reset();
        table->file->ha_delete_all_rows();
      }
      /* re-enabling indexes for next subselect iteration */
      if (union_distinct && table->file->ha_enable_indexes(HA_KEY_SWITCH_ALL))
      {
        assert(0);
      }
    }
    for (SELECT_LEX *sl= select_cursor; sl; sl= sl->next_select())
    {
      ha_rows records_at_start= 0;
      thd->lex->current_select= sl;

      if (optimized)
	saved_error= sl->join->reinit();
      else
      {
        set_limit(sl);
	if (sl == global_parameters || describe)
	{
	  offset_limit_cnt= 0;
	  /*
	    We can't use LIMIT at this stage if we are using order_st BY for the
	    whole query
	  */
	  if (sl->order_list.first || describe)
	    select_limit_cnt= HA_POS_ERROR;
        }

        /*
          When using braces, SQL_CALC_FOUND_ROWS affects the whole query:
          we don't calculate found_rows() per union part.
          Otherwise, SQL_CALC_FOUND_ROWS should be done on all sub parts.
        */
        sl->join->select_options= 
          (select_limit_cnt == HA_POS_ERROR || sl->braces) ?
          sl->options & ~OPTION_FOUND_ROWS : sl->options | found_rows_for_union;

        /* dump_TableList_struct(select_lex, select_lex->leaf_tables); */
        if (sl->join->flatten_subqueries())
          return(true);

        /* dump_TableList_struct(select_lex, select_lex->leaf_tables); */
	saved_error= sl->join->optimize();
      }
      if (!saved_error)
      {
	records_at_start= table->file->stats.records;
	sl->join->exec();
        if (sl == union_distinct)
	{
	  if (table->file->ha_disable_indexes(HA_KEY_SWITCH_ALL))
	    return(true);
	  table->no_keyread=1;
	}
	saved_error= sl->join->error;
	offset_limit_cnt= (ha_rows)(sl->offset_limit ?
                                    sl->offset_limit->val_uint() :
                                    0);
	if (!saved_error)
	{
	  examined_rows+= thd->examined_row_count;
	  if (union_result->flush())
	  {
	    thd->lex->current_select= lex_select_save;
	    return(1);
	  }
	}
      }
      if (saved_error)
      {
	thd->lex->current_select= lex_select_save;
	return(saved_error);
      }
      /* Needed for the following test and for records_at_start in next loop */
      int error= table->file->info(HA_STATUS_VARIABLE);
      if(error)
      {
        table->file->print_error(error, MYF(0));
        return(1);
      }
      if (found_rows_for_union && !sl->braces && 
          select_limit_cnt != HA_POS_ERROR)
      {
	/*
	  This is a union without braces. Remember the number of rows that
	  could also have been part of the result set.
	  We get this from the difference of between total number of possible
	  rows and actual rows added to the temporary table.
	*/
	add_rows+= (uint64_t) (thd->limit_found_rows - (uint64_t)
			      ((table->file->stats.records -  records_at_start)));
      }
    }
  }
  optimized= 1;

  /* Send result to 'result' */
  saved_error= true;
  {
    if (!thd->is_fatal_error)				// Check if EOM
    {
      set_limit(global_parameters);
      init_prepare_fake_select_lex(thd);
      JOIN *join= fake_select_lex->join;
      if (!join)
      {
	/*
	  allocate JOIN for fake select only once (prevent
	  mysql_select automatic allocation)
          TODO: The above is nonsense. mysql_select() will not allocate the
          join if one already exists. There must be some other reason why we
          don't let it allocate the join. Perhaps this is because we need
          some special parameter values passed to join constructor?
	*/
	if (!(fake_select_lex->join= new JOIN(thd, item_list,
					      fake_select_lex->options, result)))
	{
	  fake_select_lex->table_list.empty();
	  return(true);
	}
        fake_select_lex->join->no_const_tables= true;

	/*
	  Fake st_select_lex should have item list for correctref_array
	  allocation.
	*/
	fake_select_lex->item_list= item_list;
        saved_error= mysql_select(thd, &fake_select_lex->ref_pointer_array,
                              &result_table_list,
                              0, item_list, NULL,
                              global_parameters->order_list.elements,
                              (order_st*)global_parameters->order_list.first,
                              (order_st*) NULL, NULL, (order_st*) NULL,
                              fake_select_lex->options | SELECT_NO_UNLOCK,
                              result, this, fake_select_lex);
      }
      else
      {
        if (describe)
        {
          /*
            In EXPLAIN command, constant subqueries that do not use any
            tables are executed two times:
             - 1st time is a real evaluation to get the subquery value
             - 2nd time is to produce EXPLAIN output rows.
            1st execution sets certain members (e.g. select_result) to perform
            subquery execution rather than EXPLAIN line production. In order 
            to reset them back, we re-do all of the actions (yes it is ugly):
          */
	  join->init(thd, item_list, fake_select_lex->options, result);
          saved_error= mysql_select(thd, &fake_select_lex->ref_pointer_array,
                                &result_table_list,
                                0, item_list, NULL,
                                global_parameters->order_list.elements,
                                (order_st*)global_parameters->order_list.first,
                                (order_st*) NULL, NULL, (order_st*) NULL,
                                fake_select_lex->options | SELECT_NO_UNLOCK,
                                result, this, fake_select_lex);
        }
        else
        {
          join->examined_rows= 0;
          saved_error= join->reinit();
          join->exec();
        }
      }

      fake_select_lex->table_list.empty();
      if (!saved_error)
      {
	thd->limit_found_rows = (uint64_t)table->file->stats.records + add_rows;
        thd->examined_row_count+= examined_rows;
      }
      /*
	Mark for slow query log if any of the union parts didn't use
	indexes efficiently
      */
    }
  }
  thd->lex->current_select= lex_select_save;
  return(saved_error);
}


bool st_select_lex_unit::cleanup()
{
  int error= 0;

  if (cleaned)
  {
    return(false);
  }
  cleaned= 1;

  if (union_result)
  {
    delete union_result;
    union_result=0; // Safety
    if (table)
      table->free_tmp_table(thd);
    table= 0; // Safety
  }

  for (SELECT_LEX *sl= first_select(); sl; sl= sl->next_select())
    error|= sl->cleanup();

  if (fake_select_lex)
  {
    JOIN *join;
    if ((join= fake_select_lex->join))
    {
      join->tables_list= 0;
      join->tables= 0;
    }
    error|= fake_select_lex->cleanup();
    if (fake_select_lex->order_list.elements)
    {
      order_st *ord;
      for (ord= (order_st*)fake_select_lex->order_list.first; ord; ord= ord->next)
        (*ord->item)->cleanup();
    }
  }

  return(error);
}


void st_select_lex_unit::reinit_exec_mechanism()
{
  prepared= optimized= executed= 0;
}


/*
  change select_result object of unit

  SYNOPSIS
    st_select_lex_unit::change_result()
    result	new select_result object
    old_result	old select_result object

  RETURN
    false - OK
    true  - error
*/

bool st_select_lex_unit::change_result(select_result_interceptor *new_result,
                                       select_result_interceptor *old_result)
{
  bool res= false;
  for (SELECT_LEX *sl= first_select(); sl; sl= sl->next_select())
  {
    if (sl->join && sl->join->result == old_result)
      if (sl->join->change_result(new_result))
	return true;
  }
  if (fake_select_lex && fake_select_lex->join)
    res= fake_select_lex->join->change_result(new_result);
  return (res);
}

/*
  Get column type information for this unit.

  SYNOPSIS
    st_select_lex_unit::get_unit_column_types()

  DESCRIPTION
    For a single-select the column types are taken
    from the list of selected items. For a union this function
    assumes that st_select_lex_unit::prepare has been called
    and returns the type holders that were created for unioned
    column types of all selects.

  NOTES
    The implementation of this function should be in sync with
    st_select_lex_unit::prepare()
*/

List<Item> *st_select_lex_unit::get_unit_column_types()
{
  SELECT_LEX *sl= first_select();

  if (is_union())
  {
    assert(prepared);
    /* Types are generated during prepare */
    return &types;
  }

  return &sl->item_list;
}

bool st_select_lex::cleanup()
{
  bool error= false;

  if (join)
  {
    assert((st_select_lex*)join->select_lex == this);
    error= join->destroy();
    delete join;
    join= 0;
  }
  for (SELECT_LEX_UNIT *lex_unit= first_inner_unit(); lex_unit ;
       lex_unit= lex_unit->next_unit())
  {
    error= (bool) ((uint) error | (uint) lex_unit->cleanup());
  }
  non_agg_fields.empty();
  inner_refs_list.empty();
  return(error);
}


void st_select_lex::cleanup_all_joins(bool full)
{
  SELECT_LEX_UNIT *unit;
  SELECT_LEX *sl;

  if (join)
    join->cleanup(full);

  for (unit= first_inner_unit(); unit; unit= unit->next_unit())
    for (sl= unit->first_select(); sl; sl= sl->next_select())
      sl->cleanup_all_joins(full);
}
