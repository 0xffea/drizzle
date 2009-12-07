/* - mode: c++ c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 MySQL
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

#include "drizzled/server_includes.h"
#include "drizzled/field/date.h"
#include "drizzled/error.h"
#include "drizzled/table.h"
#include "drizzled/temporal.h"
#include "drizzled/session.h"
#include "drizzled/time_functions.h"

#include <sstream>
#include <string>


/****************************************************************************
** Drizzle date type stored in 3 bytes
** In number context: YYYYMMDD
****************************************************************************/

/*
  Store string into a date field

  SYNOPSIS
    Field_date::store()
    from                Date string
    len                 Length of date field
    cs                  Character set (not used)

  RETURN
    0  ok
    1  Value was cut during conversion
    2  Wrong date string
    3  Datetime value that was cut (warning level NOTE)
       This is used by opt_range.cc:get_mm_leaf(). Note that there is a
       nearly-identical class Field_date doesn't ever return 3 from its
       store function.
*/
int Field_date::store(const char *from,
                         uint32_t len,
                         const CHARSET_INFO * const )
{
#ifdef NOTDEFINED
  long tmp;
  DRIZZLE_TIME l_time;
  int error;
  Session *session= table ? table->in_use : current_session;
  enum enum_drizzle_timestamp_type ret;
  if ((ret= str_to_datetime(from, len, &l_time,
                            (TIME_FUZZY_DATE |
                             (session->variables.sql_mode &
                              (MODE_NO_ZERO_DATE | MODE_INVALID_DATES))),
                            &error)) <= DRIZZLE_TIMESTAMP_ERROR)
  {
    tmp= 0;
    error= 2;
  }
  else
  {
    tmp= l_time.day + l_time.month*32 + l_time.year*16*32;
    if (!error && (ret != DRIZZLE_TIMESTAMP_DATE) &&
        (l_time.hour || l_time.minute || l_time.second || l_time.second_part))
      error= 3;                                 // Datetime was cut (note)
  }

  if (error)
    set_datetime_warning(error == 3 ? DRIZZLE_ERROR::WARN_LEVEL_NOTE :
                         DRIZZLE_ERROR::WARN_LEVEL_WARN,
                         ER_WARN_DATA_TRUNCATED,
                         from, len, DRIZZLE_TIMESTAMP_DATE, 1);

  int3store(ptr, tmp);
#endif /* NOTDEFINED */
  /* 
   * Try to create a DateTime from the supplied string.  Throw an error
   * if unable to create a valid DateTime.  A DateTime is used so that
   * automatic conversion from the higher-storage DateTime can be used
   * and matches on datetime format strings can occur.
   */
  ASSERT_COLUMN_MARKED_FOR_WRITE;
  drizzled::DateTime temporal;
  if (! temporal.from_string(from, (size_t) len))
  {
    my_error(ER_INVALID_DATETIME_VALUE, MYF(ME_FATALERROR), from);
    return 2;
  }
  /* Create the stored integer format. @TODO This should go away. Should be up to engine... */
  uint32_t int_value= (temporal.years() * 16 * 32) + (temporal.months() * 32) + temporal.days();
  int3store(ptr, int_value);
  return 0;
}

int Field_date::store(double from)
{
  ASSERT_COLUMN_MARKED_FOR_WRITE;
  if (from < 0.0 || from > 99991231235959.0)
  {
    /* Convert the double to a string using stringstream */
    std::stringstream ss;
    std::string tmp;
    ss.precision(18); /* 18 places should be fine for error display of double input. */
    ss << from; ss >> tmp;

    my_error(ER_INVALID_DATETIME_VALUE, MYF(ME_FATALERROR), tmp.c_str());
    return 2;
  }
  return Field_date::store((int64_t) rint(from), false);
}

int Field_date::store(int64_t from, bool)
{
  /* 
   * Try to create a DateTime from the supplied integer.  Throw an error
   * if unable to create a valid DateTime.  
   */
  ASSERT_COLUMN_MARKED_FOR_WRITE;
  drizzled::DateTime temporal;
  if (! temporal.from_int64_t(from))
  {
    /* Convert the integer to a string using stringstream */
    std::stringstream ss;
    std::string tmp;
    ss << from; ss >> tmp;

    my_error(ER_INVALID_DATETIME_VALUE, MYF(ME_FATALERROR), tmp.c_str());
    return 2;
  }

  /* Create the stored integer format. @TODO This should go away. Should be up to engine... */
  uint32_t int_value= (temporal.years() * 16 * 32) + (temporal.months() * 32) + temporal.days();
  int3store(ptr, int_value);
  return 0;
}

