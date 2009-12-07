/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Toru Maesaka
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

BlitzLock::BlitzLock() : scanner_count(0), updater_count(0) {
  pthread_cond_init(&condition, NULL);
  pthread_mutex_init(&mutex, NULL);
}

BlitzLock::~BlitzLock() {
  pthread_cond_destroy(&condition);
  pthread_mutex_destroy(&mutex);
}

void BlitzLock::update_begin() {
  pthread_mutex_lock(&mutex);
  while (true) {
    if (scanner_count < 1) {
      updater_count++;
      pthread_mutex_unlock(&mutex);
      return;
    }
    pthread_cond_wait(&condition, &mutex);
  }
  pthread_mutex_unlock(&mutex);
}

void BlitzLock::update_end() {
  pthread_mutex_lock(&mutex);
  updater_count--;
  assert(updater_count >= 0);
  if (updater_count == 0)
    pthread_cond_broadcast(&condition);
  pthread_mutex_unlock(&mutex);
}

void BlitzLock::scan_begin() {
  pthread_mutex_lock(&mutex);
  while (true) {
    if (updater_count == 0) {
      scanner_count++;
      pthread_mutex_unlock(&mutex);
      return;
    }
    pthread_cond_wait(&condition, &mutex);
  }
  pthread_mutex_unlock(&mutex);
}

void BlitzLock::scan_end() {
  pthread_mutex_lock(&mutex);
  scanner_count--;
  assert(scanner_count >= 0);
  if (scanner_count == 0)
    pthread_cond_broadcast(&condition);
  pthread_mutex_unlock(&mutex);
}

void BlitzLock::scan_update_begin() {
  pthread_mutex_lock(&mutex);
  while (true) {
    if (scanner_count == 0 && updater_count == 0) {
      scanner_count++;
      updater_count++;
      pthread_mutex_unlock(&mutex);
      return;
    }
    pthread_cond_wait(&condition, &mutex);
  }
  pthread_mutex_unlock(&mutex);
}

void BlitzLock::scan_update_end() {
  pthread_mutex_lock(&mutex);
  scanner_count--;
  updater_count--;
  assert(scanner_count >= 0 && updater_count >= 0);

  /* All other threads are guaranteed to be
     waiting so broadcast regardless. */
  pthread_cond_broadcast(&condition);
  pthread_mutex_unlock(&mutex);
}

int ha_blitz::scan_lock() {
  if (sql_command_type == SQLCOM_UPDATE ||
      sql_command_type == SQLCOM_DELETE ||
      sql_command_type == SQLCOM_REPLACE) {
    share->blitz_lock.scan_update_begin();
  } else {
    share->blitz_lock.scan_begin();
  }
  thread_locked = true;
  return 0;
}

int ha_blitz::scan_unlock() {
  if (sql_command_type == SQLCOM_UPDATE ||
      sql_command_type == SQLCOM_DELETE ||
      sql_command_type == SQLCOM_REPLACE) {
    share->blitz_lock.scan_update_end();
  } else {
    share->blitz_lock.scan_end();
  }
  thread_locked = false;
  return 0;
}
