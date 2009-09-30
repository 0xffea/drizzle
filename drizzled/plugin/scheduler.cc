/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
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

#include "drizzled/server_includes.h"
#include "drizzled/plugin/scheduler.h"
#include "drizzled/plugin/registry.h"

#include "drizzled/gettext.h"

using namespace std;

namespace drizzled
{

plugin::SchedulerFactory *scheduler_factory= NULL;
Registry<plugin::SchedulerFactory *> all_schedulers;

void plugin::SchedulerFactory::add(plugin::SchedulerFactory *factory)
{
  if (all_schedulers.count(factory->getName()) != 0)
  {
    errmsg_printf(ERRMSG_LVL_ERROR,
                  _("Attempted to register a scheduler %s, but a scheduler "
                    "has already been registered with that name.\n"),
                    factory->getName().c_str());
    return;
  }
  all_schedulers.add(factory);
}


void plugin::SchedulerFactory::remove(plugin::SchedulerFactory *factory)
{
  scheduler_factory= NULL;
  all_schedulers.remove(factory);
}


bool plugin::SchedulerFactory::setFactory(const string& name)
{
   
  plugin::SchedulerFactory *factory= all_schedulers.find(name);
  if (factory == NULL)
  {
    errmsg_printf(ERRMSG_LVL_WARN,
                  _("Attempted to configure %s as the scheduler, which did "
                    "not exist.\n"), name.c_str());
    return true;
  }
  scheduler_factory= factory;

  return false;
}

plugin::Scheduler *plugin::SchedulerFactory::getScheduler()
{
  assert(scheduler_factory != NULL);
  plugin::Scheduler *sched= (*scheduler_factory)();
  if (sched == NULL)
  {
    errmsg_printf(ERRMSG_LVL_ERROR, _("Scheduler initialization failed.\n"));
    exit(1);
  }
  return sched;
}

} /* namespace drizzled */
