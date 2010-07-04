/*
 *  Copyright (C) 2010 PrimeBase Technologies GmbH, Germany
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
 *
 * Barry Leslie
 *
 * 2010-05-12
 */

/**
 * @details
 *
 * This plugin is an example events plugin that just prints some info for
 * the events that it is tracking. 
 *  
set global hello_events_enable = ON;
set global hello_events_watch_databases = "x";   
set global hello_events_watch_tables = "x,y";

 */

#include "config.h"
#include <string>
#include <cstdio>

#include "drizzled/session.h"
#include "hello_events.h"

using namespace drizzled;
using namespace plugin;
using namespace std;

#define PLUGIN_NAME "hello_events1"

static bool sysvar_hello_events_enabled= true;
static HelloEvents *hello_events= NULL;
static char *sysvar_table_list= NULL;
static char *sysvar_db_list= NULL;

/*
 * Event observer positions are used to set the order in which
 * event observers are called in the case that more than one
 * plugin is interested in the same event. You should only specify
 * the order if it really matters because if more than one plugin 
 * request the same calling position only the first one gets it and
 * the others will not be registered for the event. For this reason
 * your plugin should always provide a way to reposition the event
 * observer to resolve such conflicts.
 *
 * If position matters you will always initialy ask for the first position (1)
 * or the last position (-1) in the calling order, for example it makes no sence 
 * to initially ask to be called in position 13.
 */
static int32_t sysvar_before_write_position= 1;      // Call this event observer first.
static int32_t sysvar_before_update_position= 1;
static int32_t sysvar_post_drop_db_position= -1;  // I want my event observer to be called last. No reason, I just do!


//==================================
// My table event observers: 
static bool observeBeforeInsertRecord(BeforeInsertRecordEventData &data)
{

  fprintf(stderr, PLUGIN_NAME" EVENT observeBeforeInsertRecord(%s)\n", data.table.getTableName());
  return false;
}

//---
static void observeAfterInsertRecord(AfterInsertRecordEventData &data)
{
  fprintf(stderr, PLUGIN_NAME" EVENT observeAfterInsertRecord(%s) err = %d\n", data.table.getTableName(), data.err);
}

//---
static bool observeBeforeDeleteRecord(BeforeDeleteRecordEventData &data)
{
  fprintf(stderr, PLUGIN_NAME" EVENT observeBeforeDeleteRecord(%s)\n", data.table.getTableName());
  return false;
}

//---
static void observeAfterDeleteRecord(AfterDeleteRecordEventData &data)
{
  fprintf(stderr, PLUGIN_NAME" EVENT observeAfterDeleteRecord(%s) err = %d\n", data.table.getTableName(), data.err);
}

//---
static bool observeBeforeUpdateRecord(BeforeUpdateRecordEventData &data)
{
  fprintf(stderr, PLUGIN_NAME" EVENT observeBeforeUpdateRecord(%s)\n", data.table.getTableName());
  return false;
}

//---
static void observeAfterUpdateRecord(AfterUpdateRecordEventData &data)
{
  fprintf(stderr, PLUGIN_NAME" EVENT observeAfterUpdateRecord(%s) err = %d\n", data.table.getTableName(), data.err);
}

//==================================
// My schema event observers: 
static void observeAfterDropTable(AfterDropTableEventData &data)
{
  fprintf(stderr, PLUGIN_NAME" EVENT observeAfterDropTable(%s) err = %d\n", data.table.getTableName().c_str(), data.err);
}

//---
static void observeAfterRenameTable(AfterRenameTableEventData &data)
{
  fprintf(stderr, PLUGIN_NAME" EVENT observeAfterRenameTable(%s, %s) err = %d\n", data.from.getTableName().c_str(), data.to.getTableName().c_str(), data.err);
}

//---
static void observeAfterCreateDatabase(AfterCreateDatabaseEventData &data)
{
  fprintf(stderr, PLUGIN_NAME" EVENT observeAfterCreateDatabase(%s) err = %d\n", data.db.c_str(), data.err);
}

