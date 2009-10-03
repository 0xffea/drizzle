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

#include <drizzled/global.h>
#include <drizzled/gettext.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string>
#include <fstream>
#include <unistd.h>
#include <drizzled/message/transaction.pb.h>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

/** 
 * @file Example script for writing transactions to a log file.
 */

using namespace std;
using namespace drizzled;
using namespace google;

static uint32_t server_id= 1;
static uint64_t transaction_id= 1;

static uint64_t getNanoTimestamp()
{
#ifdef HAVE_CLOCK_GETTIME
  struct timespec tp;
  clock_gettime(CLOCK_REALTIME, &tp);
  return (uint64_t) tp.tv_sec * 10000000
       + (uint64_t) tp.tv_nsec;
#else
  struct timeval tv;
  gettimeofday(&tv,NULL);
  return (uint64_t) tv.tv_sec * 10000000
       + (uint64_t) tv.tv_usec * 1000;
#endif
}

static void initTransactionContext(message::Transaction &transaction)
{
  message::TransactionContext *ctx= transaction.mutable_transaction_context();
  ctx->set_transaction_id(transaction_id++);
  ctx->set_start_timestamp(getNanoTimestamp());
  ctx->set_server_id(server_id);
}

static void finalizeTransactionContext(message::Transaction &transaction)
{
  message::TransactionContext *ctx= transaction.mutable_transaction_context();
  ctx->set_end_timestamp(getNanoTimestamp());
}

static void doCreateTable(message::Transaction &transaction)
{
  message::Statement *statement= transaction.add_statement();

  statement->set_type(message::Statement::RAW_SQL);
  statement->set_sql("CREATE TABLE t1 (a VARCHAR(32) NOT NULL, PRIMARY KEY a) ENGINE=InnoDB");
  statement->set_start_timestamp(getNanoTimestamp());
  statement->set_end_timestamp(getNanoTimestamp());
}

static void doSimpleInsert(message::Transaction &transaction)
{
  message::Statement *statement= transaction.add_statement();

  /* Do generic Statement setup */
  statement->set_type(message::Statement::INSERT);
  statement->set_sql("INSERT INTO t1 (a) VALUES (\"1\"), (\"2\")");
  statement->set_start_timestamp(getNanoTimestamp());

  /* Do INSERT-specific header and setup */
  message::InsertHeader *header= statement->mutable_insert_header();

  /* Add table and field metadata for the statement */
  message::TableMetadata *t_meta= header->mutable_table_metadata();
  t_meta->set_schema_name("test");
  t_meta->set_table_name("t1");

  message::FieldMetadata *f_meta= header->add_field_metadata();
  f_meta->set_name("a");
  f_meta->set_type(message::Table::Field::VARCHAR);

  /* Add new values... */
  message::InsertData *data= statement->mutable_insert_data();
  data->set_segment_id(1);
  data->set_end_segment(true);

  message::InsertRecord *record1= data->add_record();
  message::InsertRecord *record2= data->add_record();

  record1->add_value("1");
  record2->add_value("2");

  statement->set_end_timestamp(getNanoTimestamp());
}

static void doSimpleDelete(message::Transaction &transaction)
{
  message::Statement *statement= transaction.add_statement();

  /* Do generic Statement setup */
  statement->set_type(message::Statement::DELETE);
  statement->set_sql("DELETE FROM t1 WHERE a = \"1\"");
  statement->set_start_timestamp(getNanoTimestamp());

  /* Do DELETE-specific header and setup */
  message::DeleteHeader *header= statement->mutable_delete_header();

  /* Add table and field metadata for the statement */
  message::TableMetadata *t_meta= header->mutable_table_metadata();
  t_meta->set_schema_name("test");
  t_meta->set_table_name("t1");

  message::FieldMetadata *f_meta= header->add_key_field_metadata();
  f_meta->set_name("a");
  f_meta->set_type(message::Table::Field::VARCHAR);

  /* Add new values... */
  message::DeleteData *data= statement->mutable_delete_data();
  data->set_segment_id(1);
  data->set_end_segment(true);

  message::DeleteRecord *record1= data->add_record();

  record1->add_key_value("1");

  statement->set_end_timestamp(getNanoTimestamp());
}

static void doSimpleUpdate(message::Transaction &transaction)
{
  message::Statement *statement= transaction.add_statement();

  /* Do generic Statement setup */
  statement->set_type(message::Statement::UPDATE);
  statement->set_sql("UPDATE t1 SET a = \"5\" WHERE a = \"1\"");
  statement->set_start_timestamp(getNanoTimestamp());

  /* Do UPDATE-specific header and setup */
  message::UpdateHeader *header= statement->mutable_update_header();

  /* Add table and field metadata for the statement */
  message::TableMetadata *t_meta= header->mutable_table_metadata();
  t_meta->set_schema_name("test");
  t_meta->set_table_name("t1");

  message::FieldMetadata *kf_meta= header->add_key_field_metadata();
  kf_meta->set_name("a");
  kf_meta->set_type(message::Table::Field::VARCHAR);

  message::FieldMetadata *sf_meta= header->add_set_field_metadata();
  sf_meta->set_name("a");
  sf_meta->set_type(message::Table::Field::VARCHAR);

  header->add_set_value("5");

  /* Add new values... */
  message::UpdateData *data= statement->mutable_update_data();
  data->set_segment_id(1);
  data->set_end_segment(true);

  message::UpdateRecord *record1= data->add_record();

  record1->add_key_value("1");

  statement->set_end_timestamp(getNanoTimestamp());
}

static void writeTransaction(protobuf::io::CodedOutputStream *output, message::Transaction &transaction)
{
  std::string buffer("");
  finalizeTransactionContext(transaction);
  transaction.SerializeToString(&buffer);

  size_t length= buffer.length();

  output->WriteLittleEndian64(static_cast<uint64_t>(length));
  output->WriteString(buffer);
}

int main(int argc, char* argv[])
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  int file;

  if (argc != 2) 
  {
    fprintf(stderr, _("Usage: %s TRANSACTION_LOG\n"), argv[0]);
    return -1;
  }

  if ((file= open(argv[1], O_APPEND|O_CREAT|O_SYNC|O_WRONLY, S_IRWXU)) == -1)
  {
    fprintf(stderr, _("Cannot open file: %s\n"), argv[1]);
    return -1;
  }

  protobuf::io::ZeroCopyOutputStream *raw_output= new protobuf::io::FileOutputStream(file);
  protobuf::io::CodedOutputStream *coded_output= new protobuf::io::CodedOutputStream(raw_output);

  /* Write a series of statements which test each type of Statement */
  message::Transaction transaction;

  /* Simple CREATE TABLE statement as raw sql */
  initTransactionContext(transaction);
  doCreateTable(transaction);
  writeTransaction(coded_output, transaction);
  transaction.Clear();

  /* Simple INSERT statement */
  initTransactionContext(transaction);
  doSimpleInsert(transaction);
  writeTransaction(coded_output, transaction);
  transaction.Clear();

  /* Write a DELETE and an UPDATE in one transaction */
  initTransactionContext(transaction);
  doSimpleDelete(transaction);
  doSimpleUpdate(transaction);
  writeTransaction(coded_output, transaction);

  delete coded_output;
  delete raw_output;

  return 0;
}
