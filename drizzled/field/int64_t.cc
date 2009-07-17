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


#include <drizzled/server_includes.h>
#include <drizzled/field/int64_t.h>
#include <drizzled/error.h>
#include <drizzled/table.h>
#include <drizzled/session.h>

#include <algorithm>

using namespace std;


/****************************************************************************
 Field type int64_t int (8 bytes)
****************************************************************************/

int Field_int64_t::store(const char *from,uint32_t len, const CHARSET_INFO * const cs)
{
  int error= 0;
  char *end;
  uint64_t tmp;

  ASSERT_COLUMN_MARKED_FOR_WRITE;

  tmp= cs->cset->strntoull10rnd(cs, from, len, false, &end,&error);
  if (error == MY_ERRNO_ERANGE)
  {
    set_warning(DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);
    error= 1;
  }
  else if (table->in_use->count_cuted_fields &&
           check_int(cs, from, len, end, error))
    error= 1;
  else
    error= 0;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int8store(ptr,tmp);
  }
  else
#endif
    int64_tstore(ptr,tmp);
  return error;
}


int Field_int64_t::store(double nr)
{
  int error= 0;
  int64_t res;

  ASSERT_COLUMN_MARKED_FOR_WRITE;

  nr= rint(nr);

  if (nr <= (double) INT64_MIN)
  {
    res= INT64_MIN;
    error= (nr < (double) INT64_MIN);
  }
  else if (nr >= (double) (uint64_t) INT64_MAX)
  {
    res= INT64_MAX;
    error= (nr > (double) INT64_MAX);
  }
  else
    res=(int64_t) nr;

  if (error)
    set_warning(DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int8store(ptr,res);
  }
  else
#endif
    int64_tstore(ptr,res);
  return error;
}


int Field_int64_t::store(int64_t nr, bool )
{
  int error= 0;

  ASSERT_COLUMN_MARKED_FOR_WRITE;

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int8store(ptr,nr);
  }
  else
#endif
    int64_tstore(ptr,nr);
  return error;
}


double Field_int64_t::val_real(void)
{
  int64_t j;

  ASSERT_COLUMN_MARKED_FOR_READ;

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    j=sint8korr(ptr);
  }
  else
#endif
    int64_tget(j,ptr);
  /* The following is open coded to avoid a bug in gcc 3.3 */
  return (double) j;
}


int64_t Field_int64_t::val_int(void)
{
  int64_t j;

  ASSERT_COLUMN_MARKED_FOR_READ;

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    j=sint8korr(ptr);
  else
#endif
    int64_tget(j,ptr);
  return j;
}


String *Field_int64_t::val_str(String *val_buffer,
				String *)
{
  const CHARSET_INFO * const cs= &my_charset_bin;
  uint32_t length;
  uint32_t mlength= max(field_length+1,22*cs->mbmaxlen);
  val_buffer->alloc(mlength);
  char *to=(char*) val_buffer->ptr();
  int64_t j;

  ASSERT_COLUMN_MARKED_FOR_READ;

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    j=sint8korr(ptr);
  else
#endif
    int64_tget(j,ptr);

  length=(uint32_t) (cs->cset->int64_t10_to_str)(cs,to,mlength, -10, j);
  val_buffer->length(length);

  return val_buffer;
}

int Field_int64_t::cmp(const unsigned char *a_ptr, const unsigned char *b_ptr)
{
  int64_t a,b;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    a=sint8korr(a_ptr);
    b=sint8korr(b_ptr);
  }
  else
#endif
  {
    int64_tget(a,a_ptr);
    int64_tget(b,b_ptr);
  }
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}

void Field_int64_t::sort_string(unsigned char *to,uint32_t )
{
#ifdef WORDS_BIGENDIAN
  if (!table->s->db_low_byte_first)
  {
    to[0] = (char) (ptr[0] ^ 128);		/* Revers signbit */
    to[1]   = ptr[1];
    to[2]   = ptr[2];
    to[3]   = ptr[3];
    to[4]   = ptr[4];
    to[5]   = ptr[5];
    to[6]   = ptr[6];
    to[7]   = ptr[7];
  }
  else
#endif
  {
    to[0] = (char) (ptr[7] ^ 128);		/* Revers signbit */
    to[1]   = ptr[6];
    to[2]   = ptr[5];
    to[3]   = ptr[4];
    to[4]   = ptr[3];
    to[5]   = ptr[2];
    to[6]   = ptr[1];
    to[7]   = ptr[0];
  }
}


void Field_int64_t::sql_type(String &res) const
{
  const CHARSET_INFO * const cs=res.charset();
  res.length(cs->cset->snprintf(cs,(char*) res.ptr(),res.alloced_length(), "bigint"));
}


unsigned char *Field_int64_t::pack(unsigned char* to, const unsigned char *from,
                                         uint32_t,
#ifdef WORDS_BIGENDIAN
                                         bool low_byte_first
#else
                                         bool
#endif
)
{
  int64_t val;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
     val = sint8korr(from);
  else
#endif
    int64_tget(val, from);

#ifdef WORDS_BIGENDIAN
  if (low_byte_first)
    int8store(to, val);
  else
#endif
    int64_tstore(to, val);
  return to + sizeof(val);
}


const unsigned char *Field_int64_t::unpack(unsigned char* to, const unsigned char *from, uint32_t,
#ifdef WORDS_BIGENDIAN
                                           bool low_byte_first
#else
                                           bool
#endif
)
{
  int64_t val;
#ifdef WORDS_BIGENDIAN
  if (low_byte_first)
    val = sint8korr(from);
  else
#endif
    int64_tget(val, from);

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    int8store(to, val);
  else
#endif
    int64_tstore(to, val);
  return from + sizeof(val);
}

