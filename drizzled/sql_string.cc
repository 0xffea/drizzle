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

/* This file is originally from the mysql distribution. Coded by monty */

#include "global.h"
#include <mysys/my_sys.h>
#include <mystrings/m_string.h>

/*
  The following extern declarations are ok as these are interface functions
  required by the string function
*/

extern uchar* sql_alloc(unsigned size);
extern void sql_element_free(void *ptr);

#include "sql_string.h"

/*****************************************************************************
** String functions
*****************************************************************************/

bool String::real_alloc(uint32_t arg_length)
{
  arg_length=ALIGN_SIZE(arg_length+1);
  str_length=0;
  if (Alloced_length < arg_length)
  {
    free();
    if (!(Ptr=(char*) my_malloc(arg_length,MYF(MY_WME))))
      return true;
    Alloced_length=arg_length;
    alloced=1;
  }
  Ptr[0]=0;
  return false;
}


/*
** Check that string is big enough. Set string[alloc_length] to 0
** (for C functions)
*/

bool String::realloc(uint32_t alloc_length)
{
  uint32_t len=ALIGN_SIZE(alloc_length+1);
  if (Alloced_length < len)
  {
    char *new_ptr;
    if (alloced)
    {
      if ((new_ptr= (char*) my_realloc(Ptr,len,MYF(MY_WME))))
      {
	Ptr=new_ptr;
	Alloced_length=len;
      }
      else
	return true;				// Signal error
    }
    else if ((new_ptr= (char*) my_malloc(len,MYF(MY_WME))))
    {
      if (str_length)				// Avoid bugs in memcpy on AIX
	memcpy(new_ptr,Ptr,str_length);
      new_ptr[str_length]=0;
      Ptr=new_ptr;
      Alloced_length=len;
      alloced=1;
    }
    else
      return true;			// Signal error
  }
  Ptr[alloc_length]=0;			// This make other funcs shorter
  return false;
}

bool String::set_int(int64_t num, bool unsigned_flag, const CHARSET_INFO * const cs)
{
  uint l=20*cs->mbmaxlen+1;
  int base= unsigned_flag ? 10 : -10;

  if (alloc(l))
    return true;
  str_length=(uint32_t) (cs->cset->int64_t10_to_str)(cs,Ptr,l,base,num);
  str_charset=cs;
  return false;
}

bool String::set_real(double num,uint decimals, const CHARSET_INFO * const cs)
{
  char buff[FLOATING_POINT_BUFFER];
  uint dummy_errors;
  size_t len;

  str_charset=cs;
  if (decimals >= NOT_FIXED_DEC)
  {
    len= my_gcvt(num, MY_GCVT_ARG_DOUBLE, sizeof(buff) - 1, buff, NULL);
    return copy(buff, len, &my_charset_latin1, cs, &dummy_errors);
  }
  len= my_fcvt(num, decimals, buff, NULL);
  return copy(buff, (uint32_t) len, &my_charset_latin1, cs,
              &dummy_errors);
}


bool String::copy()
{
  if (!alloced)
  {
    Alloced_length=0;				// Force realloc
    return realloc(str_length);
  }
  return false;
}

bool String::copy(const String &str)
{
  if (alloc(str.str_length))
    return true;
  str_length=str.str_length;
  memmove(Ptr, str.Ptr, str_length);		// May be overlapping
  Ptr[str_length]=0;
  str_charset=str.str_charset;
  return false;
}

bool String::copy(const char *str,uint32_t arg_length, const CHARSET_INFO * const cs)
{
  if (alloc(arg_length))
    return true;
  if ((str_length=arg_length))
    memcpy(Ptr,str,arg_length);
  Ptr[arg_length]=0;
  str_charset=cs;
  return false;
}


/*
  Checks that the source string can be just copied to the destination string
  without conversion.

  SYNPOSIS

  needs_conversion()
  arg_length		Length of string to copy.
  from_cs		Character set to copy from
  to_cs			Character set to copy to
  uint32_t *offset	Returns number of unaligned characters.

  RETURN
   0  No conversion needed
   1  Either character set conversion or adding leading  zeros
      (e.g. for UCS-2) must be done

  NOTE
  to_cs may be NULL for "no conversion" if the system variable
  character_set_results is NULL.
*/

