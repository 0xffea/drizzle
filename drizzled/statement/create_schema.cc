/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
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
#include <drizzled/show.h>
#include <drizzled/session.h>
#include <drizzled/statement/create_schema.h>
#include <drizzled/db.h>

#include <string>

using namespace std;

namespace drizzled
{

bool statement::CreateSchema::execute()
{
  if (not validateSchemaOptions())
    return true;

  if (not session->endActiveTransaction())
  {
    return true;
  }

  SchemaIdentifier schema_identifier(string(session->lex->name.str, session->lex->name.length));
  if (not check_db_name(schema_identifier))
  {
    my_error(ER_WRONG_DB_NAME, MYF(0), schema_identifier.getSQLPath().c_str());
    return false;
  }

  schema_message.set_name(session->lex->name.str);
  schema_message.mutable_engine()->set_name(std::string("filesystem")); // For the moment we have only one.
  if (not schema_message.has_collation())
  {
    schema_message.set_collation(default_charset_info->name);
  }

  bool res= mysql_create_db(session, schema_message, is_if_not_exists);
  return not res;
}

// We don't actually test anything at this point, we assume it is all bad.
bool statement::CreateSchema::validateSchemaOptions()
{
  size_t num_engine_options= schema_message.engine().options_size();
  bool rc= num_engine_options ? false : true;

  for (size_t y= 0; y < num_engine_options; ++y)
  {
    my_error(ER_UNKNOWN_SCHEMA_OPTION, MYF(0),
             schema_message.engine().options(y).name().c_str(),
             schema_message.engine().options(y).state().c_str());

    rc= false;
  }

  return rc;
}

} /* namespace drizzled */

