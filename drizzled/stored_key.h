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

#ifndef DRIZZLED_STORED_KEY_H
#define DRIZZLED_STORED_KEY_H

namespace drizzled
{

/** class to store an field/item as a key struct */
class StoredKey :public memory::SqlAlloc
{
public:
  bool null_key; /**< If true, the value of the key has a null part */
  enum store_key_result 
  { 
    STORE_KEY_OK,
    STORE_KEY_FATAL, 
    STORE_KEY_CONV 
  };
protected:
  Field *to_field;				// Store data here
  unsigned char *null_ptr;
  unsigned char err;
  virtual enum store_key_result copy_inner()=0;
public:
  StoredKey(Session *session,
            Field *field_arg, 
            unsigned char *ptr,
            unsigned char *null, 
            uint32_t length)
    :
      null_key(0), 
      null_ptr(null), 
      err(0)
  {
    if (field_arg->type() == DRIZZLE_TYPE_BLOB)
    {
      /*
        Key segments are always packed with a 2 byte length prefix.
        See mi_rkey for details.
      */
      to_field= new Field_varstring(ptr,
                                    length,
                                    2,
                                    null,
                                    1,
                                    field_arg->field_name,
                                    field_arg->table->getMutableShare(),
                                    field_arg->charset());
      to_field->init(field_arg->table);
    }
    else
      to_field= field_arg->new_key_field(session->mem_root, field_arg->table,
                                        ptr, null, 1);

    to_field->setWriteSet();
  }
  virtual ~StoredKey() {}			/** Not actually needed */
  virtual const char *name() const=0;

  /**
    @brief sets ignore truncation warnings mode and calls the real copy method

    @details this function makes sure truncation warnings when preparing the
    key buffers don't end up as errors (because of an enclosing INSERT/UPDATE).
  */
  enum store_key_result copy()
  {
    enum store_key_result result;
    Session *session= to_field->table->in_use;
    enum_check_fields saved_count_cuted_fields= session->count_cuted_fields;
    session->count_cuted_fields= CHECK_FIELD_IGNORE;
    result= copy_inner();
    session->count_cuted_fields= saved_count_cuted_fields;

    return result;
  }
};

class store_key_field: public StoredKey
{
  CopyField copy_field;
  const char *field_name;
public:
  store_key_field(Session *session, Field *to_field_arg, unsigned char *ptr,
                  unsigned char *null_ptr_arg,
		  uint32_t length, Field *from_field, const char *name_arg)
    :StoredKey(session, to_field_arg,ptr,
	       null_ptr_arg ? null_ptr_arg : from_field->maybe_null() ? &err
	       : (unsigned char*) 0, length), field_name(name_arg)
  {
    if (to_field)
    {
      copy_field.set(to_field,from_field,0);
    }
  }
  const char *name() const { return field_name; }

protected:
  enum store_key_result copy_inner()
  {
    copy_field.do_copy(&copy_field);
    null_key= to_field->is_null();
    return err != 0 ? STORE_KEY_FATAL : STORE_KEY_OK;
  }
};

class store_key_item :public StoredKey
{
 protected:
  Item *item;
public:
  store_key_item(Session *session, Field *to_field_arg, unsigned char *ptr,
                 unsigned char *null_ptr_arg, uint32_t length, Item *item_arg)
    :StoredKey(session, to_field_arg, ptr,
	       null_ptr_arg ? null_ptr_arg : item_arg->maybe_null ?
	       &err : (unsigned char*) 0, length), item(item_arg)
  {}
  const char *name() const { return "func"; }

 protected:
  enum store_key_result copy_inner()
  {
    int res= item->save_in_field(to_field, 1);
    null_key= to_field->is_null() || item->null_value;
    return (err != 0 || res > 2 ? STORE_KEY_FATAL : (store_key_result) res);
  }
};

class store_key_const_item :public store_key_item
{
  bool inited;
public:
  store_key_const_item(Session *session, Field *to_field_arg, unsigned char *ptr,
		       unsigned char *null_ptr_arg, uint32_t length,
		       Item *item_arg)
    :store_key_item(session, to_field_arg,ptr,
		    null_ptr_arg ? null_ptr_arg : item_arg->maybe_null ?
		    &err : (unsigned char*) 0, length, item_arg), inited(0)
  {
  }
  const char *name() const { return "const"; }

protected:
  enum store_key_result copy_inner()
  {
    int res;
    if (!inited)
    {
      inited=1;
      if ((res= item->save_in_field(to_field, 1)))
      {
        if (!err)
          err= res;
      }
    }
    null_key= to_field->is_null() || item->null_value;
    return (err > 2 ?  STORE_KEY_FATAL : (store_key_result) err);
  }
};

} /* namespace drizzled */

#endif /* DRIZZLED_STORED_KEY_H */
