/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Sun Microsystems
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

#include <plugin/data_engine/function.h>

using namespace std;
using namespace drizzled;

IndexPartsTool::IndexPartsTool() :
  IndexesTool("INDEX_PARTS")
{
  add_field("TABLE_SCHEMA");
  add_field("TABLE_NAME");
  add_field("INDEX_NAME");
  add_field("COLUMN_NAME");
  add_field("COLUMN_NUMBER", Tool::NUMBER);
  add_field("COMPARE_LENGTH", Tool::NUMBER);
  add_field("IS_ORDER_REVERSE", Tool::BOOLEAN);
}

IndexPartsTool::Generator::Generator(Field **arg) :
  IndexesTool::Generator(arg),
  index_part_iterator(0),
  is_index_part_primed(false)
{
}


bool IndexPartsTool::Generator::nextIndexPartsCore()
{
  if (is_index_part_primed)
  {
    index_part_iterator++;
  }
  else
  {
    if (not isIndexesPrimed())
      return false;

    index_part_iterator= 0;
    is_index_part_primed= true;
  }

  if (index_part_iterator >= getIndex().index_part_size())
    return false;

  index_part= getIndex().index_part(index_part_iterator);

  return true;
}

bool IndexPartsTool::Generator::nextIndexParts()
{
  while (not nextIndexPartsCore())
  {
    if (not nextIndex())
      return false;
    is_index_part_primed= false;
  }

  return true;
}

bool IndexPartsTool::Generator::populate()
{
  if (not nextIndexParts())
    return false;

  fill();

  return true;
}

void IndexPartsTool::Generator::fill()
{
  /* TABLE_SCHEMA */
  push(schema_name());

  /* TABLE_NAME */
  push(table_name());

  /* INDEX_NAME */
  push(getIndex().name());

  /* COLUMN_NAME */
  push(getTableProto().field(index_part.fieldnr()).name());

  /* COLUMN_NUMBER */
  push(index_part.fieldnr());

  /* COMPARE_LENGTH */
  push(index_part.compare_length());

  /* IS_ORDER_REVERSE */
  push(index_part.in_reverse_order());
}
