/* Copyright (C) 2006 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#include "rpl_utility.h"
#include "rpl_rli.h"

/*********************************************************************
 *                   table_def member definitions                    *
 *********************************************************************/

/*
  This function returns the field size in raw bytes based on the type
  and the encoded field data from the master's raw data.
*/
uint32 table_def::calc_field_size(uint col, uchar *master_data) const
{
  uint32 length= 0;

  switch (type(col)) {
  case MYSQL_TYPE_NEWDECIMAL:
    length= my_decimal_get_binary_size(m_field_metadata[col] >> 8, 
                                       m_field_metadata[col] & 0xff);
    break;
  case MYSQL_TYPE_FLOAT:
  case MYSQL_TYPE_DOUBLE:
    length= m_field_metadata[col];
    break;
  /*
    The cases for SET and ENUM are include for completeness, however
    both are mapped to type MYSQL_TYPE_STRING and their real types
    are encoded in the field metadata.
  */
  case MYSQL_TYPE_SET:
  case MYSQL_TYPE_ENUM:
  case MYSQL_TYPE_STRING:
  {
    uchar type= m_field_metadata[col] >> 8U;
    if ((type == MYSQL_TYPE_SET) || (type == MYSQL_TYPE_ENUM))
      length= m_field_metadata[col] & 0x00ff;
    else
    {
      /*
        We are reading the actual size from the master_data record
        because this field has the actual lengh stored in the first
        byte.
      */
      length= (uint) *master_data + 1;
      assert(length != 0);
    }
    break;
  }
  case MYSQL_TYPE_YEAR:
  case MYSQL_TYPE_TINY:
    length= 1;
    break;
  case MYSQL_TYPE_SHORT:
    length= 2;
    break;
  case MYSQL_TYPE_LONG:
    length= 4;
    break;
  case MYSQL_TYPE_LONGLONG:
    length= 8;
    break;
  case MYSQL_TYPE_NULL:
    length= 0;
    break;
  case MYSQL_TYPE_NEWDATE:
    length= 3;
    break;
  case MYSQL_TYPE_TIME:
    length= 3;
    break;
  case MYSQL_TYPE_TIMESTAMP:
    length= 4;
    break;
  case MYSQL_TYPE_DATETIME:
    length= 8;
    break;
  case MYSQL_TYPE_VARCHAR:
  {
    length= m_field_metadata[col] > 255 ? 2 : 1; // c&p of Field_varstring::data_length()
    assert(uint2korr(master_data) > 0);
    length+= length == 1 ? (uint32) *master_data : uint2korr(master_data);
    break;
  }
  case MYSQL_TYPE_BLOB:
  {
    length= uint4korr(master_data);
    length+= m_field_metadata[col];
    break;
  }
  default:
    length= ~(uint32) 0;
  }
  return length;
}

/*
  Is the definition compatible with a table?

*/
int
table_def::compatible_with(Relay_log_info const *rli_arg, TABLE *table)
  const
{
  /*
    We only check the initial columns for the tables.
  */
  uint const cols_to_check= min(table->s->fields, size());
  int error= 0;
  Relay_log_info const *rli= const_cast<Relay_log_info*>(rli_arg);

  TABLE_SHARE const *const tsh= table->s;

  for (uint col= 0 ; col < cols_to_check ; ++col)
  {
    if (table->field[col]->type() != type(col))
    {
      assert(col < size() && col < tsh->fields);
      assert(tsh->db.str && tsh->table_name.str);
      error= 1;
      char buf[256];
      snprintf(buf, sizeof(buf), "Column %d type mismatch - "
                "received type %d, %s.%s has type %d",
                col, type(col), tsh->db.str, tsh->table_name.str,
                table->field[col]->type());
      rli->report(ERROR_LEVEL, ER_BINLOG_ROW_WRONG_TABLE_DEF,
                  ER(ER_BINLOG_ROW_WRONG_TABLE_DEF), buf);
    }
    /*
      Check the slave's field size against that of the master.
    */
    if (!error && 
        !table->field[col]->compatible_field_size(field_metadata(col)))
    {
      error= 1;
      char buf[256];
      snprintf(buf, sizeof(buf), "Column %d size mismatch - "
               "master has size %d, %s.%s on slave has size %d."
               " Master's column size should be <= the slave's "
               "column size.", col,
               table->field[col]->pack_length_from_metadata(
                                    m_field_metadata[col]),
               tsh->db.str, tsh->table_name.str, 
               table->field[col]->row_pack_length());
      rli->report(ERROR_LEVEL, ER_BINLOG_ROW_WRONG_TABLE_DEF,
                  ER(ER_BINLOG_ROW_WRONG_TABLE_DEF), buf);
    }
  }

  return error;
}
