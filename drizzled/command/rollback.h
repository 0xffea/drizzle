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

#ifndef DRIZZLED_COMMAND_ROLLBACK_H
#define DRIZZLED_COMMAND_ROLLBACK_H

#include <drizzled/command.h>

class Session;

namespace drizzled
{
namespace statement
{

class Rollback : public SqlCommand
{
public:
  Rollback(Session *in_session)
    :
      SqlCommand(in_session)
  {}

  bool execute();

private:
  static const enum enum_sql_command type= SQLCOM_ROLLBACK;
};

} /* end namespace statement */

} /* end namespace drizzled */

#endif /* DRIZZLED_COMMAND_ROLLBACK_H */
