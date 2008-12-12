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

#ifndef DRIZZLED_ITEM_BLOB_H
#define DRIZZLED_ITEM_BLOB_H

#include <drizzled/item/string.h>

class Item_blob :public Item_string
{
public:
  Item_blob(const char *name, uint32_t length) :
    Item_string(name, length, &my_charset_bin)
  { max_length= length; }
  enum Type type() const { return TYPE_HOLDER; }
  enum_field_types field_type() const { return DRIZZLE_TYPE_BLOB; }
};


#endif /* DRIZZLED_ITEM_BLOB_H */
