/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 * Copyright (c) 2009, Patrick "CaptTofu" Galbraith, Padraig O'Sullivan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *   * Neither the name of Patrick Galbraith nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PLUGIN_MEMCACHED_FUNCTIONS_MEMC_MISC_H
#define PLUGIN_MEMCACHED_FUNCTIONS_MEMC_MISC_H

#include "config.h"
#include <drizzled/item/func.h>
#include <drizzled/function/str/strfunc.h>

/* implements memc_libmemcached_version */
class MemcachedVersion : public Item_str_func
{
public:
  MemcachedVersion()
    : 
      Item_str_func(),
      failure_buff("FAILURE", &my_charset_bin)
  {}

  const char *func_name() const
  {
    return "memc_libmemcached_version";
  }

  String *val_str(String *);

  void fix_length_and_dec()
  {
    max_length= 32;
  }

private:
  String buffer;
  String failure_buff;
};

/* implements memc_server_count */
class MemcachedServerCount : public Item_int_func
{
public:
  MemcachedServerCount()
    :
      Item_int_func()
  {
    unsigned_flag= 1;
  }

  const char *func_name() const
  {
    return "memc_server_count";
  }

  void fix_length_and_dec()
  {
    max_length= 10;
  }

  int64_t val_int();
};

#endif /* PLUGIN_MEMCACHED_FUNCTIONS_MEMC_MISC_H */
