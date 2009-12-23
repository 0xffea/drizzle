/* Copyright (C) 2009 Sun Microsystems

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#ifndef PLUGIN_SINGLE_THREAD_SINGLE_THREAD_H
#define PLUGIN_SINGLE_THREAD_SINGLE_THREAD_H

#include <drizzled/gettext.h>
#include <drizzled/error.h>
#include <drizzled/plugin/scheduler.h>
#include <drizzled/sql_parse.h>
#include <drizzled/session.h>
#include <string>
#include "mysys/my_sys.h"


/**
 * Simple scheduler that uses the main thread to handle the request. This
 * should only be used for debugging.
 */
class SingleThreadScheduler : public drizzled::plugin::Scheduler
{
public:
  SingleThreadScheduler(const char *name_arg) : 
    Scheduler(name_arg) {}

  /* When we enter this function, LOCK_thread_count is held! */
  virtual bool addSession(Session *session)
  {
    if (my_thread_init())
    {
      session->disconnect(ER_OUT_OF_RESOURCES, true);
      statistic_increment(aborted_connects, &LOCK_status);
      return true;
    }

    /*
      This is not the real thread start beginning, but there is not an easy
      way to find it.
    */
    session->thread_stack= (char *)&session;

    session->run();
    killSessionNow(session);
    return false;
  }

  virtual void killSessionNow(Session *session)
  {
    Session::unlink(session);
    my_thread_end();
  }
};

#endif /* PLUGIN_SINGLE_THREAD_SINGLE_THREAD_H */
