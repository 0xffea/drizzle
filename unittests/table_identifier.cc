/* -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
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

#include <gtest/gtest.h>

#include <drizzled/table_identifier.h>

using namespace drizzled;

TEST(table_identifier_test_standard, Create)
{
  TableIdentifier identifier("test", "a");
  EXPECT_EQ("test/a", identifier.getPath());
  EXPECT_EQ("test.a", identifier.getSQLPath());
}

TEST(table_identifier_test_temporary, Create)
{
  TableIdentifier identifier("test", "a", message::Table::TEMPORARY);
  EXPECT_EQ("/#sql", identifier.getPath().substr(0, 5));
  EXPECT_EQ("test.#a", identifier.getSQLPath());
}

TEST(table_identifier_test_internal, Create)
{
  TableIdentifier identifier("test", "a", message::Table::TEMPORARY);
  EXPECT_EQ("/#sql", identifier.getPath().substr(0, 5));
  EXPECT_EQ("test.#a", identifier.getSQLPath());
}

TEST(table_identifier_test_build_tmptable_filename, Static)
{
  std::vector<char> pathname;

  TableIdentifier::build_tmptable_filename(pathname);

  EXPECT_GT(pathname.size(), 0);
  EXPECT_GT(strlen(&pathname[0]), 0);
}

TEST(table_identifier_test_key, Key)
{
  TableIdentifier identifier("test", "a");

  const TableIdentifier::Key key= identifier.getKey();

  EXPECT_EQ(key.size(), 7);
  EXPECT_EQ(key[0], 't');
  EXPECT_EQ(key[1], 'e');
  EXPECT_EQ(key[2], 's');
  EXPECT_EQ(key[3], 't');
  EXPECT_EQ(key[4], 0);
  EXPECT_EQ(key[5], 'a');
  EXPECT_EQ(key[6], 0);
}

TEST(table_identifier_test_key, KeyCompare)
{
  TableIdentifier identifier("test", "a");
  TableIdentifier identifier2("test", "a");

  EXPECT_EQ((identifier.getKey() == identifier.getKey()), true);
}
