/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 - 2010 Toru Maesaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
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

#include "ha_blitz.h"

/* Unlike the data dictionary, don't tune the btree by default
   since the default configuration satisfies BlitzDB's default
   performance requirements. Tuning parameters will be made dynamic
   in the upcoming releases. */
int BlitzTree::open(const char *path, const int key_num, int mode) {
  char name_buffer[FN_REFLEN];

  if ((btree = tcbdbnew()) == NULL)
    return HA_ERR_OUT_OF_MEM;

  if (!tcbdbsetmutex(btree)) {
    tcbdbdel(btree);
    return HA_ERR_CRASHED_ON_USAGE;
  }

  snprintf(name_buffer, FN_REFLEN, "%s_%02d%s", path, key_num, BLITZ_INDEX_EXT);

  if (!tcbdbopen(btree, name_buffer, mode)) {
    tcbdbdel(btree);
    return HA_ERR_CRASHED_ON_USAGE;
  }

  return 0;
}

/* Similar to UNIX touch(1) but generates a TCBDB file. */
int BlitzTree::create(const char *path, const int key_num) {
  int rv;

  if ((rv = this->open(path, key_num, (BDBOWRITER | BDBOCREAT))) != 0)
    return rv;

  if ((rv = this->close()) != 0)
    return rv;

  return rv;
}

int BlitzTree::close(void) {
  assert(btree);

  if (!tcbdbclose(btree)) {
    tcbdbdel(btree);
    return HA_ERR_CRASHED_ON_USAGE;
  }

  tcbdbdel(btree);
  return 0;
}

uint64_t BlitzTree::records(void) {
  return tcbdbrnum(btree);
}
