/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
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

#include "config.h"
#include <drizzled/message/binary_log.h>

#include <google/protobuf/io/coded_stream.h>

using namespace google;

bool
BinaryLog::Event::write(protobuf::io::CodedOutputStream* out) const
{
  // We frame each event in a length encoded in a special manner, and
  // end it with a CRC-32 checksum.

  // Write length and type
  unsigned char buf[LENGTH_ENCODE_MAX_BYTES + 1];
  unsigned char *end= length_encode(m_message->ByteSize(), buf);
  *end++= m_type;

  char cs[4] = { 0 };                           // !!! No checksum yet
#if GOOGLE_PROTOBUF_VERSION >= 2001000
  out->WriteRaw(buf, static_cast<int>(end - buf)); // Length + Type
  if (out->HadError()
    || !m_message->SerializeToCodedStream(out)) // Event body
    return false;
  out->WriteRaw(cs, sizeof(cs)); // Checksum
  if (out->HadError())
    return false;
#else
  if (!out->WriteRaw(buf, end - buf) ||         // Length + Type
      !m_message->SerializeToCodedStream(out) || // Event body
      !out->WriteRaw(cs, sizeof(cs)))           // Checksum
    return false;
#endif

  return true;
}


bool
BinaryLog::Event::read(protobuf::io::CodedInputStream *in)
{
  unsigned char buf[LENGTH_ENCODE_MAX_BYTES + 1];

  // Read length peek byte to figure out length
  if (!in->ReadRaw(buf, 1))
    return false;

  // Read in the rest of the length bytes plus the type
  size_t bytes= length_decode_bytes(*buf);
  if (! in->ReadRaw(buf + 1, static_cast<int>(bytes)))
    return false;

  size_t length;
  (void) length_decode(buf, &length);

  // Fetch type from read buffer
  m_type= static_cast<EventType>(buf[bytes]);

  // Create the right event based on the type code (is there something
  // better in the protobuf library?)
  protobuf::Message *message= NULL;
  switch (m_type) {
  case QUERY:
    message= new BinaryLog::Query;
    break;

  case COMMIT:
    message= new BinaryLog::Commit;
    break;

  case ROLLBACK:
    message= new BinaryLog::Rollback;
    break;

  case START:
    message= new BinaryLog::Start;
    break;

  case CHAIN:
    message= new BinaryLog::Chain;
    break;

  case COUNT:
  case UNDEF:
    break;
  }

  if (!message)
    return false;

  // Read the event body as length bytes. It is necessary to limit the
  // stream since otherwise ParseFromCodedStream reads all bytes of
  // the stream.
  protobuf::io::CodedInputStream::Limit limit= in->PushLimit(static_cast<int>(length));
  if (!message->ParseFromCodedStream(in))
    return false;
  in->PopLimit(limit);
  delete m_message;
  m_message= message;

  // Read checksum (none here yet)
  char checksum[4];
  if (!in->ReadRaw(checksum, sizeof(checksum)))
    return false;
  return true;
}

template <class EventClass>
void print_common(std::ostream& out, EventClass* event)
{
  out << "# Global Id: (" << event->header().server_id() << "," << event->header().trans_id() << ")\n";
}


void
BinaryLog::Event::print(std::ostream& out) const
{
  switch (m_type) {
  case QUERY:
  {
    Query *event= static_cast<Query*>(m_message);
    print_common(out, event);
    for (protobuf::RepeatedPtrField<Query::Variable>::const_iterator ii=
           event->variable().begin() ;
         ii != event->variable().end() ;
         ++ii)
    {
      out << "set @" << ii->name() << " = '" << ii->val() << "'\n";
    }
    out << event->query() << std::endl;
    break;
  }

  case COMMIT:
  {
    Commit *event= static_cast<Commit*>(m_message);
    print_common(out, event);
    // NYI !!!
    break;
  }

  case ROLLBACK:
  {
    Rollback *event= static_cast<Rollback*>(m_message);
    print_common(out, event);
    // NYI !!!
    break;
  }

  case START:
  {
    Start *event= static_cast<Start*>(m_message);
    print_common(out, event);
    // NYI !!!
    break;
  }

  case CHAIN:
  {
    Chain *event= static_cast<Chain*>(m_message);
    print_common(out, event);
    // NYI !!!
    break;
  }

  default:
    break;                                      /* Nothing */
  }
}

