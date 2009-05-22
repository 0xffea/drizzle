/* Copyright (C) 1995-2002 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA */

/**********************************************************************
This file contains the implementation of error and warnings related

  - Whenever an error or warning occurred, it pushes it to a warning list
    that the user can retrieve with SHOW WARNINGS or SHOW ERRORS.

  - For each statement, we return the number of warnings generated from this
    command.  Note that this can be different from @@warning_count as
    we reset the warning list only for questions that uses a table.
    This is done to allow on to do:
    INSERT ...;
    SELECT @@warning_count;
    SHOW WARNINGS;
    (If we would reset after each command, we could not retrieve the number
     of warnings)

  - When client requests the information using SHOW command, then
    server processes from this list and returns back in the form of
    resultset.

    Supported syntaxes:

    SHOW [COUNT(*)] ERRORS [LIMIT [offset,] rows]
    SHOW [COUNT(*)] WARNINGS [LIMIT [offset,] rows]
    SELECT @@warning_count, @@error_count;

***********************************************************************/

#include <drizzled/server_includes.h>
#include <drizzled/session.h>
#include <drizzled/sql_base.h>
#include <drizzled/item/empty_string.h>
#include <drizzled/item/return_int.h>

/*
  Store a new message in an error object

  This is used to in group_concat() to register how many warnings we actually
  got after the query has been executed.
*/
void DRIZZLE_ERROR::set_msg(Session *session, const char *msg_arg)
{
  msg= strdup_root(&session->warn_root, msg_arg);
}

/*
  Reset all warnings for the thread

  SYNOPSIS
    drizzle_reset_errors()
    session			Thread handle
    force               Reset warnings even if it has been done before

  IMPLEMENTATION
    Don't reset warnings if this has already been called for this query.
    This may happen if one gets a warning during the parsing stage,
    in which case push_warnings() has already called this function.
*/

void drizzle_reset_errors(Session *session, bool force)
{
  if (session->query_id != session->warn_id || force)
  {
    session->warn_id= session->query_id;
    free_root(&session->warn_root,MYF(0));
    memset(session->warn_count, 0, sizeof(session->warn_count));
    if (force)
      session->total_warn_count= 0;
    session->warn_list.empty();
    session->row_count= 1; // by default point to row 1
  }
  return;
}


/*
  Push the warning/error to error list if there is still room in the list

  SYNOPSIS
    push_warning()
    session			Thread handle
    level		Severity of warning (note, warning, error ...)
    code		Error number
    msg			Clear error message

  RETURN
    pointer on DRIZZLE_ERROR object
*/

DRIZZLE_ERROR *push_warning(Session *session, DRIZZLE_ERROR::enum_warning_level level,
                          uint32_t code, const char *msg)
{
  DRIZZLE_ERROR *err= 0;

  if (level == DRIZZLE_ERROR::WARN_LEVEL_NOTE &&
      !(session->options & OPTION_SQL_NOTES))
    return(0);

  if (session->query_id != session->warn_id)
    drizzle_reset_errors(session, 0);
  session->got_warning= 1;

  /* Abort if we are using strict mode and we are not using IGNORE */
  if ((int) level >= (int) DRIZZLE_ERROR::WARN_LEVEL_WARN &&
      session->really_abort_on_warning())
  {
    /* Avoid my_message() calling push_warning */
    bool no_warnings_for_error= session->no_warnings_for_error;

    session->no_warnings_for_error= 1;

    session->killed= Session::KILL_BAD_DATA;
    my_message(code, msg, MYF(0));

    session->no_warnings_for_error= no_warnings_for_error;
    /* Store error in error list (as my_message() didn't do it) */
    level= DRIZZLE_ERROR::WARN_LEVEL_ERROR;
  }

  if (session->handle_error(code, msg, level))
    return NULL;

  if (session->warn_list.elements < session->variables.max_error_count)
  {
    /* We have to use warn_root, as mem_root is freed after each query */
    if ((err= new (&session->warn_root) DRIZZLE_ERROR(session, code, level, msg)))
      session->warn_list.push_back(err, &session->warn_root);
  }
  session->warn_count[(uint32_t) level]++;
  session->total_warn_count++;
  return(err);
}

/*
  Push the warning/error to error list if there is still room in the list

  SYNOPSIS
    push_warning_printf()
    session			Thread handle
    level		Severity of warning (note, warning, error ...)
    code		Error number
    msg			Clear error message
*/

void push_warning_printf(Session *session, DRIZZLE_ERROR::enum_warning_level level,
			 uint32_t code, const char *format, ...)
{
  va_list args;
  char    warning[ERRMSGSIZE+20];

  va_start(args,format);
  vsnprintf(warning, sizeof(warning), format, args);
  va_end(args);
  push_warning(session, level, code, warning);
  return;
}


/*
  Send all notes, errors or warnings to the client in a result set

  SYNOPSIS
    mysqld_show_warnings()
    session			Thread handler
    levels_to_show	Bitmap for which levels to show

  DESCRIPTION
    Takes into account the current LIMIT

  RETURN VALUES
    false ok
    true  Error sending data to client
*/

const LEX_STRING warning_level_names[]=
{
  { C_STRING_WITH_LEN("Note") },
  { C_STRING_WITH_LEN("Warning") },
  { C_STRING_WITH_LEN("Error") },
  { C_STRING_WITH_LEN("?") }
};

bool mysqld_show_warnings(Session *session, uint32_t levels_to_show)
{
  List<Item> field_list;

  field_list.push_back(new Item_empty_string("Level", 7));
  field_list.push_back(new Item_return_int("Code",4, DRIZZLE_TYPE_LONG));
  field_list.push_back(new Item_empty_string("Message",DRIZZLE_ERRMSG_SIZE));

  if (session->protocol->sendFields(&field_list,
                                  Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF))
    return(true);

  DRIZZLE_ERROR *err;
  Select_Lex *sel= &session->lex->select_lex;
  Select_Lex_Unit *unit= &session->lex->unit;
  ha_rows idx= 0;
  Protocol *protocol=session->protocol;

  unit->set_limit(sel);

  List_iterator_fast<DRIZZLE_ERROR> it(session->warn_list);
  while ((err= it++))
  {
    /* Skip levels that the user is not interested in */
    if (!(levels_to_show & ((ulong) 1 << err->level)))
      continue;
    if (++idx <= unit->offset_limit_cnt)
      continue;
    if (idx > unit->select_limit_cnt)
      break;
    protocol->prepareForResend();
    protocol->store(warning_level_names[err->level].str,
		    warning_level_names[err->level].length, system_charset_info);
    protocol->store((uint32_t) err->code);
    protocol->store(err->msg, strlen(err->msg), system_charset_info);
    if (protocol->write())
      return(true);
  }
  session->my_eof();
  return(false);
}
