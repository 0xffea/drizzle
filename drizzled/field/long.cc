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
#include <drizzled/field/long.h>
#include <drizzled/error.h>
#include <drizzled/table.h>
#include <drizzled/session.h>
#include CMATH_H

#if defined(CMATH_NAMESPACE)
using namespace CMATH_NAMESPACE;
#endif

/****************************************************************************
** long int
****************************************************************************/

int Field_long::store(const char *from,uint32_t len, const CHARSET_INFO * const cs)
{
  long store_tmp;
  int error;
  int64_t rnd;
  
  error= get_int(cs, from, len, &rnd, UINT32_MAX, INT32_MIN, INT32_MAX);
  store_tmp= (long) rnd;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int4store(ptr, store_tmp);
  }
  else
#endif
    longstore(ptr, store_tmp);
  return error;
}


int Field_long::store(double nr)
{
  int error= 0;
  int32_t res;
  nr=rint(nr);

  if (nr < (double) INT32_MIN)
  {
    res=(int32_t) INT32_MIN;
    error= 1;
  }
  else if (nr > (double) INT32_MAX)
  {
    res=(int32_t) INT32_MAX;
    error= 1;
  }
  else
    res=(int32_t) (int64_t) nr;

  if (error)
    set_warning(DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int4store(ptr,res);
  }
  else
#endif
    longstore(ptr,res);
  return error;
}


int Field_long::store(int64_t nr, bool unsigned_val)
{
  int error= 0;
  int32_t res;

  if (nr < 0 && unsigned_val)
    nr= ((int64_t) INT32_MAX) + 1;           // Generate overflow
  if (nr < (int64_t) INT32_MIN) 
  {
    res=(int32_t) INT32_MIN;
    error= 1;
  }
  else if (nr > (int64_t) INT32_MAX)
  {
    res=(int32_t) INT32_MAX;
    error= 1;
  }
  else
    res=(int32_t) nr;

  if (error)
    set_warning(DRIZZLE_ERROR::WARN_LEVEL_WARN, ER_WARN_DATA_OUT_OF_RANGE, 1);

#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    int4store(ptr,res);
  }
  else
#endif
    longstore(ptr,res);
  return error;
}


double Field_long::val_real(void)
{
  int32_t j;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    j=sint4korr(ptr);
  else
#endif
    longget(j,ptr);
  return (double) j;
}

int64_t Field_long::val_int(void)
{
  int32_t j;
  /* See the comment in Field_long::store(int64_t) */
  assert(table->in_use == current_session);
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    j=sint4korr(ptr);
  else
#endif
    longget(j,ptr);
  return (int64_t) j;
}

String *Field_long::val_str(String *val_buffer,
			    String *val_ptr __attribute__((unused)))
{
  const CHARSET_INFO * const cs= &my_charset_bin;
  uint32_t length;
  uint32_t mlength=cmax(field_length+1,12*cs->mbmaxlen);
  val_buffer->alloc(mlength);
  char *to=(char*) val_buffer->ptr();
  int32_t j;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
    j=sint4korr(ptr);
  else
#endif
    longget(j,ptr);

  length=cs->cset->long10_to_str(cs,to,mlength,-10,(long) j);
  val_buffer->length(length);

  return val_buffer;
}


bool Field_long::send_binary(Protocol *protocol)
{
  return protocol->store_long(Field_long::val_int());
}

int Field_long::cmp(const unsigned char *a_ptr, const unsigned char *b_ptr)
{
  int32_t a,b;
#ifdef WORDS_BIGENDIAN
  if (table->s->db_low_byte_first)
  {
    a=sint4korr(a_ptr);
    b=sint4korr(b_ptr);
  }
  else
#endif
  {
    longget(a,a_ptr);
    longget(b,b_ptr);
  }

  return (a < b) ? -1 : (a > b) ? 1 : 0;
}

void Field_long::sort_string(unsigned char *to,uint32_t length __attribute__((unused)))
{
#ifdef WORDS_BIGENDIAN
  if (!table->s->db_low_byte_first)
  {
    to[0] = (char) (ptr[0] ^ 128);		/* Revers signbit */
    to[1]   = ptr[1];
    to[2]   = ptr[2];
    to[3]   = ptr[3];
  }
  else
#endif
  {
    to[0] = (char) (ptr[3] ^ 128);		/* Revers signbit */
    to[1]   = ptr[2];
    to[2]   = ptr[1];
    to[3]   = ptr[0];
  }
}


void Field_long::sql_type(String &res) const
{
  const CHARSET_INFO * const cs=res.charset();
  res.length(cs->cset->snprintf(cs,(char*) res.ptr(),res.alloced_length(), "int"));
}

unsigned char *Field_long::pack(unsigned char* to, const unsigned char *from,
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


const unsigned char *Field_long::unpack(unsigned char* to, const unsigned char *from, uint32_t,
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

