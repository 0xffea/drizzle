/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008-2009 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

/**
 * @file
 *
 * Defines the implementation of the default replicator.
 *
 * @see drizzled/plugin/command_replicator.h
 * @see drizzled/plugin/command_applier.h
 *
 * @details
 *
 * This is a very simple implementation.  All we do is pass along the 
 * event to the supplier.  This is meant as a skeleton replicator only.
 *
 * @todo
 *
 * Want a neat project?  Take this skeleton replicator and make a
 * simple filtered replicator which allows the user to filter out
 * events based on a schema or table name...
 */

#include <drizzled/server_includes.h>
#include "default_replicator.h"

#include <drizzled/gettext.h>
#include <drizzled/message/replication.pb.h>

#include <vector>
#include <string>

using namespace std;
using namespace drizzled;

static bool sysvar_default_replicator_enable= false;

bool DefaultReplicator::isActive()
{
  return sysvar_default_replicator_enable;
}

void DefaultReplicator::replicate(plugin::CommandApplier *in_applier, message::Command &to_replicate)
{
  /* 
   * We do absolutely nothing but call the applier's apply() method, passing
   * along the supplied Command.  Yep, told you it was simple...
   *
   * Perfectly fine to use const_cast<> below.  All that does is allow the replicator
   * to conform to the CommandApplier::apply() API call which dictates that the applier
   * shall never modify the supplied Command message argument.  Since the replicator 
   * itself *can* modify the supplied Command message, we use const_cast<> here to
   * set the message to a readonly state that the compiler will like.
   */
  in_applier->apply(const_cast<const message::Command&>(to_replicate));
}

static DefaultReplicator *default_replicator= NULL; /* The singleton replicator */

static int init(drizzled::plugin::Registry &registry)
{
  default_replicator= new DefaultReplicator("default_replicator");
  registry.add(default_replicator);
  return 0;
}

static int deinit(drizzled::plugin::Registry &registry)
{
  if (default_replicator)
  {
    registry.remove(default_replicator);
    delete default_replicator;
  }
  return 0;
}

static DRIZZLE_SYSVAR_BOOL(
  enable,
  sysvar_default_replicator_enable,
  PLUGIN_VAR_NOCMDARG,
  N_("Enable default replicator"),
  NULL, /* check func */
  NULL, /* update func */
  false /* default */);

static struct st_mysql_sys_var* default_replicator_system_variables[]= {
  DRIZZLE_SYSVAR(enable),
  NULL
};

drizzle_declare_plugin(default_replicator)
{
  "default_replicator",
  "0.1",
  "Jay Pipes",
  N_("Default Replicator"),
  PLUGIN_LICENSE_GPL,
  init, /* Plugin Init */
  deinit, /* Plugin Deinit */
  NULL, /* status variables */
  default_replicator_system_variables, /* system variables */
  NULL    /* config options */
}
drizzle_declare_plugin_end;
