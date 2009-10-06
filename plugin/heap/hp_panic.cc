/* Copyright (C) 2000-2002 MySQL AB

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

#include "heap_priv.h"

using namespace std;

	/* if flag == HA_PANIC_CLOSE then all files are removed for more
	   memory */

int hp_panic(enum ha_panic_function flag)
{
  pthread_mutex_lock(&THR_LOCK_heap);
  list<HP_INFO *>::iterator info_it= heap_open_list.begin();
  while (info_it != heap_open_list.end())
  {
    HP_INFO *info= *info_it;
    switch (flag) {
    case HA_PANIC_CLOSE:
      hp_close(info);
      break;
    default:
      break;
    }
    ++info_it;
  }
  list<HP_SHARE *>::iterator share_it= heap_share_list.begin();
  while (share_it != heap_share_list.end())
  {
    HP_SHARE *share= *share_it;
    switch (flag) {
    case HA_PANIC_CLOSE:
    {
      if (!share->open_count)
	hp_free(share);
      break;
    }
    default:
      break;
    }
    ++share_it;
  }
  pthread_mutex_unlock(&THR_LOCK_heap);
  return(0);
} /* hp_panic */