//---
static void observeAfterDropDatabase(AfterDropDatabaseEventData &data)
{
  fprintf(stderr, PLUGIN_NAME" EVENT observeAfterDropDatabase(%s) err = %d\n", data.db.c_str(), data.err);
}

//==================================
/* This is where I register which table events my pluggin is interested in.*/
void HelloEvents::registerTableEventsDo(TableShare &table_share, EventObserverList &observers)
{
  if ((is_enabled == false) 
    || !isTableInteresting(table_share.getTableName())
    || !isDatabaseInteresting(table_share.getSchemaName()))
    return;
    
  registerEvent(observers, BEFORE_INSERT_RECORD, sysvar_before_write_position); // I want to be called first if passible
  registerEvent(observers, AFTER_INSERT_RECORD);
  registerEvent(observers, BEFORE_UPDATE_RECORD, sysvar_before_update_position);
  registerEvent(observers, AFTER_UPDATE_RECORD);
  registerEvent(observers, BEFORE_DELETE_RECORD);
  registerEvent(observers, AFTER_DELETE_RECORD);
}

//==================================
/* This is where I register which schema events my pluggin is interested in.*/
void HelloEvents::registerSchemaEventsDo(const std::string &db, EventObserverList &observers)
{
  if ((is_enabled == false) 
    || !isDatabaseInteresting(db))
    return;
    
  registerEvent(observers, AFTER_DROP_TABLE);
  registerEvent(observers, AFTER_RENAME_TABLE);
}

//==================================
/* This is where I register which session events my pluggin is interested in.*/
void HelloEvents::registerSessionEventsDo(Session &session, EventObserverList &observers)
{
  if ((is_enabled == false) 
    || !isSessionInteresting(session))
    return;
    
  registerEvent(observers, AFTER_CREATE_DATABASE);
  registerEvent(observers, AFTER_DROP_DATABASE, sysvar_post_drop_db_position);
}


//==================================
/* The event observer.*/
bool HelloEvents::observeEventDo(EventData &data)
{
  bool result= false;
  
  switch (data.event) {
  case AFTER_DROP_TABLE:
    observeAfterDropTable((AfterDropTableEventData &)data);
    break;
    
  case AFTER_RENAME_TABLE:
    observeAfterRenameTable((AfterRenameTableEventData &)data);
    break;
    
  case BEFORE_INSERT_RECORD:
     result = observeBeforeInsertRecord((BeforeInsertRecordEventData &)data);
    break;
    
  case AFTER_INSERT_RECORD:
    observeAfterInsertRecord((AfterInsertRecordEventData &)data);
    break;     
       
  case BEFORE_UPDATE_RECORD:
    result = observeBeforeUpdateRecord((BeforeUpdateRecordEventData &)data);
    break;
             
  case AFTER_UPDATE_RECORD:
     observeAfterUpdateRecord((AfterUpdateRecordEventData &)data);
    break;     
    
  case BEFORE_DELETE_RECORD:
    result = observeBeforeDeleteRecord((BeforeDeleteRecordEventData &)data);
    break;

  case AFTER_DELETE_RECORD:
    observeAfterDeleteRecord((AfterDeleteRecordEventData &)data);
    break;

  case AFTER_CREATE_DATABASE:
    observeAfterCreateDatabase((AfterCreateDatabaseEventData &)data);
    break;

  case AFTER_DROP_DATABASE:
    observeAfterDropDatabase((AfterDropDatabaseEventData &)data);
    break;

  default:
    fprintf(stderr, "HelloEvents: Unexpected event '%s'\n", EventObserver::eventName(data.event));
 
  }
  
  return false;
}

//==================================
// Some custom things for my plugin:


/* Plugin initialization and system variables */

