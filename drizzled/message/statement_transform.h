/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
 *
 *  Authors:
 *
 *    Jay Pipes <joinfu@sun.com>
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

/**
 * @file
 *
 * Declarations of various routines that can be used to convert
 * Transaction messages to other formats, including SQL statements.
 */

#ifndef DRIZZLED_MESSAGE_STATEMENT_TRANSFORM_H
#define DRIZZLED_MESSAGE_STATEMENT_TRANSFORM_H

#include <drizzled/message/table.pb.h>
#include <string>
#include <vector>

namespace drizzled
{
namespace message
{
/* some forward declarations */
class Statement;
class InsertHeader;
class InsertData;
class InsertRecord;
class UpdateHeader;
class UpdateData;
class UpdateRecord;
class DeleteHeader;
class DeleteData;
class DeleteRecord;
class TruncateTableStatement;
class SetVariableStatement;

/** A Variation of SQL to be output during transformation */
enum TransformSqlVariant
{
  ANSI,
  MYSQL_4,
  MYSQL_5,
  DRIZZLE
};

/** Error codes which can happen during tranformations */
enum TransformSqlError
{
  NONE= 0,
  MISSING_HEADER= 1, /* A data segment without a header segment was found */
  MISSING_DATA= 2 /* A header segment without a data segment was found */
};

/**
 * This function looks at the Statement
 * message and appends one or more correctly-formatted SQL
 * strings to the supplied vector of strings.
 *
 * @param Statement message to transform
 * @param Vector of strings to append SQL statements to
 * @param Variation of SQL to generate
 *
 * @retval
 *  NONE if successful transformation
 * @retval
 *  Error code (see enum TransformSqlError definition) if failure
 */
enum TransformSqlError
transformStatementToSql(const Statement &source,
                        std::vector<std::string> &sql_strings,
                        enum TransformSqlVariant sql_variant= DRIZZLE,
                        bool already_in_transaction= false);

/**
 * This function looks at a supplied InsertHeader
 * and InsertData message and constructs a correctly-formatted SQL
 * statement to the supplied destination string.
 *
 * @note
 *
 * This function is used when you want to construct a <strong>
 * single SQL statement</strong> from an entire InsertHeader and
 * InsertData message.  If there are many records in the InsertData
 * message, the SQL statement will be a multi-value INSERT statement.
 *
 * @param InsertHeader message to transform
 * @param InsertData message to transform
 * @param Destination string to append SQL to
 * @param Variation of SQL to generate
 *
 * @retval
 *  NONE if successful transformation
 * @retval
 *  Error code (see enum TransformSqlError definition) if failure
 */
enum TransformSqlError
transformInsertStatementToSql(const InsertHeader &header,
                              const InsertData &data,
                              std::string *destination,
                              enum TransformSqlVariant sql_variant= DRIZZLE);

/**
 * This function looks at a supplied InsertHeader
 * and a single InsertRecord message and constructs a correctly-formatted
 * SQL statement to the supplied destination string.
 *
 * @param InsertHeader message to transform
 * @param InsertRecord message to transform
 * @param Destination string to append SQL to
 * @param Variation of SQL to generate
 *
 * @retval
 *  NONE if successful transformation
 * @retval
 *  Error code (see enum TransformSqlError definition) if failure
 */
enum TransformSqlError
transformInsertRecordToSql(const InsertHeader &header,
                           const InsertRecord &record,
                           std::string *destination,
                           enum TransformSqlVariant sql_variant= DRIZZLE);

/**
 * Helper function to construct the header portion of an INSERT
 * SQL statement from an InsertHeader message.
 *
 * @param InsertHeader message to transform
 * @param Destination string to append SQL to
 * @param Variation of SQL to generate
 *
 * @retval
 *  NONE if successful transformation
 * @retval
 *  Error code (see enum TransformSqlError definition) if failure
 */
enum TransformSqlError
transformInsertHeaderToSql(const InsertHeader &header,
                           std::string *destination,
                           enum TransformSqlVariant sql_variant= DRIZZLE);

/**
 * Helper function to construct the header portion of an UPDATE
 * SQL statement from an UpdateHeader message.
 *
 * @param UpdateHeader message to transform
 * @param Destination string to append SQL to
 * @param Variation of SQL to generate
 *
 * @retval
 *  NONE if successful transformation
 * @retval
 *  Error code (see enum TransformSqlError definition) if failure
 */
enum TransformSqlError
transformUpdateHeaderToSql(const UpdateHeader &header,
                           std::string *destination,
                           enum TransformSqlVariant sql_variant= DRIZZLE);

/**
 * This function looks at a supplied UpdateHeader
 * and a single UpdateRecord message and constructs a correctly-formatted
 * SQL statement to the supplied destination string.
 *
 * @param UpdateHeader message to transform
 * @param UpdateRecord message to transform
 * @param Destination string to append SQL to
 * @param Variation of SQL to generate
 *
 * @retval
 *  NONE if successful transformation
 * @retval
 *  Error code (see enum TransformSqlError definition) if failure
 */
enum TransformSqlError
transformUpdateRecordToSql(const UpdateHeader &header,
                           const UpdateRecord &record,
                           std::string *destination,
                           enum TransformSqlVariant sql_variant= DRIZZLE);

/**
 * This function looks at a supplied DeleteHeader
 * and DeleteData message and constructs a correctly-formatted SQL
 * statement to the supplied destination string.
 *
 * @note
 *
 * This function constructs a <strong>single SQL statement</strong>
 * for all keys in the DeleteData message.
 *
 * @param DeleteHeader message to transform
 * @param DeleteData message to transform
 * @param Destination string to append SQL to
 * @param Variation of SQL to generate
 *
 * @retval
 *  NONE if successful transformation
 * @retval
 *  Error code (see enum TransformSqlError definition) if failure
 */
enum TransformSqlError
transformDeleteStatementToSql(const DeleteHeader &header,
                              const DeleteData &data,
                              std::string *destination,
                              enum TransformSqlVariant sql_variant= DRIZZLE);

/**
 * This function looks at a supplied DeleteHeader
 * and a single DeleteRecord message and constructs a correctly-formatted
 * SQL statement to the supplied destination string.
 *
 * @param DeleteHeader message to transform
 * @param DeleteRecord message to transform
 * @param Destination string to append SQL to
 * @param Variation of SQL to generate
 *
 * @retval
 *  NONE if successful transformation
 * @retval
 *  Error code (see enum TransformSqlError definition) if failure
 */
enum TransformSqlError
transformDeleteRecordToSql(const DeleteHeader &header,
                           const DeleteRecord &record,
                           std::string *destination,
                           enum TransformSqlVariant sql_variant= DRIZZLE);

/**
 * Helper function to construct the header portion of a DELETE
 * SQL statement from an DeleteHeader message.
 *
 * @param DeleteHeader message to transform
 * @param Destination string to append SQL to
 * @param Variation of SQL to generate
 *
 * @retval
 *  NONE if successful transformation
 * @retval
 *  Error code (see enum TransformSqlError definition) if failure
 */
enum TransformSqlError
transformDeleteHeaderToSql(const DeleteHeader &header,
                           std::string *destination,
                           enum TransformSqlVariant sql_variant= DRIZZLE);

/**
 * This function looks at a supplied TruncateTableStatement
 * and constructs a correctly-formatted SQL
 * statement to the supplied destination string.
 *
 * @param TruncateTableStatement message to transform
 * @param Destination string to append SQL to
 * @param Variation of SQL to generate
 *
 * @retval
 *  NONE if successful transformation
 * @retval
 *  Error code (see enum TransformSqlError definition) if failure
 */
enum TransformSqlError
transformTruncateTableStatementToSql(const TruncateTableStatement &statement,
                                     std::string *destination,
                                     enum TransformSqlVariant sql_variant= DRIZZLE);

/**
 * This function looks at a supplied SetVariableStatement
 * and constructs a correctly-formatted SQL
 * statement to the supplied destination string.
 *
 * @param SetVariableStatement message to transform
 * @param Destination string to append SQL to
 * @param Variation of SQL to generate
 *
 * @retval
 *  NONE if successful transformation
 * @retval
 *  Error code (see enum TransformSqlError definition) if failure
 */
enum TransformSqlError
transformSetVariableStatementToSql(const SetVariableStatement &statement,
                                   std::string *destination,
                                   enum TransformSqlVariant sql_variant= DRIZZLE);


/**
 * Returns true if the supplied message::Table::Field::FieldType
 * should have its values quoted when modifying values.
 *
 * @param[in] type of field
 */
bool shouldQuoteFieldValue(Table::Field::FieldType in_type);

} /* end namespace drizzled::message */
} /* end namespace drizzled */

#endif /* DRIZZLED_MESSAGE_STATEMENT_TRANSFORM_H */
