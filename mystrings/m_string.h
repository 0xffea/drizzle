/* Copyright (C) 2000 MySQL AB

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

/* There may be prolems include all of theese. Try to test in
   configure with ones are needed? */

/*  This is needed for the definitions of strchr... on solaris */


#ifndef _m_string_h
#define _m_string_h

#include <drizzled/global.h>

#ifndef __USE_GNU
#define __USE_GNU				/* We want to use stpcpy */
#endif
#if defined(HAVE_STRINGS_H)
#include <strings.h>
#endif
#if defined(HAVE_STRING_H)
#include <string.h>
#endif

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <limits.h>

/*  This is needed for the definitions of memcpy... on solaris */
#if defined(HAVE_MEMORY_H) && !defined(__cplusplus)
#include <memory.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/*
  my_str_malloc() and my_str_free() are assigned to implementations in
  strings/alloc.c, but can be overridden in the calling program.
 */
extern void *(*my_str_malloc)(size_t);
extern void (*my_str_free)(void *);

#define strmov_overlapp(A,B) stpcpy(A,B)
#define strmake_overlapp(A,B,C) strmake(A,B,C)

extern void bmove_upp(unsigned char *dst,const unsigned char *src,size_t len);

extern	void bchange(unsigned char *dst,size_t old_len,const unsigned char *src,
		     size_t new_len,size_t tot_len);
extern	char *strend(const char *s);
extern	char *strfield(char *src,int fields,int chars,int blanks,
			   int tabch);
extern	char *strfill(char * s,size_t len,char fill);
extern	char *strkey(char *dst,char *head,char *tail,char *flags);
extern	char *strmake(char *dst,const char *src,size_t length);
#ifndef strmake_overlapp
extern	char *strmake_overlapp(char *dst,const char *src, size_t length);
#endif

extern	char *strsuff(const char *src,const char *suffix);
extern	char *strxcat(char *dst,const char *src, ...);
extern	char *strxmov(char *dst,const char *src, ...);
extern	char *strxcpy(char *dst,const char *src, ...);
extern	char *strxncat(char *dst,size_t len, const char *src, ...);
extern	char *strxnmov(char *dst,size_t len, const char *src, ...);
extern	char *strxncpy(char *dst,size_t len, const char *src, ...);

/* Prototypes of normal stringfunctions (with may ours) */

#ifdef WANT_STRING_PROTOTYPES
extern char *strcat(char *, const char *);
extern char *strchr(const char *, char);
extern char *strrchr(const char *, char);
extern char *strcpy(char *, const char *);
extern int strcmp(const char *, const char *);
#ifndef __GNUC__
extern size_t strlen(const char *);
#endif
#endif

#if !defined(__cplusplus)
#ifndef HAVE_STRPBRK
extern char *strpbrk(const char *, const char *);
#endif
#ifndef HAVE_STRSTR
extern char *strstr(const char *, const char *);
#endif
#endif
extern int is_prefix(const char *, const char *);

/* Conversion routines */
typedef enum {
  MY_GCVT_ARG_FLOAT,
  MY_GCVT_ARG_DOUBLE
} my_gcvt_arg_type;

double my_strtod(const char *str, char **end, int *error);
double my_atof(const char *nptr);
size_t my_fcvt(double x, int precision, char *to, bool *error);
size_t my_gcvt(double x, my_gcvt_arg_type type, int width, char *to,
               bool *error);

#define NOT_FIXED_DEC 31

/*
  The longest string my_fcvt can return is 311 + "precision" bytes.
  Here we assume that we never cal my_fcvt() with precision >= NOT_FIXED_DEC
  (+ 1 byte for the terminating '\0').
*/
#define FLOATING_POINT_BUFFER (311 + NOT_FIXED_DEC)

/*
  We want to use the 'e' format in some cases even if we have enough space
  for the 'f' one just to mimic sprintf("%.15g") behavior for large integers,
  and to improve it for numbers < 10^(-4).
  That is, for |x| < 1 we require |x| >= 10^(-15), and for |x| > 1 we require
  it to be integer and be <= 10^DBL_DIG for the 'f' format to be used.
  We don't lose precision, but make cases like "1e200" or "0.00001" look nicer.
*/
#define MAX_DECPT_FOR_F_FORMAT DBL_DIG

/*
  The maximum possible field width for my_gcvt() conversion.
  (DBL_DIG + 2) significant digits + sign + "." + ("e-NNN" or
  MAX_DECPT_FOR_F_FORMAT zeros for cases when |x|<1 and the 'f' format is used).
*/
#define MY_GCVT_MAX_FIELD_WIDTH (DBL_DIG + 4 + max(5, MAX_DECPT_FOR_F_FORMAT))
  