bool String::needs_conversion(uint32_t arg_length,
			      const CHARSET_INFO * const from_cs,
			      const CHARSET_INFO * const to_cs,
			      uint32_t *offset)
{
  *offset= 0;
  if (!to_cs ||
      (to_cs == &my_charset_bin) || 
      (to_cs == from_cs) ||
      my_charset_same(from_cs, to_cs) ||
      ((from_cs == &my_charset_bin) &&
       (!(*offset=(arg_length % to_cs->mbminlen)))))
    return false;
  return true;
}


/*
  Copy a multi-byte character sets with adding leading zeros.

  SYNOPSIS

  copy_aligned()
  str			String to copy
  arg_length		Length of string. This should NOT be dividable with
			cs->mbminlen.
  offset		arg_length % cs->mb_minlength
  cs			Character set for 'str'

  NOTES
    For real multi-byte, ascii incompatible charactser sets,
    like UCS-2, add leading zeros if we have an incomplete character.
    Thus, 
      SELECT _ucs2 0xAA 
    will automatically be converted into
      SELECT _ucs2 0x00AA

  RETURN
    0  ok
    1  error
*/

bool String::copy_aligned(const char *str,uint32_t arg_length, uint32_t offset,
                          const CHARSET_INFO * const cs)
{
  /* How many bytes are in incomplete character */
  offset= cs->mbmaxlen - offset; /* How many zeros we should prepend */
  assert(offset && offset != cs->mbmaxlen);

  uint32_t aligned_length= arg_length + offset;
  if (alloc(aligned_length))
    return true;
  
  /*
    Note, this is only safe for big-endian UCS-2.
    If we add little-endian UCS-2 sometimes, this code
    will be more complicated. But it's OK for now.
  */
  memset(Ptr, 0, offset);
  memcpy(Ptr + offset, str, arg_length);
  Ptr[aligned_length]=0;
  /* str_length is always >= 0 as arg_length is != 0 */
  str_length= aligned_length;
  str_charset= cs;
  return false;
}


bool String::set_or_copy_aligned(const char *str,uint32_t arg_length,
                                 const CHARSET_INFO * const cs)
{
  /* How many bytes are in incomplete character */
  uint32_t offset= (arg_length % cs->mbminlen); 
  
  if (!offset) /* All characters are complete, just copy */
  {
    set(str, arg_length, cs);
    return false;
  }
  return copy_aligned(str, arg_length, offset, cs);
}

	/* Copy with charset conversion */

bool String::copy(const char *str, uint32_t arg_length,
		          const CHARSET_INFO * const from_cs,
				  const CHARSET_INFO * const to_cs, uint *errors)
{
  uint32_t offset;
  if (!needs_conversion(arg_length, from_cs, to_cs, &offset))
  {
    *errors= 0;
    return copy(str, arg_length, to_cs);
  }
  if ((from_cs == &my_charset_bin) && offset)
  {
    *errors= 0;
    return copy_aligned(str, arg_length, offset, to_cs);
  }
  uint32_t new_length= to_cs->mbmaxlen*arg_length;
  if (alloc(new_length))
    return true;
  str_length=copy_and_convert((char*) Ptr, new_length, to_cs,
                              str, arg_length, from_cs, errors);
  str_charset=to_cs;
  return false;
}


/*
  Set a string to the value of a latin1-string, keeping the original charset
  
  SYNOPSIS
    copy_or_set()
    str			String of a simple charset (latin1)
    arg_length		Length of string

  IMPLEMENTATION
    If string object is of a simple character set, set it to point to the
    given string.
    If not, make a copy and convert it to the new character set.

  RETURN
    0	ok
    1	Could not allocate result buffer

*/

bool String::set_ascii(const char *str, uint32_t arg_length)
{
  if (str_charset->mbminlen == 1)
  {
    set(str, arg_length, str_charset);
    return 0;
  }
  uint dummy_errors;
  return copy(str, arg_length, &my_charset_latin1, str_charset, &dummy_errors);
}


