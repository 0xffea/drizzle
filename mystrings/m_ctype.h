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

/*
  A better inplementation of the UNIX ctype(3) library.
  Notes:   drizzled/global.h should be included before ctype.h
*/

#ifndef _m_ctype_h
#define _m_ctype_h

#ifdef	__cplusplus
extern "C" {
#endif

#define MY_CS_NAME_SIZE			32
#define MY_CS_CTYPE_TABLE_SIZE		257
#define MY_CS_TO_LOWER_TABLE_SIZE	256
#define MY_CS_TO_UPPER_TABLE_SIZE	256
#define MY_CS_SORT_ORDER_TABLE_SIZE	256
#define MY_CS_TO_UNI_TABLE_SIZE		256

#define CHARSET_DIR	"charsets/"

#define my_wc_t ulong

typedef struct unicase_info_st
{
  uint16_t toupper;
  uint16_t tolower;
  uint16_t sort;
} MY_UNICASE_INFO;


extern MY_UNICASE_INFO *my_unicase_default[256];
extern MY_UNICASE_INFO *my_unicase_turkish[256];

typedef struct uni_ctype_st
{
  unsigned char  pctype;
  unsigned char  *ctype;
} MY_UNI_CTYPE;

extern MY_UNI_CTYPE my_uni_ctype[256];

/* wm_wc and wc_mb return codes */
#define MY_CS_ILSEQ	0     /* Wrong by sequence: wb_wc                   */
#define MY_CS_ILUNI	0     /* Cannot encode Unicode to charset: wc_mb    */
#define MY_CS_TOOSMALL  -101  /* Need at least one byte:    wc_mb and mb_wc */
#define MY_CS_TOOSMALL2 -102  /* Need at least two bytes:   wc_mb and mb_wc */
#define MY_CS_TOOSMALL3 -103  /* Need at least three bytes: wc_mb and mb_wc */
/* These following three are currently not really used */
#define MY_CS_TOOSMALL4 -104  /* Need at least 4 bytes: wc_mb and mb_wc */
#define MY_CS_TOOSMALL5 -105  /* Need at least 5 bytes: wc_mb and mb_wc */
#define MY_CS_TOOSMALL6 -106  /* Need at least 6 bytes: wc_mb and mb_wc */
/* A helper macros for "need at least n bytes" */
#define MY_CS_TOOSMALLN(n)    (-100-(n))

#define MY_SEQ_INTTAIL	1
#define MY_SEQ_SPACES	2

        /* My charsets_list flags */
#define MY_CS_COMPILED  1      /* compiled-in sets               */
#define MY_CS_CONFIG    2      /* sets that have a *.conf file   */
#define MY_CS_INDEX     4      /* sets listed in the Index file  */
#define MY_CS_LOADED    8      /* sets that are currently loaded */
#define MY_CS_BINSORT	16     /* if binary sort order           */
#define MY_CS_PRIMARY	32     /* if primary collation           */
#define MY_CS_STRNXFRM	64     /* if strnxfrm is used for sort   */
#define MY_CS_UNICODE	128    /* is a charset is full unicode   */
#define MY_CS_READY	256    /* if a charset is initialized    */
#define MY_CS_AVAILABLE	512    /* If either compiled-in or loaded*/
#define MY_CS_CSSORT	1024   /* if case sensitive sort order   */	
#define MY_CS_HIDDEN	2048   /* don't display in SHOW          */	
#define MY_CS_PUREASCII 4096   /* if a charset is pure ascii     */
#define MY_CS_NONASCII  8192   /* if not ASCII-compatible        */
#define MY_CHARSET_UNDEFINED 0

/* Character repertoire flags */
#define MY_REPERTOIRE_ASCII      1 /* Pure ASCII            U+0000..U+007F */
#define MY_REPERTOIRE_EXTENDED   2 /* Extended characters:  U+0080..U+FFFF */
#define MY_REPERTOIRE_UNICODE30  3 /* ASCII | EXTENDED:     U+0000..U+FFFF */

/* Flags for strxfrm */
#define MY_STRXFRM_LEVEL1          0x00000001 /* for primary weights   */
#define MY_STRXFRM_LEVEL2          0x00000002 /* for secondary weights */
#define MY_STRXFRM_LEVEL3          0x00000004 /* for tertiary weights  */
#define MY_STRXFRM_LEVEL4          0x00000008 /* fourth level weights  */
#define MY_STRXFRM_LEVEL5          0x00000010 /* fifth level weights   */
#define MY_STRXFRM_LEVEL6          0x00000020 /* sixth level weights   */
#define MY_STRXFRM_LEVEL_ALL       0x0000003F /* Bit OR for the above six */
#define MY_STRXFRM_NLEVELS         6          /* Number of possible levels*/

#define MY_STRXFRM_PAD_WITH_SPACE  0x00000040 /* if pad result with spaces */
#define MY_STRXFRM_UNUSED_00000080 0x00000080 /* for future extensions     */

#define MY_STRXFRM_DESC_LEVEL1     0x00000100 /* if desc order for level1 */
#define MY_STRXFRM_DESC_LEVEL2     0x00000200 /* if desc order for level2 */
#define MY_STRXFRM_DESC_LEVEL3     0x00000300 /* if desc order for level3 */
#define MY_STRXFRM_DESC_LEVEL4     0x00000800 /* if desc order for level4 */
#define MY_STRXFRM_DESC_LEVEL5     0x00001000 /* if desc order for level5 */
#define MY_STRXFRM_DESC_LEVEL6     0x00002000 /* if desc order for level6 */
#define MY_STRXFRM_DESC_SHIFT      8

#define MY_STRXFRM_UNUSED_00004000 0x00004000 /* for future extensions     */
#define MY_STRXFRM_UNUSED_00008000 0x00008000 /* for future extensions     */

#define MY_STRXFRM_REVERSE_LEVEL1  0x00010000 /* if reverse order for level1 */
#define MY_STRXFRM_REVERSE_LEVEL2  0x00020000 /* if reverse order for level2 */
#define MY_STRXFRM_REVERSE_LEVEL3  0x00040000 /* if reverse order for level3 */
#define MY_STRXFRM_REVERSE_LEVEL4  0x00080000 /* if reverse order for level4 */
#define MY_STRXFRM_REVERSE_LEVEL5  0x00100000 /* if reverse order for level5 */
#define MY_STRXFRM_REVERSE_LEVEL6  0x00200000 /* if reverse order for level6 */
#define MY_STRXFRM_REVERSE_SHIFT   16


typedef struct my_uni_idx_st
{
  uint16_t from;
  uint16_t to;
  unsigned char  *tab;
} MY_UNI_IDX;

typedef struct
{
  uint32_t beg;
  uint32_t end;
  uint32_t mb_len;
} my_match_t;

enum my_lex_states
{
  MY_LEX_START, MY_LEX_CHAR, MY_LEX_IDENT, 
  MY_LEX_IDENT_SEP, MY_LEX_IDENT_START,
  MY_LEX_REAL, MY_LEX_HEX_NUMBER, MY_LEX_BIN_NUMBER,
  MY_LEX_CMP_OP, MY_LEX_LONG_CMP_OP, MY_LEX_STRING, MY_LEX_COMMENT, MY_LEX_END,
  MY_LEX_OPERATOR_OR_IDENT, MY_LEX_NUMBER_IDENT, MY_LEX_INT_OR_REAL,
  MY_LEX_REAL_OR_POINT, MY_LEX_BOOL, MY_LEX_EOL, MY_LEX_ESCAPE, 
  MY_LEX_LONG_COMMENT, MY_LEX_END_LONG_COMMENT, MY_LEX_SEMICOLON, 
  MY_LEX_SET_VAR, MY_LEX_USER_END, MY_LEX_HOSTNAME, MY_LEX_SKIP, 
  MY_LEX_USER_VARIABLE_DELIMITER, MY_LEX_SYSTEM_VAR,
  MY_LEX_IDENT_OR_KEYWORD,
  MY_LEX_IDENT_OR_HEX, MY_LEX_IDENT_OR_BIN,
  MY_LEX_STRING_OR_DELIMITER
};

struct charset_info_st;


/* See strings/CHARSET_INFO.txt for information about this structure  */
typedef struct my_collation_handler_st
{
  bool (*init)(struct charset_info_st *, void *(*alloc)(size_t));
  /* Collation routines */
  int     (*strnncoll)(const struct charset_info_st * const,
		       const unsigned char *, size_t, const unsigned char *, size_t, bool);
  int     (*strnncollsp)(const struct charset_info_st * const,
                         const unsigned char *, size_t, const unsigned char *, size_t,
                         bool diff_if_only_endspace_difference);
  size_t  (*strnxfrm)(const struct charset_info_st * const,
                      unsigned char *dst, size_t dstlen, uint32_t nweights,
                      const unsigned char *src, size_t srclen, uint32_t flags);
  size_t    (*strnxfrmlen)(const struct charset_info_st * const, size_t);
  bool (*like_range)(const struct charset_info_st * const,
                        const char *s, size_t s_length,
                        char escape, char w_one, char w_many,
                        size_t res_length,
                        char *min_str, char *max_str,
                        size_t *min_len, size_t *max_len);
  int     (*wildcmp)(const struct charset_info_st * const,
  		     const char *str,const char *str_end,
                     const char *wildstr,const char *wildend,
                     int escape,int w_one, int w_many);

  int  (*strcasecmp)(const struct charset_info_st * const, const char *, const char *);
  
  uint32_t (*instr)(const struct charset_info_st * const,
                const char *b, size_t b_length,
                const char *s, size_t s_length,
                my_match_t *match, uint32_t nmatch);
  
  /* Hash calculation */
  void (*hash_sort)(const struct charset_info_st *cs, const unsigned char *key, size_t len,
                    uint32_t *nr1, uint32_t *nr2); 
  bool (*propagate)(const struct charset_info_st *cs, const unsigned char *str, size_t len);
} MY_COLLATION_HANDLER;

extern MY_COLLATION_HANDLER my_collation_mb_bin_handler;
extern MY_COLLATION_HANDLER my_collation_8bit_bin_handler;
extern MY_COLLATION_HANDLER my_collation_8bit_simple_ci_handler;
extern MY_COLLATION_HANDLER my_collation_ucs2_uca_handler;

/* Some typedef to make it easy for C++ to make function pointers */
typedef int (*my_charset_conv_mb_wc)(const struct charset_info_st * const, my_wc_t *,
                                     const unsigned char *, const unsigned char *);
typedef int (*my_charset_conv_wc_mb)(const struct charset_info_st * const, my_wc_t,
                                     unsigned char *, unsigned char *);
typedef size_t (*my_charset_conv_case)(const struct charset_info_st * const,
                                       char *, size_t, char *, size_t);


/* See strings/CHARSET_INFO.txt about information on this structure  */
typedef struct my_charset_handler_st
{
  bool (*init)(struct charset_info_st *, void *(*alloc)(size_t));
  /* Multibyte routines */
  uint32_t    (*ismbchar)(const struct charset_info_st * const, const char *, const char *);
  uint32_t    (*mbcharlen)(const struct charset_info_st * const, uint32_t c);
  size_t  (*numchars)(const struct charset_info_st * const, const char *b, const char *e);
  size_t  (*charpos)(const struct charset_info_st * const, const char *b, const char *e,
                     size_t pos);
  size_t  (*well_formed_len)(const struct charset_info_st * const,
                             const char *b,const char *e,
                             size_t nchars, int *error);
  size_t  (*lengthsp)(const struct charset_info_st * const, const char *ptr, size_t length);
  size_t  (*numcells)(const struct charset_info_st * const, const char *b, const char *e);
  
  /* Unicode conversion */
  my_charset_conv_mb_wc mb_wc;
  my_charset_conv_wc_mb wc_mb;

  /* CTYPE scanner */
  int (*ctype)(const struct charset_info_st *cs, int *ctype,
               const unsigned char *s, const unsigned char *e);
  
  /* Functions for case and sort conversion */
  size_t  (*caseup_str)(const struct charset_info_st * const, char *);
  size_t  (*casedn_str)(const struct charset_info_st * const, char *);

  my_charset_conv_case caseup;
  my_charset_conv_case casedn;

  /* Charset dependant snprintf() */
  size_t (*snprintf)(const struct charset_info_st * const, char *to, size_t n,
                     const char *fmt,
                     ...) __attribute__((format(printf, 4, 5)));
  size_t (*long10_to_str)(const struct charset_info_st * const, char *to, size_t n,
                          int radix, long int val);
  size_t (*int64_t10_to_str)(const struct charset_info_st * const, char *to, size_t n,
                              int radix, int64_t val);
  
  void (*fill)(const struct charset_info_st * const, char *to, size_t len, int fill);
  
  /* String-to-number conversion routines */
  long        (*strntol)(const struct charset_info_st * const, const char *s, size_t l,
			 int base, char **e, int *err);
  ulong      (*strntoul)(const struct charset_info_st * const, const char *s, size_t l,
			 int base, char **e, int *err);
  int64_t   (*strntoll)(const struct charset_info_st * const, const char *s, size_t l,
			 int base, char **e, int *err);
  uint64_t (*strntoull)(const struct charset_info_st * const, const char *s, size_t l,
			 int base, char **e, int *err);
  double      (*strntod)(const struct charset_info_st * const, char *s, size_t l, char **e,
			 int *err);
  int64_t    (*strtoll10)(const struct charset_info_st *cs,
                           const char *nptr, char **endptr, int *error);
  uint64_t   (*strntoull10rnd)(const struct charset_info_st *cs,
                                const char *str, size_t length,
                                int unsigned_fl,
                                char **endptr, int *error);
  size_t        (*scan)(const struct charset_info_st * const, const char *b, const char *e,
                        int sq);
} MY_CHARSET_HANDLER;

extern MY_CHARSET_HANDLER my_charset_8bit_handler;
extern MY_CHARSET_HANDLER my_charset_ucs2_handler;


/* See strings/CHARSET_INFO.txt about information on this structure  */
typedef struct charset_info_st
{
  uint32_t      number;
  uint32_t      primary_number;
  uint32_t      binary_number;
  uint32_t      state;
  const char *csname;
  const char *name;
  const char *comment;
  const char *tailoring;
  unsigned char    *ctype;
  unsigned char    *to_lower;
  unsigned char    *to_upper;
  unsigned char    *sort_order;
  uint16_t   *contractions;
  uint16_t   **sort_order_big;
  uint16_t      *tab_to_uni;
  MY_UNI_IDX  *tab_from_uni;
  MY_UNICASE_INFO **caseinfo;
  unsigned char     *state_map;
  unsigned char     *ident_map;
  uint32_t      strxfrm_multiply;
  unsigned char     caseup_multiply;
  unsigned char     casedn_multiply;
  uint32_t      mbminlen;
  uint32_t      mbmaxlen;
  uint16_t    min_sort_char;
  uint16_t    max_sort_char; /* For LIKE optimization */
  unsigned char     pad_char;
  bool   escape_with_backslash_is_dangerous;
  unsigned char     levels_for_compare;
  unsigned char     levels_for_order;
  
  MY_CHARSET_HANDLER *cset;
  MY_COLLATION_HANDLER *coll;
  
} CHARSET_INFO;
#define ILLEGAL_CHARSET_INFO_NUMBER (UINT32_MAX)


extern CHARSET_INFO my_charset_bin;
extern CHARSET_INFO my_charset_filename;
extern CHARSET_INFO my_charset_latin1;
extern CHARSET_INFO my_charset_latin1_german2_ci;
extern CHARSET_INFO my_charset_latin1_bin;
extern CHARSET_INFO my_charset_latin2_czech_ci;
extern CHARSET_INFO my_charset_utf8mb3_bin;
extern CHARSET_INFO my_charset_utf8mb3_general_ci;
extern CHARSET_INFO my_charset_utf8mb3_unicode_ci;
extern CHARSET_INFO my_charset_utf8mb4_bin;
extern CHARSET_INFO my_charset_utf8mb4_general_ci;
extern CHARSET_INFO my_charset_utf8mb4_unicode_ci;

#define MY_UTF8MB3                 "utf8mb3"
#define MY_UTF8MB4                 "utf8"
#define my_charset_utf8_general_ci my_charset_utf8mb4_general_ci
#define my_charset_utf8_bin        my_charset_utf8mb4_bin


/* declarations for simple charsets */
extern size_t my_strnxfrm_simple(const CHARSET_INFO * const,
                                 unsigned char *dst, size_t dstlen, uint32_t nweights,
                                 const unsigned char *src, size_t srclen, uint32_t flags);
size_t my_strnxfrmlen_simple(const CHARSET_INFO * const, size_t);
extern int  my_strnncoll_simple(const CHARSET_INFO * const, const unsigned char *, size_t,
				const unsigned char *, size_t, bool);

extern int  my_strnncollsp_simple(const CHARSET_INFO * const, const unsigned char *, size_t,
                                  const unsigned char *, size_t,
                                  bool diff_if_only_endspace_difference);

extern void my_hash_sort_simple(const CHARSET_INFO * const cs,
				const unsigned char *key, size_t len,
				uint32_t *nr1, uint32_t *nr2); 

extern size_t my_lengthsp_8bit(const CHARSET_INFO * const cs, const char *ptr, size_t length);

extern uint32_t my_instr_simple(const CHARSET_INFO * const,
                            const char *b, size_t b_length,
                            const char *s, size_t s_length,
                            my_match_t *match, uint32_t nmatch);


/* Functions for 8bit */
extern size_t my_caseup_str_8bit(const CHARSET_INFO * const, char *);
extern size_t my_casedn_str_8bit(const CHARSET_INFO * const, char *);
extern size_t my_caseup_8bit(const CHARSET_INFO * const, char *src, size_t srclen,
                             char *dst, size_t dstlen);
extern size_t my_casedn_8bit(const CHARSET_INFO * const, char *src, size_t srclen,
                             char *dst, size_t dstlen);

extern int my_strcasecmp_8bit(const CHARSET_INFO * const  cs, const char *, const char *);

int my_mb_wc_8bit(const CHARSET_INFO * const cs,my_wc_t *wc, const unsigned char *s,const unsigned char *e);
int my_wc_mb_8bit(const CHARSET_INFO * const cs,my_wc_t wc, unsigned char *s, unsigned char *e);

int my_mb_ctype_8bit(const CHARSET_INFO * const,int *, const unsigned char *,const unsigned char *);
int my_mb_ctype_mb(const CHARSET_INFO * const,int *, const unsigned char *,const unsigned char *);

size_t my_scan_8bit(const CHARSET_INFO * const cs, const char *b, const char *e, int sq);

size_t my_snprintf_8bit(const CHARSET_INFO * const, char *to, size_t n,
                        const char *fmt, ...)
  __attribute__((format(printf, 4, 5)));

long       my_strntol_8bit(const CHARSET_INFO * const, const char *s, size_t l, int base,
                           char **e, int *err);
ulong      my_strntoul_8bit(const CHARSET_INFO * const, const char *s, size_t l, int base,
			    char **e, int *err);
int64_t   my_strntoll_8bit(const CHARSET_INFO * const, const char *s, size_t l, int base,
			    char **e, int *err);
uint64_t my_strntoull_8bit(const CHARSET_INFO * const, const char *s, size_t l, int base,
			    char **e, int *err);
double      my_strntod_8bit(const CHARSET_INFO * const, char *s, size_t l,char **e,
			    int *err);
size_t my_long10_to_str_8bit(const CHARSET_INFO * const, char *to, size_t l, int radix,
                             long int val);
size_t my_int64_t10_to_str_8bit(const CHARSET_INFO * const, char *to, size_t l, int radix,
                                 int64_t val);

int64_t my_strtoll10_8bit(const CHARSET_INFO * const cs,
                           const char *nptr, char **endptr, int *error);
int64_t my_strtoll10_ucs2(CHARSET_INFO *cs, 
                           const char *nptr, char **endptr, int *error);

uint64_t my_strntoull10rnd_8bit(const CHARSET_INFO * const cs,
                                 const char *str, size_t length, int
                                 unsigned_fl, char **endptr, int *error);
uint64_t my_strntoull10rnd_ucs2(CHARSET_INFO *cs, 
                                 const char *str, size_t length,
                                 int unsigned_fl, char **endptr, int *error);

void my_fill_8bit(const CHARSET_INFO * const cs, char* to, size_t l, int fill);

bool  my_like_range_simple(const CHARSET_INFO * const cs,
			      const char *ptr, size_t ptr_length,
			      char escape, char w_one, char w_many,
			      size_t res_length,
			      char *min_str, char *max_str,
			      size_t *min_length, size_t *max_length);

bool  my_like_range_mb(const CHARSET_INFO * const cs,
			  const char *ptr, size_t ptr_length,
			  char escape, char w_one, char w_many,
			  size_t res_length,
			  char *min_str, char *max_str,
			  size_t *min_length, size_t *max_length);

bool  my_like_range_ucs2(const CHARSET_INFO * const cs,
			    const char *ptr, size_t ptr_length,
			    char escape, char w_one, char w_many,
			    size_t res_length,
			    char *min_str, char *max_str,
			    size_t *min_length, size_t *max_length);

bool  my_like_range_utf16(const CHARSET_INFO * const cs,
			     const char *ptr, size_t ptr_length,
			     char escape, char w_one, char w_many,
			     size_t res_length,
			     char *min_str, char *max_str,
			     size_t *min_length, size_t *max_length);

bool  my_like_range_utf32(const CHARSET_INFO * const cs,
			     const char *ptr, size_t ptr_length,
			     char escape, char w_one, char w_many,
			     size_t res_length,
			     char *min_str, char *max_str,
			     size_t *min_length, size_t *max_length);


int my_wildcmp_8bit(const CHARSET_INFO * const,
		    const char *str,const char *str_end,
		    const char *wildstr,const char *wildend,
		    int escape, int w_one, int w_many);

int my_wildcmp_bin(const CHARSET_INFO * const,
		   const char *str,const char *str_end,
		   const char *wildstr,const char *wildend,
		   int escape, int w_one, int w_many);

size_t my_numchars_8bit(const CHARSET_INFO * const, const char *b, const char *e);
size_t my_numcells_8bit(const CHARSET_INFO * const, const char *b, const char *e);
size_t my_charpos_8bit(const CHARSET_INFO * const, const char *b, const char *e, size_t pos);
size_t my_well_formed_len_8bit(const CHARSET_INFO * const, const char *b, const char *e,
                             size_t pos, int *error);
uint32_t my_mbcharlen_8bit(const CHARSET_INFO * const, uint32_t c);


/* Functions for multibyte charsets */
extern size_t my_caseup_str_mb(const CHARSET_INFO * const, char *);
extern size_t my_casedn_str_mb(const CHARSET_INFO * const, char *);
extern size_t my_caseup_mb(const CHARSET_INFO * const, char *src, size_t srclen,
                                         char *dst, size_t dstlen);
extern size_t my_casedn_mb(const CHARSET_INFO * const, char *src, size_t srclen,
                                         char *dst, size_t dstlen);
extern int my_strcasecmp_mb(const CHARSET_INFO * const  cs, const char *s, const char *t);

int my_wildcmp_mb(const CHARSET_INFO * const,
		  const char *str,const char *str_end,
		  const char *wildstr,const char *wildend,
		  int escape, int w_one, int w_many);
size_t my_numchars_mb(const CHARSET_INFO * const, const char *b, const char *e);
size_t my_numcells_mb(const CHARSET_INFO * const, const char *b, const char *e);
size_t my_charpos_mb(const CHARSET_INFO * const, const char *b, const char *e, size_t pos);
size_t my_well_formed_len_mb(const CHARSET_INFO * const, const char *b, const char *e,
                             size_t pos, int *error);
uint32_t my_instr_mb(const CHARSET_INFO * const,
                 const char *b, size_t b_length,
                 const char *s, size_t s_length,
                 my_match_t *match, uint32_t nmatch);

int my_strnncoll_mb_bin(const CHARSET_INFO * const  cs,
                        const unsigned char *s, size_t slen,
                        const unsigned char *t, size_t tlen,
                        bool t_is_prefix);

int my_strnncollsp_mb_bin(const CHARSET_INFO * const cs,
                          const unsigned char *a, size_t a_length,
                          const unsigned char *b, size_t b_length,
                          bool diff_if_only_endspace_difference);

int my_wildcmp_mb_bin(const CHARSET_INFO * const cs,
                      const char *str,const char *str_end,
                      const char *wildstr,const char *wildend,
                      int escape, int w_one, int w_many);

int my_strcasecmp_mb_bin(const CHARSET_INFO * const  cs __attribute__((unused)),
                         const char *s, const char *t);

void my_hash_sort_mb_bin(const CHARSET_INFO * const cs __attribute__((unused)),
                         const unsigned char *key, size_t len, uint32_t *nr1, uint32_t *nr2);

size_t my_strnxfrm_mb(const CHARSET_INFO * const,
                      unsigned char *dst, size_t dstlen, uint32_t nweights,
                      const unsigned char *src, size_t srclen, uint32_t flags);

int my_wildcmp_unicode(const CHARSET_INFO * const cs,
                       const char *str, const char *str_end,
                       const char *wildstr, const char *wildend,
                       int escape, int w_one, int w_many,
                       MY_UNICASE_INFO **weights);

extern bool my_parse_charset_xml(const char *bug, size_t len,
				    int (*add)(CHARSET_INFO *cs));

bool my_propagate_simple(const CHARSET_INFO * const cs, const unsigned char *str, size_t len);
bool my_propagate_complex(const CHARSET_INFO * const cs, const unsigned char *str, size_t len);


uint32_t my_string_repertoire(const CHARSET_INFO * const cs, const char *str, ulong len);
bool my_charset_is_ascii_based(const CHARSET_INFO * const cs);
bool my_charset_is_8bit_pure_ascii(const CHARSET_INFO * const cs);


uint32_t my_strxfrm_flag_normalize(uint32_t flags, uint32_t nlevels);
void my_strxfrm_desc_and_reverse(unsigned char *str, unsigned char *strend,
                                 uint32_t flags, uint32_t level);
size_t my_strxfrm_pad_desc_and_reverse(const CHARSET_INFO * const cs,
                                       unsigned char *str, unsigned char *frmend, unsigned char *strend,
                                       uint32_t nweights, uint32_t flags, uint32_t level);

bool my_charset_is_ascii_compatible(const CHARSET_INFO * const cs);

#define	_MY_U	01	/* Upper case */
#define	_MY_L	02	/* Lower case */
#define	_MY_NMR	04	/* Numeral (digit) */
#define	_MY_SPC	010	/* Spacing character */
#define	_MY_PNT	020	/* Punctuation */
#define	_MY_CTR	040	/* Control character */
#define	_MY_B	0100	/* Blank */
#define	_MY_X	0200	/* heXadecimal digit */


#define	my_isascii(c)	(!((c) & ~0177))
#define	my_toascii(c)	((c) & 0177)
#define my_tocntrl(c)	((c) & 31)
#define my_toprint(c)	((c) | 64)
#define my_toupper(s,c)	(char) ((s)->to_upper[(unsigned char) (c)])
#define my_tolower(s,c)	(char) ((s)->to_lower[(unsigned char) (c)])
#define	my_isalpha(s, c)  (((s)->ctype+1)[(unsigned char) (c)] & (_MY_U | _MY_L))
#define	my_isupper(s, c)  (((s)->ctype+1)[(unsigned char) (c)] & _MY_U)
#define	my_islower(s, c)  (((s)->ctype+1)[(unsigned char) (c)] & _MY_L)
#define	my_isdigit(s, c)  (((s)->ctype+1)[(unsigned char) (c)] & _MY_NMR)
#define	my_isxdigit(s, c) (((s)->ctype+1)[(unsigned char) (c)] & _MY_X)
#define	my_isalnum(s, c)  (((s)->ctype+1)[(unsigned char) (c)] & (_MY_U | _MY_L | _MY_NMR))
#define	my_isspace(s, c)  (((s)->ctype+1)[(unsigned char) (c)] & _MY_SPC)
#define	my_ispunct(s, c)  (((s)->ctype+1)[(unsigned char) (c)] & _MY_PNT)
#define	my_isprint(s, c)  (((s)->ctype+1)[(unsigned char) (c)] & (_MY_PNT | _MY_U | _MY_L | _MY_NMR | _MY_B))
#define	my_isgraph(s, c)  (((s)->ctype+1)[(unsigned char) (c)] & (_MY_PNT | _MY_U | _MY_L | _MY_NMR))
#define	my_iscntrl(s, c)  (((s)->ctype+1)[(unsigned char) (c)] & _MY_CTR)

/* Some macros that should be cleaned up a little */
#define my_isvar(s,c)                 (my_isalnum(s,c) || (c) == '_')
#define my_isvar_start(s,c)           (my_isalpha(s,c) || (c) == '_')

#define my_binary_compare(s)	      ((s)->state  & MY_CS_BINSORT)
#define use_strnxfrm(s)               ((s)->state  & MY_CS_STRNXFRM)
#define my_strnxfrm(cs, d, dl, s, sl) \
   ((cs)->coll->strnxfrm((cs), (d), (dl), (dl), (s), (sl), MY_STRXFRM_PAD_WITH_SPACE))
#define my_strnncoll(s, a, b, c, d) ((s)->coll->strnncoll((s), (a), (b), (c), (d), 0))
#define my_like_range(s, a, b, c, d, e, f, g, h, i, j) \
   ((s)->coll->like_range((s), (a), (b), (c), (d), (e), (f), (g), (h), (i), (j)))
#define my_wildcmp(cs,s,se,w,we,e,o,m) ((cs)->coll->wildcmp((cs),(s),(se),(w),(we),(e),(o),(m)))
#define my_strcasecmp(s, a, b)        ((s)->coll->strcasecmp((s), (a), (b)))
#define my_charpos(cs, b, e, num)     (cs)->cset->charpos((cs), (const char*) (b), (const char *)(e), (num))


#define use_mb(s)                     ((s)->cset->ismbchar != NULL)
#define my_ismbchar(s, a, b)          ((s)->cset->ismbchar((s), (a), (b)))
#ifdef USE_MB
#define my_mbcharlen(s, a)            ((s)->cset->mbcharlen((s),(a)))
#else
#define my_mbcharlen(s, a)            1
#endif

#define my_caseup_str(s, a)           ((s)->cset->caseup_str((s), (a)))
#define my_casedn_str(s, a)           ((s)->cset->casedn_str((s), (a)))
#define my_strntol(s, a, b, c, d, e)  ((s)->cset->strntol((s),(a),(b),(c),(d),(e)))
#define my_strntoul(s, a, b, c, d, e) ((s)->cset->strntoul((s),(a),(b),(c),(d),(e)))
#define my_strntoll(s, a, b, c, d, e) ((s)->cset->strntoll((s),(a),(b),(c),(d),(e)))
#define my_strntoull(s, a, b, c,d, e) ((s)->cset->strntoull((s),(a),(b),(c),(d),(e)))
#define my_strntod(s, a, b, c, d)     ((s)->cset->strntod((s),(a),(b),(c),(d)))


/* XXX: still need to take care of this one */
#ifdef MY_CHARSET_TIS620
#error The TIS620 charset is broken at the moment.  Tell tim to fix it.
#define USE_TIS620
#include <mystrings/t_ctype.h>
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _m_ctype_h */
