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
#define __USE_GNU				/* We want to use my_stpcpy */
#endif
#if defined(HAVE_STRINGS_H)
#include <strings.h>
#endif
#if defined(HAVE_STRING_H)
#include <string.h>
#endif

#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <limits.h>
#include <ctype.h>

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

char *my_stpncpy(register char *dst, register const char *src, size_t n);
char *my_stpcpy(register char *dst, register const char *src);

#define strmov_overlapp(A,B) my_stpcpy(A,B)
#define strmake_overlapp(A,B,C) strmake(A,B,C)

extern void bmove_upp(unsigned char *dst,const unsigned char *src,size_t len);

extern	void bchange(unsigned char *dst,size_t old_len,const unsigned char *src,
		     size_t new_len,size_t tot_len);
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
extern	char *strxncpy(char *dst,size_t len, const char *src, ...);

/* Prototypes of normal stringfunctions (with may ours) */

#ifdef WANT_STRING_PROTOTYPES
extern char *strcat(char *, const char *);
extern char *strchr(const char *, char);
extern char *strrchr(const char *, char);
extern char *strcpy(char *, const char *);
#endif

#if !defined(__cplusplus)
#ifndef HAVE_STRPBRK
extern char *strpbrk(const char *, const char *);
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
#define MY_GCVT_MAX_FIELD_WIDTH (DBL_DIG + 4 + cmax(5, MAX_DECPT_FOR_F_FORMAT))
  

extern char *llstr(int64_t value,char *buff);
extern char *ullstr(int64_t value,char *buff);

extern char *int2str(int32_t val, char *dst, int radix, int upcase);
extern char *int10_to_str(int32_t val,char *dst,int radix);
extern char *str2int(const char *src,int radix,long lower,long upper,
                     long *val);
int64_t my_strtoll10(const char *nptr, char **endptr, int *error);
extern char *int64_t2str(int64_t val,char *dst,int radix);
extern char *int64_t10_to_str(int64_t val,char *dst,int radix);


#if defined(__cplusplus)
}
#endif


/**
  Skip trailing space.

   @param     ptr   pointer to the input string
   @param     len   the length of the string
   @return          the last non-space character
*/

static inline const unsigned char *
skip_trailing_space(const unsigned char *ptr, size_t len)
{
  const unsigned char *end= ptr + len;

  while (end > ptr && isspace(*--end))
    continue;
  return end+1;
}

#endif