/* This is used by mysql.cc */

bool String::fill(uint32_t max_length,char fill_char)
{
  if (str_length > max_length)
    Ptr[str_length=max_length]=0;
  else
  {
    if (realloc(max_length))
      return true;
    memset(Ptr+str_length, fill_char, max_length-str_length);
    str_length=max_length;
  }
  return false;
}

bool String::append(const String &s)
{
  if (s.length())
  {
    if (realloc(str_length+s.length()))
      return true;
    memcpy(Ptr+str_length,s.ptr(),s.length());
    str_length+=s.length();
  }
  return false;
}


/*
  Append an ASCII string to the a string of the current character set
*/

bool String::append(const char *s,uint32_t arg_length)
{
  if (!arg_length)
    return false;

  /*
    For an ASCII incompatible string, e.g. UCS-2, we need to convert
  */
  if (str_charset->mbminlen > 1)
  {
    uint32_t add_length=arg_length * str_charset->mbmaxlen;
    uint dummy_errors;
    if (realloc(str_length+ add_length))
      return true;
    str_length+= copy_and_convert(Ptr+str_length, add_length, str_charset,
				  s, arg_length, &my_charset_latin1,
                                  &dummy_errors);
    return false;
  }

  /*
    For an ASCII compatinble string we can just append.
  */
  if (realloc(str_length+arg_length))
    return true;
  memcpy(Ptr+str_length,s,arg_length);
  str_length+=arg_length;
  return false;
}


/*
  Append a 0-terminated ASCII string
*/

bool String::append(const char *s)
{
  return append(s, strlen(s));
}


/*
  Append a string in the given charset to the string
  with character set recoding
*/

bool String::append(const char *s,uint32_t arg_length, const CHARSET_INFO * const cs)
{
  uint32_t dummy_offset;
  
  if (needs_conversion(arg_length, cs, str_charset, &dummy_offset))
  {
    uint32_t add_length= arg_length / cs->mbminlen * str_charset->mbmaxlen;
    uint dummy_errors;
    if (realloc(str_length + add_length)) 
      return true;
    str_length+= copy_and_convert(Ptr+str_length, add_length, str_charset,
				  s, arg_length, cs, &dummy_errors);
  }
  else
  {
    if (realloc(str_length + arg_length)) 
      return true;
    memcpy(Ptr + str_length, s, arg_length);
    str_length+= arg_length;
  }
  return false;
}


bool String::append(IO_CACHE* file, uint32_t arg_length)
{
  if (realloc(str_length+arg_length))
    return true;
  if (my_b_read(file, (uchar*) Ptr + str_length, arg_length))
  {
    shrink(str_length);
    return true;
  }
  str_length+=arg_length;
  return false;
}

bool String::append_with_prefill(const char *s,uint32_t arg_length,
		 uint32_t full_length, char fill_char)
{
  int t_length= arg_length > full_length ? arg_length : full_length;

  if (realloc(str_length + t_length))
    return true;
  t_length= full_length - arg_length;
  if (t_length > 0)
  {
    memset(Ptr+str_length, fill_char, t_length);
    str_length=str_length + t_length;
  }
  append(s, arg_length);
  return false;
}

uint32_t String::numchars()
{
  return str_charset->cset->numchars(str_charset, Ptr, Ptr+str_length);
}

int String::charpos(int i,uint32_t offset)
{
  if (i <= 0)
    return i;
  return str_charset->cset->charpos(str_charset,Ptr+offset,Ptr+str_length,i);
}

int String::strstr(const String &s,uint32_t offset)
{
  if (s.length()+offset <= str_length)
  {
    if (!s.length())
      return ((int) offset);	// Empty string is always found

    register const char *str = Ptr+offset;
    register const char *search=s.ptr();
    const char *end=Ptr+str_length-s.length()+1;
    const char *search_end=s.ptr()+s.length();
skip:
    while (str != end)
    {
      if (*str++ == *search)
      {
	register char *i,*j;
	i=(char*) str; j=(char*) search+1;
	while (j != search_end)
	  if (*i++ != *j++) goto skip;
	return (int) (str-Ptr) -1;
      }
    }
  }
  return -1;
}