static void enable(Session *,
                   drizzle_sys_var *,
                   void *var_ptr,
                   const void *save)
{
  if (hello_events)
  {
    if (*(bool *)save != false)
    {
      hello_events->enable();
      *(bool *) var_ptr= (bool) true;
    }
    else
    {
      hello_events->disable();
      *(bool *) var_ptr= (bool) false;
    }
  }
}


static void set_db_list(Session *,
                   drizzle_sys_var *, 
                   void *var_ptr,     
                   const void *save)
{
  if (hello_events)
  {
    hello_events->setDatabasesOfInterest(*(const char **) save);
    *(const char **) var_ptr= hello_events->getDatabasesOfInterest();
  }
}

static void set_table_list(Session *,
                   drizzle_sys_var *, 
                   void *var_ptr,     
                   const void *save)
{
  if (hello_events)
  {
    hello_events->setTablesOfInterest(*(const char **) save);
    *(const char **) var_ptr= hello_events->getTablesOfInterest();
  }
}


static int init(module::Context &context)
{
  hello_events= new HelloEvents(PLUGIN_NAME);

  context.add(hello_events);

  if (sysvar_hello_events_enabled)
  {
    hello_events->enable();
  }

  return 0;
}

static DRIZZLE_SYSVAR_STR(watch_databases,
                           sysvar_db_list,
                           PLUGIN_VAR_OPCMDARG,
                           N_("A comma delimited list of databases to watch"),
                           NULL, /* check func */
                           set_db_list, /* update func */
                           "" /* default */);

static DRIZZLE_SYSVAR_STR(watch_tables,
                           sysvar_table_list,
                           PLUGIN_VAR_OPCMDARG,
                           N_("A comma delimited list of tables to watch"),
                           NULL, /* check func */
                           set_table_list, /* update func */
                           "" /* default */);

static DRIZZLE_SYSVAR_BOOL(enable,
                           sysvar_hello_events_enabled,
                           PLUGIN_VAR_NOCMDARG,
                           N_("Enable Example Events Plugin"),
                           NULL, /* check func */
                           enable, /* update func */
                           false /* default */);

static DRIZZLE_SYSVAR_INT(before_write_position,
                           sysvar_before_write_position,
                           PLUGIN_VAR_NOCMDARG,
                           N_("Before write row event observer call position"),
                           NULL, /* check func */
                           NULL, /* update func */
                           1, /* default */
                           1, /* min */
                           INT32_MAX -1, /* max */
                           0 /* blk */);

static DRIZZLE_SYSVAR_INT(before_update_position,
                           sysvar_before_update_position,
                           PLUGIN_VAR_NOCMDARG,
                           N_("Before update row event observer call position"),
                           NULL, /* check func */
                           NULL, /* update func */
                           1, /* default */
                           1, /* min */
                           INT32_MAX -1, /* max */
                           0 /* blk */);

static DRIZZLE_SYSVAR_INT(post_drop_db_position,
                           sysvar_post_drop_db_position,
                           PLUGIN_VAR_NOCMDARG,
                           N_("After drop database event observer call position"),
                           NULL, /* check func */
                           NULL, /* update func */
                           -1, /* default */
                           INT32_MIN +1, /* min */
                           -1, /* max */
                           0 /* blk */);

static drizzle_sys_var* system_var[]= {
  DRIZZLE_SYSVAR(watch_databases),
  DRIZZLE_SYSVAR(watch_tables),
  DRIZZLE_SYSVAR(enable),
  DRIZZLE_SYSVAR(before_write_position),
  DRIZZLE_SYSVAR(before_update_position),
  DRIZZLE_SYSVAR(post_drop_db_position),
  NULL
};

DRIZZLE_DECLARE_PLUGIN
{
  DRIZZLE_VERSION_ID,
  PLUGIN_NAME,
  "0.1",
  "Barry Leslie",
  N_("An example events Plugin"),
  PLUGIN_LICENSE_BSD,
  init,   /* Plugin Init      */
  system_var, /* system variables */
  NULL    /* config options   */
}
DRIZZLE_DECLARE_PLUGIN_END;
