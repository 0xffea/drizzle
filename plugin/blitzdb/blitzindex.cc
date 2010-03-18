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

#include <config.h>
#include "ha_blitz.h"

/* Unlike the data dictionary, don't tune the btree by default
   since the default configuration satisfies BlitzDB's default
   performance requirements. Tuning parameters will be made dynamic
   in the upcoming releases. */
int BlitzTree::open(const char *path, const int key_num, int mode) {
  char buf[FN_REFLEN];

  if ((btree = tcbdbnew()) == NULL)
    return HA_ERR_OUT_OF_MEM;

  if ((bt_cursor = tcbdbcurnew(btree)) == NULL) {
    tcbdbdel(btree);
    return HA_ERR_OUT_OF_MEM;
  }

  if (!tcbdbsetmutex(btree)) {
    tcbdbdel(btree);
    tcbdbcurdel(bt_cursor);
    return HA_ERR_CRASHED_ON_USAGE;
  }

  if (!tcbdbsetcmpfunc(btree, blitz_keycmp_cb, this)) {
    tcbdbdel(btree);
    tcbdbcurdel(bt_cursor);
    return HA_ERR_CRASHED_ON_USAGE;
  }

  snprintf(buf, FN_REFLEN, "%s_%02d%s", path, key_num, BLITZ_INDEX_EXT);

  if (!tcbdbopen(btree, buf, mode)) {
    tcbdbdel(btree);
    tcbdbcurdel(bt_cursor);
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

int BlitzTree::drop(const char *path, const int key_num) {
  char buf[FN_REFLEN];
  snprintf(buf, FN_REFLEN, "%s_%02d%s", path, key_num, BLITZ_INDEX_EXT);
  return unlink(buf);
}

int BlitzTree::rename(const char *from, const char *to, const int key_num) {
  char from_buf[FN_REFLEN];
  char to_buf[FN_REFLEN];

  snprintf(from_buf, FN_REFLEN, "%s_%02d%s", from, key_num, BLITZ_INDEX_EXT);
  snprintf(to_buf, FN_REFLEN, "%s_%02d%s", to, key_num, BLITZ_INDEX_EXT);

  return std::rename(from_buf, to_buf);
}

int BlitzTree::close(void) {
  assert(btree);

  if (!tcbdbclose(btree)) {
    tcbdbdel(btree);
    return HA_ERR_CRASHED_ON_USAGE;
  }

  tcbdbcurdel(bt_cursor);
  bt_cursor = NULL;

  tcbdbdel(btree);
  return 0;
}

int BlitzTree::write(const char *key, const size_t klen) {
  return (tcbdbputdup(btree, key, klen, "", 0)) ? 0 : -1;
}

int BlitzTree::write_unique(const char *key, const size_t klen) {
  if (!tcbdbputkeep(btree, key, klen, "", 0)) {
    if (tcbdbecode(btree) == TCEKEEP) {
      errno = HA_ERR_FOUND_DUPP_KEY;
      return HA_ERR_FOUND_DUPP_KEY;
    }
  }
  return 0;
}

char *BlitzTree::first_key(int *key_len) {
  if (!tcbdbcurfirst(bt_cursor))
    return NULL;

  return (char *)tcbdbcurkey(bt_cursor, key_len);
}

char *BlitzTree::final_key(int *key_len) {
  if (!tcbdbcurlast(bt_cursor))
    return NULL;

  return (char *)tcbdbcurkey(bt_cursor, key_len);
}

/* It's possible that the cursor had been unxplicitly moved
   forward. If so, we obtain the key at the current position
   and return that. This can happen in delete_key(). */
char *BlitzTree::next_key(int *key_len) {
  if (!cursor_moved) {
    if (!tcbdbcurnext(bt_cursor))
      return NULL;
  } else
    cursor_moved = false;

  return (char *)tcbdbcurkey(bt_cursor, key_len);
}

char *BlitzTree::prev_key(int *key_len) {
  if (!tcbdbcurprev(bt_cursor))
    return NULL;

  return (char *)tcbdbcurkey(bt_cursor, key_len);
}

char *BlitzTree::find_key(const char *key, const int klen, int *rv_len) {
  if (!tcbdbcurjump(bt_cursor, (void *)key, klen))
    return NULL;

  char *rv = (char *)tcbdbcurkey(bt_cursor, rv_len);

  /* A cursor based lookup on a B+Tree doesn't guarantee that the
     returned key is identical. This is because it would return the
     next logical key if one exists. Therefore we must check the
     lowerbound of the returned key for it's validity. */
  int cmp_len = (*rv_len < klen) ? *rv_len : klen;

  if (memcmp(rv, key, cmp_len) != 0) {
    free(rv);
    rv = NULL;
  }

  return rv;
}

/* Search and position the cursor to the last occurrence of the
   provided key. Returns true on success and otherwise false. */
bool BlitzTree::move_cursor(const char *key, const int klen,
                            const int search_mode) {
  char *fetched_key;
  int fetched_klen, cmp;

  fetched_key = this->find_key(key, klen, &fetched_klen);

  if (fetched_key == NULL)
    return false;

  free(fetched_key);

  /* If this index is unique, then there is no point in scanning
     for duplicates. Thus we return. */
  if (this->unique)
    return true;

  /* This mode means find the first possible key in the tree. */
  if (search_mode == drizzled::HA_READ_KEY_EXACT)
    return true;

  /* Keep traversing forward until we hit a different key or EOF. */
  do {
    fetched_key = this->next_key(&fetched_klen);

    /* No more nodes to visit. */
    if (fetched_key == NULL)
      break;

    cmp = blitz_keycmp_cb(fetched_key, fetched_klen, key, klen, this);

    if (cmp != 0) {
      free(fetched_key);
      /* Step back one key since that's the last position of
         the key that we're interested in. */
      tcbdbcurprev(bt_cursor);
      break;
    }

    free(fetched_key);
  } while(1);

  return true;
}

int BlitzTree::delete_key(const char *key, const int klen) {
  return (tcbdbout(btree, key, klen)) ? 0 : -1;
}

int BlitzTree::delete_cursor_pos(void) {
  if (!tcbdbcurout(bt_cursor))
    return -1;

  cursor_moved = true;
  return 0;
}

int BlitzTree::delete_all(void) {
  return (tcbdbvanish(btree)) ? 0 : -1;
}

uint64_t BlitzTree::records(void) {
  return tcbdbrnum(btree);
}