/*
** Search string from end. Offset is offset to the end of string
*/

int String::strrstr(const String &s,uint32_t offset)
{
  if (s.length() <= offset && offset <= str_length)
  {
    if (!s.length())
      return offset;				// Empty string is always found
    register const char *str = Ptr+offset-1;
    register const char *search=s.ptr()+s.length()-1;

    const char *end=Ptr+s.length()-2;
    const char *search_end=s.ptr()-1;
skip:
    while (str != end)
    {
      if (*str-- == *search)
      {
	register char *i,*j;
	i=(char*) str; j=(char*) search-1;
	while (j != search_end)
	  if (*i-- != *j--) goto skip;
	return (int) (i-Ptr) +1;
      }
    }
  }
  return -1;
}

/*
  Replace substring with string
  If wrong parameter or not enough memory, do nothing
*/

bool String::replace(uint32_t offset,uint32_t arg_length,const String &to)
{
  return replace(offset,arg_length,to.ptr(),to.length());
}

bool String::replace(uint32_t offset,uint32_t arg_length,
                     const char *to, uint32_t to_length)
{
  long diff = (long) to_length-(long) arg_length;
  if (offset+arg_length <= str_length)
  {
    if (diff < 0)
    {
      if (to_length)
	memcpy(Ptr+offset,to,to_length);
      memcpy(Ptr+offset+to_length, Ptr+offset+arg_length,
             str_length-offset-arg_length);
    }
    else
    {
      if (diff)
      {
	if (realloc(str_length+(uint32_t) diff))
	  return true;
	bmove_upp((uchar*) Ptr+str_length+diff, (uchar*) Ptr+str_length,
		  str_length-offset-arg_length);
      }
      if (to_length)
	memcpy(Ptr+offset,to,to_length);
    }
    str_length+=(uint32_t) diff;
  }
  return false;
}


// added by Holyfoot for "geometry" needs
int String::reserve(uint32_t space_needed, uint32_t grow_by)
{
  if (Alloced_length < str_length + space_needed)
  {
    if (realloc(Alloced_length + max(space_needed, grow_by) - 1))
      return true;
  }
  return false;
}

void String::qs_append(const char *str, uint32_t len)
{
  memcpy(Ptr + str_length, str, len + 1);
  str_length += len;
}

void String::qs_append(double d)
{
  char *buff = Ptr + str_length;
  str_length+= my_gcvt(d, MY_GCVT_ARG_DOUBLE, FLOATING_POINT_BUFFER - 1, buff, NULL);
}

void String::qs_append(double *d)
{
  double ld;
  float8get(ld, (char*) d);
  qs_append(ld);
}

void String::qs_append(int i)
{
  char *buff= Ptr + str_length;
  char *end= int10_to_str(i, buff, -10);
  str_length+= (int) (end-buff);
}

void String::qs_append(uint i)
{
  char *buff= Ptr + str_length;
  char *end= int10_to_str(i, buff, 10);
  str_length+= (int) (end-buff);
}

/*
  Compare strings according to collation, without end space.

  SYNOPSIS
    sortcmp()
    s		First string
    t		Second string
    cs		Collation

  NOTE:
    Normally this is case sensitive comparison

  RETURN
  < 0	s < t
  0	s == t
  > 0	s > t
*/


int sortcmp(const String *s,const String *t, const CHARSET_INFO * const cs)
{
 return cs->coll->strnncollsp(cs,
                              (uchar *) s->ptr(),s->length(),
                              (uchar *) t->ptr(),t->length(), 0);
}


/*
  Compare strings byte by byte. End spaces are also compared.

  SYNOPSIS
    stringcmp()
    s		First string
    t		Second string

  NOTE:
    Strings are compared as a stream of uchars

  RETURN
  < 0	s < t
  0	s == t
  > 0	s > t
*/


