/* Copyright (C) 2006 MySQL AB

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

#include "config.h"
#include <plugin/multi_thread/multi_thread.h>
#include "drizzled/pthread_globals.h"
#include <boost/program_options.hpp>
#include <drizzled/module/option_map.h>
#include <drizzled/errmsg_print.h>

#include <boost/thread.hpp>
#include <boost/bind.hpp>

namespace po= boost::program_options;
using namespace std;
using namespace drizzled;

/* Configuration variables. */
static uint32_t max_threads;

/* Global's (TBR) */
static MultiThreadScheduler *scheduler= NULL;

namespace drizzled
{
  extern size_t my_thread_stack_size;
}

void MultiThreadScheduler::runSession(drizzled::Session *session)
{
  if (drizzled::internal::my_thread_init())
  {
    session->disconnect(drizzled::ER_OUT_OF_RESOURCES, true);
    session->status_var.aborted_connects++;
    killSessionNow(session);
  }
  boost::this_thread::at_thread_exit(&internal::my_thread_end);

  session->thread_stack= (char*) &session;
  session->run();
  killSessionNow(session);
}

void MultiThreadScheduler::setStackSize()
{
  pthread_attr_t attr;

  (void) pthread_attr_init(&attr);

  /* Get the thread stack size that the OS will use and make sure
    that we update our global variable. */
  int err= pthread_attr_getstacksize(&attr, &my_thread_stack_size);
  pthread_attr_destroy(&attr);

  if (err != 0)
  {
    errmsg_printf(ERRMSG_LVL_ERROR, _("Unable to get thread stack size\n"));
    my_thread_stack_size= 524288; // At the time of the writing of this code, this was OSX's
  }

  if (my_thread_stack_size == 0)
  {
    my_thread_stack_size= 524288; // At the time of the writing of this code, this was OSX's
  }
#ifdef __sun
  /*
   * Solaris will return zero for the stack size in a call to
   * pthread_attr_getstacksize() to indicate that the OS default stack
   * size is used. We need an actual value in my_thread_stack_size so that
   * check_stack_overrun() will work. The Solaris man page for the
   * pthread_attr_getstacksize() function says that 2M is used for 64-bit
   * processes. We'll explicitly set it here to make sure that is what
   * will be used.
   */
  if (my_thread_stack_size == 0)
  {
    my_thread_stack_size= 2 * 1024 * 1024;
  }
#endif
}

bool MultiThreadScheduler::addSession(Session *session)
{
  if (thread_count >= max_threads)
    return true;

  thread_count.increment();

  boost::thread new_thread(boost::bind(&MultiThreadScheduler::runSession, this, session));

  if (not new_thread.joinable())
  {
    thread_count.decrement();
    return true;
  }

  return false;
}


void MultiThreadScheduler::killSessionNow(Session *session)
{
  /* Locks LOCK_thread_count and deletes session */
  Session::unlink(session);
  thread_count.decrement();
}

MultiThreadScheduler::~MultiThreadScheduler()
{
  boost::mutex::scoped_lock scopedLock(LOCK_thread_count);
  while (thread_count)
  {
    COND_thread_count.wait(scopedLock);
  }
}

  
static int init(drizzled::module::Context &context)
{
  
  const module::option_map &vm= context.getOptions();
  if (vm.count("max-threads"))
  {
    if (max_threads > 4096 || max_threads < 1)
    {
      errmsg_printf(ERRMSG_LVL_ERROR, _("Invalid value for max-threads\n"));
      exit(-1);
    }
  }

  scheduler= new MultiThreadScheduler("multi_thread");
  context.add(scheduler);

  return 0;
}

static DRIZZLE_SYSVAR_UINT(max_threads, max_threads,
                           PLUGIN_VAR_RQCMDARG,
                           N_("Maximum number of user threads available."),
                           NULL, NULL, 2048, 1, 4096, 0);

static void init_options(drizzled::module::option_context &context)
{
  context("max-threads",
          po::value<uint32_t>(&max_threads)->default_value(2048),
          N_("Maximum number of user threads available."));
}

static drizzle_sys_var* sys_variables[]= {
  DRIZZLE_SYSVAR(max_threads),
  NULL
};

DRIZZLE_DECLARE_PLUGIN
{
  DRIZZLE_VERSION_ID,
  "multi_thread",
  "0.1",
  "Brian Aker",
  "One Thread Per Session Scheduler",
  PLUGIN_LICENSE_GPL,
  init, /* Plugin Init */
  sys_variables,   /* system variables */
  init_options    /* config options */
}
DRIZZLE_DECLARE_PLUGIN_END;
