/* Copyright (C) 2005-2006 MySQL AB

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

#include "config.h"
#include <time.h>
#include "drizzled/current_session.h"
#include "drizzled/error.h"
#include "drizzled/field.h"
#include "drizzled/internal/my_sys.h"

namespace drizzled
{

/**
  report result of decimal operation.

  @param result  decimal library return code (E_DEC_* see include/decimal.h)

  @todo
    Fix error messages

  @return
    result
*/

int decimal_operation_results(int result)
{
  switch (result) {
  case E_DEC_OK:
    break;
  case E_DEC_TRUNCATED:
    push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_WARN,
			ER_WARN_DATA_TRUNCATED, ER(ER_WARN_DATA_TRUNCATED),
			"", (long)-1);
    break;
  case E_DEC_OVERFLOW:
    push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_ERROR,
                        ER_TRUNCATED_WRONG_VALUE,
                        ER(ER_TRUNCATED_WRONG_VALUE),
			"DECIMAL", "");
    break;
  case E_DEC_DIV_ZERO:
    push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_ERROR,
			ER_DIVISION_BY_ZERO, ER(ER_DIVISION_BY_ZERO));
    break;
  case E_DEC_BAD_NUM:
    push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_ERROR,
			ER_TRUNCATED_WRONG_VALUE_FOR_FIELD,
			ER(ER_TRUNCATED_WRONG_VALUE_FOR_FIELD),
			"decimal", "", "", (long)-1);
    break;
  case E_DEC_OOM:
    my_error(ER_OUT_OF_RESOURCES, MYF(0));
    break;
  default:
    assert(0);
  }
  return result;
}


/**
  @brief Converting decimal to string

  @details Convert given my_decimal to String; allocate buffer as needed.

  @param[in]   mask        what problems to warn on (mask of E_DEC_* values)
  @param[in]   d           the decimal to print
  @param[in]   fixed_prec  overall number of digits if ZEROFILL, 0 otherwise
  @param[in]   fixed_dec   number of decimal places (if fixed_prec != 0)
  @param[in]   filler      what char to pad with (ZEROFILL et al.)
  @param[out]  *str        where to store the resulting string

  @return error coce
    @retval E_DEC_OK
    @retval E_DEC_TRUNCATED
    @retval E_DEC_OVERFLOW
    @retval E_DEC_OOM
*/

int my_decimal2string(uint32_t mask, const my_decimal *d,
                      uint32_t fixed_prec, uint32_t fixed_dec,
                      char filler, String *str)
{
  /*
    Calculate the size of the string: For DECIMAL(a,b), fixed_prec==a
    holds true iff the type is also ZEROFILL, which in turn implies
    UNSIGNED. Hence the buffer for a ZEROFILLed value is the length
    the user requested, plus one for a possible decimal point, plus
    one if the user only wanted decimal places, but we force a leading
    zero on them. Because the type is implicitly UNSIGNED, we do not
    need to reserve a character for the sign. For all other cases,
    fixed_prec will be 0, and my_decimal_string_length() will be called
    instead to calculate the required size of the buffer.
  */
  int length= (fixed_prec
               ? (fixed_prec + ((fixed_prec == fixed_dec) ? 1 : 0) + 1)
               : my_decimal_string_length(d));
  int result;
  if (str->alloc(length))
    return check_result(mask, E_DEC_OOM);
  result= decimal2string((decimal_t*) d, (char*) str->ptr(),
                         &length, (int)fixed_prec, fixed_dec,
                         filler);
  str->length(length);
  return check_result(mask, result);
}


/*
  Convert from decimal to binary representation

  SYNOPSIS
    my_decimal2binary()
    mask        error processing mask
    d           number for conversion
    bin         pointer to buffer where to write result
    prec        overall number of decimal digits
    scale       number of decimal digits after decimal point

  NOTE
    Before conversion we round number if it need but produce truncation
    error in this case

  RETURN
    E_DEC_OK
    E_DEC_TRUNCATED
    E_DEC_OVERFLOW
*/

int my_decimal2binary(uint32_t mask, const my_decimal *d, unsigned char *bin, int prec,
		      int scale)
{
  int err1= E_DEC_OK, err2;
  my_decimal rounded;
  my_decimal2decimal(d, &rounded);
  rounded.frac= decimal_actual_fraction(&rounded);
  if (scale < rounded.frac)
  {
    err1= E_DEC_TRUNCATED;
    /* decimal_round can return only E_DEC_TRUNCATED */
    decimal_round(&rounded, &rounded, scale, HALF_UP);
  }
  err2= decimal2bin(&rounded, bin, prec, scale);
  if (!err2)
    err2= err1;
  return check_result(mask, err2);
}


/*
  Convert string for decimal when string can be in some multibyte charset

  SYNOPSIS
    str2my_decimal()
    mask            error processing mask
    from            string to process
    length          length of given string
    charset         charset of given string
    decimal_value   buffer for result storing

  RESULT
    E_DEC_OK
    E_DEC_TRUNCATED
    E_DEC_OVERFLOW
    E_DEC_BAD_NUM
    E_DEC_OOM
*/

int str2my_decimal(uint32_t mask, const char *from, uint32_t length,
                   const CHARSET_INFO * charset, my_decimal *decimal_value)
{
  char *end, *from_end;
  int err;
  char buff[STRING_BUFFER_USUAL_SIZE];
  String tmp(buff, sizeof(buff), &my_charset_bin);
  if (charset->mbminlen > 1)
  {
    uint32_t dummy_errors;
    tmp.copy(from, length, charset, &my_charset_utf8_general_ci, &dummy_errors);
    from= tmp.ptr();
    length=  tmp.length();
    charset= &my_charset_bin;
  }
  from_end= end= (char*) from+length;
  err= string2decimal((char *)from, (decimal_t*) decimal_value, &end);
  if (end != from_end && !err)
  {
    /* Give warning if there is something other than end space */
    for ( ; end < from_end; end++)
    {
      if (!my_isspace(&my_charset_utf8_general_ci, *end))
      {
        err= E_DEC_TRUNCATED;
        break;
      }
    }
  }
  check_result_and_overflow(mask, err, decimal_value);
  return err;
}


my_decimal *date2my_decimal(DRIZZLE_TIME *ltime, my_decimal *dec)
{
  int64_t date;
  date = (ltime->year*100L + ltime->month)*100L + ltime->day;
  if (ltime->time_type > DRIZZLE_TIMESTAMP_DATE)
    date= ((date*100L + ltime->hour)*100L+ ltime->minute)*100L + ltime->second;
  if (int2my_decimal(E_DEC_FATAL_ERROR, date, false, dec))
    return dec;
  if (ltime->second_part)
  {
    dec->buf[(dec->intg-1) / 9 + 1]= ltime->second_part * 1000;
    dec->frac= 6;
  }
  return dec;
}


void my_decimal_trim(uint32_t *precision, uint32_t *scale)
{
  if (!(*precision) && !(*scale))
  {
    *precision= 10;
    *scale= 0;
    return;
  }
}

} /* namespace drizzled */