int stringcmp(const String *s,const String *t)
{
  uint32_t s_len=s->length(),t_len=t->length(),len=min(s_len,t_len);
  int cmp= memcmp(s->ptr(), t->ptr(), len);
  return (cmp) ? cmp : (int) (s_len - t_len);
}


String *copy_if_not_alloced(String *to,String *from,uint32_t from_length)
{
  if (from->Alloced_length >= from_length)
    return from;
  if (from->alloced || !to || from == to)
  {
    (void) from->realloc(from_length);
    return from;
  }
  if (to->realloc(from_length))
    return from;				// Actually an error
  if ((to->str_length=min(from->str_length,from_length)))
    memcpy(to->Ptr,from->Ptr,to->str_length);
  to->str_charset=from->str_charset;
  return to;
}


/****************************************************************************
  Help functions
****************************************************************************/

/*
  copy a string from one character set to another
  
  SYNOPSIS
    copy_and_convert()
    to			Store result here
    to_cs		Character set of result string
    from		Copy from here
    from_length		Length of from string
    from_cs		From character set

  NOTES
    'to' must be big enough as form_length * to_cs->mbmaxlen

  RETURN
    length of bytes copied to 'to'
*/


static uint32_t
copy_and_convert_extended(char *to, uint32_t to_length,
                          const CHARSET_INFO * const to_cs, 
                          const char *from, uint32_t from_length,
                          const CHARSET_INFO * const from_cs,
                          uint *errors)
{
  int         cnvres;
  my_wc_t     wc;
  const uchar *from_end= (const uchar*) from+from_length;
  char *to_start= to;
  uchar *to_end= (uchar*) to+to_length;
  my_charset_conv_mb_wc mb_wc= from_cs->cset->mb_wc;
  my_charset_conv_wc_mb wc_mb= to_cs->cset->wc_mb;
  uint error_count= 0;

  while (1)
  {
    if ((cnvres= (*mb_wc)(from_cs, &wc, (uchar*) from,
				      from_end)) > 0)
      from+= cnvres;
    else if (cnvres == MY_CS_ILSEQ)
    {
      error_count++;
      from++;
      wc= '?';
    }
    else if (cnvres > MY_CS_TOOSMALL)
    {
      /*
        A correct multibyte sequence detected
        But it doesn't have Unicode mapping.
      */
      error_count++;
      from+= (-cnvres);
      wc= '?';
    }
    else
      break;  // Not enough characters

outp:
    if ((cnvres= (*wc_mb)(to_cs, wc, (uchar*) to, to_end)) > 0)
      to+= cnvres;
    else if (cnvres == MY_CS_ILUNI && wc != '?')
    {
      error_count++;
      wc= '?';
      goto outp;
    }
    else
      break;
  }
  *errors= error_count;
  return (uint32_t) (to - to_start);
}


/*
  Optimized for quick copying of ASCII characters in the range 0x00..0x7F.
*/
uint32_t
copy_and_convert(char *to, uint32_t to_length, const CHARSET_INFO * const to_cs, 
                 const char *from, uint32_t from_length,
				 const CHARSET_INFO * const from_cs, uint *errors)
{
  /*
    If any of the character sets is not ASCII compatible,
    immediately switch to slow mb_wc->wc_mb method.
  */
  if ((to_cs->state | from_cs->state) & MY_CS_NONASCII)
    return copy_and_convert_extended(to, to_length, to_cs,
                                     from, from_length, from_cs, errors);

  uint32_t length= min(to_length, from_length), length2= length;

#if defined(__i386__)
  /*
    Special loop for i386, it allows to refer to a
    non-aligned memory block as UINT32, which makes
    it possible to copy four bytes at once. This
    gives about 10% performance improvement comparing
    to byte-by-byte loop.
  */
  for ( ; length >= 4; length-= 4, from+= 4, to+= 4)
  {
    if ((*(uint32_t*)from) & 0x80808080)
      break;
    *((uint32_t*) to)= *((const uint32_t*) from);
  }
#endif

  for (; ; *to++= *from++, length--)
  {
    if (!length)
    {
      *errors= 0;
      return length2;
    }
    if (*((unsigned char*) from) > 0x7F) /* A non-ASCII character */
    {
      uint32_t copied_length= length2 - length;
      to_length-= copied_length;
      from_length-= copied_length;
      return copied_length + copy_and_convert_extended(to, to_length,
                                                       to_cs,
                                                       from, from_length,
                                                       from_cs,
                                                       errors);
    }
  }

  assert(false); // Should never get to here
  return 0;           // Make compiler happy
}


