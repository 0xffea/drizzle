/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Definitions required for Authorization plugin
 *
 *  Copyright (C) 2010 Monty Taylor
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

#ifndef DRIZZLED_PLUGIN_AUTHORIZATION_H
#define DRIZZLED_PLUGIN_AUTHORIZATION_H

#include "drizzled/plugin.h"
#include "drizzled/plugin/plugin.h"
#include "drizzled/security_context.h"
#include "drizzled/table_identifier.h"

#include <string>
#include <set>

namespace drizzled
{

namespace plugin
{

class Authorization : public Plugin
{
  Authorization();
  Authorization(const Authorization &);
  Authorization& operator=(const Authorization &);
public:
  explicit Authorization(std::string name_arg)
    : Plugin(name_arg, "Authorization")
  {}
  virtual ~Authorization() {}

  /**
   * Should we restrict the current user's access to this schema?
   *
   * @param Current security context
   * @param Database to check against
   *
   * @returns true if the user cannot access the schema
   */
  virtual bool restrictSchema(const SecurityContext &user_ctx,
                              SchemaIdentifier &schema)= 0;

  /**
   * Should we restrict the current user's access to this table?
   *
   * @param Current security context
   * @param Database to check against
   * @param Table to check against
   *
   * @returns true if the user cannot access the table
   */
  virtual bool restrictTable(const SecurityContext &user_ctx,
                             TableIdentifier &table);

  /**
   * Should we restrict the current user's access to see this process?
   *
   * @param Current security context
   * @param Database to check against
   * @param Table to check against
   *
   * @returns true if the user cannot see the process
   */
  virtual bool restrictProcess(const SecurityContext &user_ctx,
                               const SecurityContext &session_ctx);

  /** Server API method for checking schema authorization */
  static bool isAuthorized(const SecurityContext &user_ctx,
                           SchemaIdentifier &schema_identifier,
                           bool send_error= true);

  /** Server API method for checking table authorization */
  static bool isAuthorized(const SecurityContext &user_ctx,
                           TableIdentifier &table_identifier,
                           bool send_error= true);

  /** Server API method for checking process authorization */
  static bool isAuthorized(const SecurityContext &user_ctx,
                           const Session *session,
                           bool send_error= true);

  /**
   * Server API helper method for applying authorization tests
   * to a set of schema names (for use in the context of getSchemaNames
   */
  static void pruneSchemaNames(const SecurityContext &user_ctx,
                               SchemaIdentifierList &set_of_schemas);
  
  /**
   * Standard plugin system registration hooks
   */
  static bool addPlugin(plugin::Authorization *auth);
  static void removePlugin(plugin::Authorization *auth);

};

inline bool Authorization::restrictTable(const SecurityContext &user_ctx,
                                         TableIdentifier &table)
{
  return restrictSchema(user_ctx, table);
}

inline bool Authorization::restrictProcess(const SecurityContext &,
                                           const SecurityContext &)
{
  return false;
}

} /* namespace plugin */

} /* namespace drizzled */

#endif /* DRIZZLED_PLUGIN_AUTHORIZATION_H */
