/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Brian Aker
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

#include "config.h"

#include "plugin/table_cache_dictionary/dictionary.h"
#include "drizzled/pthread_globals.h"
#include "drizzled/my_hash.h"

using namespace drizzled;
using namespace std;

table_cache_dictionary::TableCache::TableCache() :
  plugin::TableFunction("DATA_DICTIONARY", "TABLE_CACHE")
{
  add_field("SESSION_ID", plugin::TableFunction::NUMBER);
  add_field("TABLE_SCHEMA");
  add_field("TABLE_NAME");
  add_field("VERSION", plugin::TableFunction::NUMBER);
  add_field("IS_NAME_LOCKED", plugin::TableFunction::BOOLEAN);
  add_field("ROWS", plugin::TableFunction::NUMBER);
  add_field("AVG_ROW_LENGTH", plugin::TableFunction::NUMBER);
  add_field("TABLE_SIZE", plugin::TableFunction::NUMBER);
  add_field("AUTO_INCREMENT", plugin::TableFunction::NUMBER);
}

table_cache_dictionary::TableCache::Generator::Generator(drizzled::Field **arg) :
  drizzled::plugin::TableFunction::Generator(arg),
  is_primed(false)
{
  pthread_mutex_lock(&LOCK_open); /* Optionally lock for remove tables from open_cahe if not in use */

  drizzled::HASH *open_cache=
    get_open_cache();

  for (uint32_t idx= 0; idx < open_cache->records; idx++ )
  {
    table= (Table*) hash_element(open_cache, idx);
    table_list.push_back(table);
  }
  std::sort(table_list.begin(), table_list.end(), Table::compare);
}

table_cache_dictionary::TableCache::Generator::~Generator()
{
  pthread_mutex_unlock(&LOCK_open); /* Optionally lock for remove tables from open_cahe if not in use */
}

bool table_cache_dictionary::TableCache::Generator::nextCore()
{
  if (is_primed)
  {
    table_list_iterator++;
  }
  else
  {
    is_primed= true;
    table_list_iterator= table_list.begin();
  }

  if (table_list_iterator == table_list.end())
    return false;

  table= *table_list_iterator;

  return true;
}

bool table_cache_dictionary::TableCache::Generator::next()
{
  while (not nextCore())
  {
    if (table_list_iterator != table_list.end())
      continue;

    return false;
  }

  return true;
}

bool table_cache_dictionary::TableCache::Generator::populate()
{
  if (not next())
    return false;
  
  fill();

  return true;
}

void table_cache_dictionary::TableCache::Generator::fill()
{
  /**
    For test cases use:
    --replace_column 1 # 4 # 5 #  6 # 8 # 9 #
  */

  /* SESSION_ID 1 */
  if (table->getSession())
    push(table->getSession()->getSessionId());
  else
    push(static_cast<int64_t>(0));

  /* TABLE_SCHEMA 2 */
  string arg;
  push(table->getShare()->getSchemaName(arg));

  /* TABLE_NAME  3 */
  push(table->getShare()->getTableName(arg));

  /* VERSION 4 */
  push(static_cast<int64_t>(table->getShare()->getVersion()));

  /* IS_NAME_LOCKED 5 */
  push(table->getShare()->isNameLock());

  /* ROWS 6 */
  push(static_cast<uint64_t>(table->getCursor().records()));

  /* AVG_ROW_LENGTH 7 */
  push(table->getCursor().rowSize());

  /* TABLE_SIZE 8 */
  push(table->getCursor().tableSize());

  /* AUTO_INCREMENT 9 */
  push(table->getCursor().getNextInsertId());
}