extern char *llstr(int64_t value,char *buff);
extern char *ullstr(int64_t value,char *buff);

extern char *int2str(long val, char *dst, int radix, int upcase);
extern char *int10_to_str(long val,char *dst,int radix);
extern char *str2int(const char *src,int radix,long lower,long upper,
			 long *val);
int64_t my_strtoll10(const char *nptr, char **endptr, int *error);
#if SIZEOF_LONG == SIZEOF_LONG_LONG
#define int64_t2str(A,B,C) int2str((A),(B),(C),1)
#define int64_t10_to_str(A,B,C) int10_to_str((A),(B),(C))
#undef strtoll
#define strtoll(A,B,C) strtol((A),(B),(C))
#define strtoull(A,B,C) strtoul((A),(B),(C))
#ifndef HAVE_STRTOULL
#define HAVE_STRTOULL
#endif
#ifndef HAVE_STRTOLL
#define HAVE_STRTOLL
#endif
#else
extern char *int64_t2str(int64_t val,char *dst,int radix);
extern char *int64_t10_to_str(int64_t val,char *dst,int radix);
#if (!defined(HAVE_STRTOULL) || defined(NO_STRTOLL_PROTO))
extern int64_t strtoll(const char *str, char **ptr, int base);
extern uint64_t strtoull(const char *str, char **ptr, int base);
#endif
#endif


#if defined(__cplusplus)
}
#endif

/*
  LEX_STRING -- a pair of a C-string and its length.
*/

#ifndef _my_plugin_h
/* This definition must match the one given in mysql/plugin.h */
struct st_mysql_lex_string
{
  char *str;
  size_t length;
};
#endif
typedef struct st_mysql_lex_string LEX_STRING;

#define STRING_WITH_LEN(X) (X), ((size_t) (sizeof(X) - 1))
#define USTRING_WITH_LEN(X) ((unsigned char*) X), ((size_t) (sizeof(X) - 1))
#define C_STRING_WITH_LEN(X) ((char *) (X)), ((size_t) (sizeof(X) - 1))

/* SPACE_INT is a word that contains only spaces */
#if SIZEOF_INT == 4
#define SPACE_INT 0x20202020
#elif SIZEOF_INT == 8
#define SPACE_INT 0x2020202020202020
#else
#error define the appropriate constant for a word full of spaces
#endif

/**
  Skip trailing space.

  On most systems reading memory in larger chunks (ideally equal to the size of
  the chinks that the machine physically reads from memory) causes fewer memory
  access loops and hence increased performance.
  This is why the 'int' type is used : it's closest to that (according to how
  it's defined in C).
  So when we determine the amount of whitespace at the end of a string we do
  the following :
    1. We divide the string into 3 zones :
      a) from the start of the string (__start) to the first multiple
        of sizeof(int)  (__start_words)
      b) from the end of the string (__end) to the last multiple of sizeof(int)
        (__end_words)
      c) a zone that is aligned to sizeof(int) and can be safely accessed
        through an int *
    2. We start comparing backwards from (c) char-by-char. If all we find is
       space then we continue
    3. If there are elements in zone (b) we compare them as unsigned ints to a
       int mask (SPACE_INT) consisting of all spaces
    4. Finally we compare the remaining part (a) of the string char by char.
       This covers for the last non-space unsigned int from 3. (if any)

   This algorithm works well for relatively larger strings, but it will slow
   the things down for smaller strings (because of the additional calculations
   and checks compared to the naive method). Thus the barrier of length 20
   is added.

   @param     ptr   pointer to the input string
   @param     len   the length of the string
   @return          the last non-space character
*/

static inline const unsigned char *skip_trailing_space(const unsigned char *ptr,size_t len)
{
  const unsigned char *end= ptr + len;

  if (len > 20)
  {
    const unsigned char *end_words= (const unsigned char *)(intptr_t)
      (((uint64_t)(intptr_t)end) / SIZEOF_INT * SIZEOF_INT);
    const unsigned char *start_words= (const unsigned char *)(intptr_t)
       ((((uint64_t)(intptr_t)ptr) + SIZEOF_INT - 1) / SIZEOF_INT * SIZEOF_INT);

    assert(((uint64_t)(intptr_t)ptr) >= SIZEOF_INT);
    if (end_words > ptr)
    {
      while (end > end_words && end[-1] == 0x20)
        end--;
      if (end[-1] == 0x20 && start_words < end_words)
        while (end > start_words && ((const unsigned *)end)[-1] == SPACE_INT)
          end -= SIZEOF_INT;
    }
  }
  while (end > ptr && end[-1] == 0x20)
    end--;
  return (end);
}

#endif
