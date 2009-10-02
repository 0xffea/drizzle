/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 * Copyright (C) 2008 MySQL AB
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA 
 */

#include <drizzled/server_includes.h>
#include <drizzled/error.h>
#include <drizzled/session.h>

using namespace std;
using namespace drizzled;

class BenchmarkFunction :public Item_int_func
{
public:
  BenchmarkFunction() :Item_int_func() {}
  int64_t val_int();
  virtual void print(String *str, enum_query_type query_type);

  const char *func_name() const
  { 
    return "benchmark"; 
  }

  void fix_length_and_dec()
  { 
    max_length= 1; 
    maybe_null= false;
  }

  bool check_argument_count(int n)
  { 
    return (n == 2); 
  }
};


/* This function is just used to test speed of different functions */
int64_t BenchmarkFunction::val_int()
{
  assert(fixed == true);

  char buff[MAX_FIELD_WIDTH];
  String tmp(buff,sizeof(buff), &my_charset_bin);
  my_decimal tmp_decimal;
  Session *session= current_session;
  uint64_t loop_count;

  loop_count= (uint64_t) args[0]->val_int();

  if (args[0]->null_value ||
      (args[0]->unsigned_flag == false && (((int64_t) loop_count) < 0)))
  {
    if (args[0]->null_value == false)
    {
      llstr(((int64_t) loop_count), buff);
      push_warning_printf(current_session, DRIZZLE_ERROR::WARN_LEVEL_ERROR,
                          ER_WRONG_VALUE_FOR_TYPE, ER(ER_WRONG_VALUE_FOR_TYPE),
                          "count", buff, "benchmark");
    }

    null_value= true;
    return 0;
  }

  null_value= false;

  uint64_t loop;
  for (loop= 0 ; loop < loop_count && !session->killed; loop++)
  {
    switch (args[1]->result_type()) 
    {
    case REAL_RESULT:
      (void) args[1]->val_real();
      break;
    case INT_RESULT:
      (void) args[1]->val_int();
      break;
    case STRING_RESULT:
      (void) args[1]->val_str(&tmp);
      break;
    case DECIMAL_RESULT:
      (void) args[1]->val_decimal(&tmp_decimal);
      break;
    case ROW_RESULT:
    default:
      // This case should never be chosen
      assert(0);
      return 0;
    }
  }
  return 0;
}

void BenchmarkFunction::print(String *str, enum_query_type query_type)
{
  str->append(STRING_WITH_LEN("benchmark("));
  args[0]->print(str, query_type);
  str->append(',');
  args[1]->print(str, query_type);
  str->append(')');
}

plugin::Create_function<BenchmarkFunction> *benchmarkudf= NULL;

static int initialize(plugin::Registry &registry)
{
  benchmarkudf= new plugin::Create_function<BenchmarkFunction>("benchmark");
  registry.add(benchmarkudf);
  return 0;
}

static int finalize(plugin::Registry &registry)
{
   registry.remove(benchmarkudf);
   delete benchmarkudf;
   return 0;
}

drizzle_declare_plugin(benchmark)
{
  "benchmark",
  "1.0",
  "Devananda van der Veen",
  "Measure time for repeated calls to a function.",
  PLUGIN_LICENSE_GPL,
  initialize, /* Plugin Init */
  finalize,   /* Plugin Deinit */
  NULL,   /* status variables */
  NULL,   /* system variables */
  NULL    /* config options */
}
drizzle_declare_plugin_end;
