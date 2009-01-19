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
  Static variables for mysys library. All definied here for easy making of
  a shared library
*/

#include "mysys_priv.h"
#include <mysys/mysys_err.h>
#include "my_static.h"
#include <stdlib.h>

bool timed_mutexes= 0;

	/* from my_init */
char *	home_dir=0;
const char      *my_progname=0;
char curr_dir[FN_REFLEN]= {0},
     home_dir_buff[FN_REFLEN]= {0};
uint32_t		my_stream_opened=0,my_file_opened=0, my_tmp_file_created=0;
uint32_t           my_file_total_opened= 0;
int my_umask=0664, my_umask_dir=0777;
struct st_my_file_info my_file_info_default[MY_NFILE]= {{0,UNOPEN}};
uint32_t   my_file_limit= MY_NFILE;
struct st_my_file_info *my_file_info= my_file_info_default;

	/* From mf_brkhant */
int my_dont_interrupt=0;
volatile int		_my_signals=0;
struct st_remember _my_sig_remember[MAX_SIGNALS]={{0,0}};
sigset_t my_signals;			/* signals blocked by mf_brkhant */

	/* from mf_reccache.c */
uint32_t my_default_record_cache_size=RECORD_CACHE_SIZE;

	/* from soundex.c */
				/* ABCDEFGHIJKLMNOPQRSTUVWXYZ */
				/* :::::::::::::::::::::::::: */
const char *soundex_map=	  "01230120022455012623010202";

	/* from safe_malloc */
uint32_t sf_malloc_prehunc=0,		/* If you have problem with core- */
     sf_malloc_endhunc=0,		/* dump when malloc-message.... */
					/* set theese to 64 or 128  */
     sf_malloc_quick=0;			/* set if no calls to sanity */
uint32_t sf_malloc_cur_memory= 0L;		/* Current memory usage */
uint32_t sf_malloc_max_memory= 0L;		/* Maximum memory usage */
uint32_t  sf_malloc_count= 0;		/* Number of times NEW() was called */
unsigned char *sf_min_adress= (unsigned char*) ~(unsigned long) 0L,
     *sf_max_adress= (unsigned char*) 0L;
/* Root of the linked list of struct st_irem */
struct st_irem *sf_malloc_root = NULL;

	/* from my_alarm */
int volatile my_have_got_alarm=0;	/* declare variable to reset */
uint32_t my_time_to_wait_for_lock=2;	/* In seconds */

	/* from errors.c */
void_ptr_int_func my_abort_hook= (void_ptr_int_func)exit;
error_handler_func error_handler_hook= NULL;

	/* How to disable options */
bool my_disable_async_io=0;
bool my_disable_flush_key_blocks=0;
bool my_disable_symlinks=0;
bool mysys_uses_curses=0;