/**
  Copy string with HEX-encoding of "bad" characters.

  @details This functions copies the string pointed by "src"
  to the string pointed by "dst". Not more than "srclen" bytes
  are read from "src". Any sequences of bytes representing
  a not-well-formed substring (according to cs) are hex-encoded,
  and all well-formed substrings (according to cs) are copied as is.
  Not more than "dstlen" bytes are written to "dst". The number 
  of bytes written to "dst" is returned.
  
   @param      cs       character set pointer of the destination string
   @param[out] dst      destination string
   @param      dstlen   size of dst
   @param      src      source string
   @param      srclen   length of src

   @retval     result length
*/

size_t
my_copy_with_hex_escaping(const CHARSET_INFO * const cs,
                          char *dst, size_t dstlen,
                          const char *src, size_t srclen)
{
  const char *srcend= src + srclen;
  char *dst0= dst;

  for ( ; src < srcend ; )
  {
    size_t chlen;
    if ((chlen= my_ismbchar(cs, src, srcend)))
    {
      if (dstlen < chlen)
        break; /* purecov: inspected */
      memcpy(dst, src, chlen);
      src+= chlen;
      dst+= chlen;
      dstlen-= chlen;
    }
    else if (*src & 0x80)
    {
      if (dstlen < 4)
        break; /* purecov: inspected */
      *dst++= '\\';
      *dst++= 'x';
      *dst++= _dig_vec_upper[((unsigned char) *src) >> 4];
      *dst++= _dig_vec_upper[((unsigned char) *src) & 15];
      src++;
      dstlen-= 4;
    }
    else
    {
      if (dstlen < 1)
        break; /* purecov: inspected */
      *dst++= *src++;
      dstlen--;
    }
  }
  return dst - dst0;
}

/*
  copy a string,
  with optional character set conversion,
  with optional left padding (for binary -> UCS2 conversion)
  
  SYNOPSIS
    well_formed_copy_nchars()
    to			     Store result here
    to_length                Maxinum length of "to" string
    to_cs		     Character set of "to" string
    from		     Copy from here
    from_length		     Length of from string
    from_cs		     From character set
    nchars                   Copy not more that nchars characters
    well_formed_error_pos    Return position when "from" is not well formed
                             or NULL otherwise.
    cannot_convert_error_pos Return position where a not convertable
                             character met, or NULL otherwise.
    from_end_pos             Return position where scanning of "from"
                             string stopped.
  NOTES

  RETURN
    length of bytes copied to 'to'
*/