int Field_date::store_time(DRIZZLE_TIME *ltime,
                              enum enum_drizzle_timestamp_type time_type)
{
  long tmp;
  int error= 0;
  if (time_type == DRIZZLE_TIMESTAMP_DATE ||
      time_type == DRIZZLE_TIMESTAMP_DATETIME)
  {
    tmp=ltime->year*16*32+ltime->month*32+ltime->day;
    if (check_date(ltime, tmp != 0,
                   (TIME_FUZZY_DATE |
                    (current_session->variables.sql_mode &
                     (MODE_NO_ZERO_DATE | MODE_INVALID_DATES))), &error))
    {
      char buff[MAX_DATE_STRING_REP_LENGTH];
      String str(buff, sizeof(buff), &my_charset_utf8_general_ci);
      make_date(ltime, &str);
      set_datetime_warning(DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_TRUNCATED,
                           str.ptr(), str.length(), DRIZZLE_TIMESTAMP_DATE, 1);
    }
    if (!error && ltime->time_type != DRIZZLE_TIMESTAMP_DATE &&
        (ltime->hour || ltime->minute || ltime->second || ltime->second_part))
    {
      char buff[MAX_DATE_STRING_REP_LENGTH];
      String str(buff, sizeof(buff), &my_charset_utf8_general_ci);
      make_datetime(ltime, &str);
      set_datetime_warning(DRIZZLE_ERROR::WARN_LEVEL_NOTE,
                           ER_WARN_DATA_TRUNCATED,
                           str.ptr(), str.length(), DRIZZLE_TIMESTAMP_DATE, 1);
      error= 3;
    }
  }
  else
  {
    tmp=0;
    error= 1;
    set_warning(DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_TRUNCATED, 1);
  }
  int3store(ptr,tmp);
  return error;
}

double Field_date::val_real(void)
{
  return (double) Field_date::val_int();
}

int64_t Field_date::val_int(void)
{
  uint32_t j;

  ASSERT_COLUMN_MARKED_FOR_READ;

  j= uint3korr(ptr);
  j= (j % 32L)+(j / 32L % 16L)*100L + (j/(16L*32L))*10000L;

  return (int64_t) j;
}

String *Field_date::val_str(String *val_buffer,
			       String *)
{
  val_buffer->alloc(field_length);
  val_buffer->length(field_length);
  uint32_t tmp=(uint32_t) uint3korr(ptr);
  int part;
  char *pos=(char*) val_buffer->ptr()+10;

  ASSERT_COLUMN_MARKED_FOR_READ;

  /* Open coded to get more speed */
  *pos--=0;					// End NULL
  part=(int) (tmp & 31);
  *pos--= (char) ('0'+part%10);
  *pos--= (char) ('0'+part/10);
  *pos--= '-';
  part=(int) (tmp >> 5 & 15);
  *pos--= (char) ('0'+part%10);
  *pos--= (char) ('0'+part/10);
  *pos--= '-';
  part=(int) (tmp >> 9);
  *pos--= (char) ('0'+part%10); part/=10;
  *pos--= (char) ('0'+part%10); part/=10;
  *pos--= (char) ('0'+part%10); part/=10;
  *pos=   (char) ('0'+part);
  return val_buffer;
}

bool Field_date::get_date(DRIZZLE_TIME *ltime,uint32_t fuzzydate)
{
  uint32_t tmp=(uint32_t) uint3korr(ptr);
  ltime->day=   tmp & 31;
  ltime->month= (tmp >> 5) & 15;
  ltime->year=  (tmp >> 9);
  ltime->time_type= DRIZZLE_TIMESTAMP_DATE;
  ltime->hour= ltime->minute= ltime->second= ltime->second_part= ltime->neg= 0;
  return ((!(fuzzydate & TIME_FUZZY_DATE) && (!ltime->month || !ltime->day)) ?
          1 : 0);
}

bool Field_date::get_time(DRIZZLE_TIME *ltime)
{
  return Field_date::get_date(ltime,0);
}

int Field_date::cmp(const unsigned char *a_ptr, const unsigned char *b_ptr)
{
  uint32_t a,b;
  a=(uint32_t) uint3korr(a_ptr);
  b=(uint32_t) uint3korr(b_ptr);
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}

void Field_date::sort_string(unsigned char *to,uint32_t )
{
  to[0] = ptr[2];
  to[1] = ptr[1];
  to[2] = ptr[0];
}

void Field_date::sql_type(String &res) const
{
  res.set_ascii(STRING_WITH_LEN("date"));
}