uint32_t
well_formed_copy_nchars(const CHARSET_INFO * const to_cs,
                        char *to, uint to_length,
                        const CHARSET_INFO * const from_cs,
                        const char *from, uint from_length,
                        uint nchars,
                        const char **well_formed_error_pos,
                        const char **cannot_convert_error_pos,
                        const char **from_end_pos)
{
  uint res;

  if ((to_cs == &my_charset_bin) || 
      (from_cs == &my_charset_bin) ||
      (to_cs == from_cs) ||
      my_charset_same(from_cs, to_cs))
  {
    if (to_length < to_cs->mbminlen || !nchars)
    {
      *from_end_pos= from;
      *cannot_convert_error_pos= NULL;
      *well_formed_error_pos= NULL;
      return 0;
    }

    if (to_cs == &my_charset_bin)
    {
      res= min(min(nchars, to_length), from_length);
      memmove(to, from, res);
      *from_end_pos= from + res;
      *well_formed_error_pos= NULL;
      *cannot_convert_error_pos= NULL;
    }
    else
    {
      int well_formed_error;
      uint from_offset;

      if ((from_offset= (from_length % to_cs->mbminlen)) &&
          (from_cs == &my_charset_bin))
      {
        /*
          Copying from BINARY to UCS2 needs to prepend zeros sometimes:
          INSERT INTO t1 (ucs2_column) VALUES (0x01);
          0x01 -> 0x0001
        */
        uint pad_length= to_cs->mbminlen - from_offset;
        memset(to, 0, pad_length);
        memmove(to + pad_length, from, from_offset);
        nchars--;
        from+= from_offset;
        from_length-= from_offset;
        to+= to_cs->mbminlen;
        to_length-= to_cs->mbminlen;
      }

      set_if_smaller(from_length, to_length);
      res= to_cs->cset->well_formed_len(to_cs, from, from + from_length,
                                        nchars, &well_formed_error);
      memmove(to, from, res);
      *from_end_pos= from + res;
      *well_formed_error_pos= well_formed_error ? from + res : NULL;
      *cannot_convert_error_pos= NULL;
      if (from_offset)
        res+= to_cs->mbminlen;
    }
  }
  else
  {
    int cnvres;
    my_wc_t wc;
    my_charset_conv_mb_wc mb_wc= from_cs->cset->mb_wc;
    my_charset_conv_wc_mb wc_mb= to_cs->cset->wc_mb;
    const uchar *from_end= (const uchar*) from + from_length;
    uchar *to_end= (uchar*) to + to_length;
    char *to_start= to;
    *well_formed_error_pos= NULL;
    *cannot_convert_error_pos= NULL;

    for ( ; nchars; nchars--)
    {
      const char *from_prev= from;
      if ((cnvres= (*mb_wc)(from_cs, &wc, (uchar*) from, from_end)) > 0)
        from+= cnvres;
      else if (cnvres == MY_CS_ILSEQ)
      {
        if (!*well_formed_error_pos)
          *well_formed_error_pos= from;
        from++;
        wc= '?';
      }
      else if (cnvres > MY_CS_TOOSMALL)
      {
        /*
          A correct multibyte sequence detected
          But it doesn't have Unicode mapping.
        */
        if (!*cannot_convert_error_pos)
          *cannot_convert_error_pos= from;
        from+= (-cnvres);
        wc= '?';
      }
      else
        break;  // Not enough characters

outp:
      if ((cnvres= (*wc_mb)(to_cs, wc, (uchar*) to, to_end)) > 0)
        to+= cnvres;
      else if (cnvres == MY_CS_ILUNI && wc != '?')
      {
        if (!*cannot_convert_error_pos)
          *cannot_convert_error_pos= from_prev;
        wc= '?';
        goto outp;
      }
      else
      {
        from= from_prev;
        break;
      }
    }
    *from_end_pos= from;
    res= to - to_start;
  }
  return (uint32_t) res;
}




void String::print(String *str)
{
  char *st= (char*)Ptr, *end= st+str_length;
  for (; st < end; st++)
  {
    uchar c= *st;
    switch (c)
    {
    case '\\':
      str->append(STRING_WITH_LEN("\\\\"));
      break;
    case '\0':
      str->append(STRING_WITH_LEN("\\0"));
      break;
    case '\'':
      str->append(STRING_WITH_LEN("\\'"));
      break;
    case '\n':
      str->append(STRING_WITH_LEN("\\n"));
      break;
    case '\r':
      str->append(STRING_WITH_LEN("\\r"));
      break;
    case '\032': // Ctrl-Z
      str->append(STRING_WITH_LEN("\\Z"));
      break;
    default:
      str->append(c);
    }
  }
}


/*
  Exchange state of this object and argument.

  SYNOPSIS
    String::swap()

  RETURN
    Target string will contain state of this object and vice versa.
*/

void String::swap(String &s)
{
  swap_variables(char *, Ptr, s.Ptr);
  swap_variables(uint32_t, str_length, s.str_length);
  swap_variables(uint32_t, Alloced_length, s.Alloced_length);
  swap_variables(bool, alloced, s.alloced);
  swap_variables(const CHARSET_INFO *, str_charset, s.str_charset);
}
