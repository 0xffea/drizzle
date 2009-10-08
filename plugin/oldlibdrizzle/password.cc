/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems, Inc.
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

/* password checking routines */
/*****************************************************************************
  The main idea is that no password are sent between client & server on
  connection and that no password are saved in mysql in a decodable form.

  On connection a random string is generated and sent to the client.
  The client generates a new string with a random generator inited with
  the hash values from the password and the sent string.
  This 'check' string is sent to the server where it is compared with
  a string generated from the stored hash_value of the password and the
  random string.

  The password is saved (in user.password) by using the PASSWORD() function in
  mysql.

  This is .c file because it's used in libmysqlclient, which is entirely in C.
  (we need it to be portable to a variety of systems).
  Example:
    update user set password=PASSWORD("hello") where user="test"
  This saves a hashed number as a string in the password field.

  The new authentication is performed in following manner:

  SERVER:  public_seed=drizzleclient_create_random_string()
           send(public_seed)

  CLIENT:  recv(public_seed)
           hash_stage1=sha1("password")
           hash_stage2=sha1(hash_stage1)
           reply=xor(hash_stage1, sha1(public_seed,hash_stage2)

           // this three steps are done in scramble()

           send(reply)


  SERVER:  recv(reply)
           hash_stage1=xor(reply, sha1(public_seed,hash_stage2))
           candidate_hash2=sha1(hash_stage1)
           check(candidate_hash2==hash_stage2)

           // this three steps are done in check_scramble()

*****************************************************************************/

#include "libdrizzle_priv.h"
#include "password.h"

#include <stdlib.h>
#include <string.h>

/************ MySQL 3.23-4.0 authentication routines: untouched ***********/

/*
  New (MySQL 3.21+) random generation structure initialization
  SYNOPSIS
    drizzleclient_randominit()
    rand_st    OUT  Structure to initialize
    seed1      IN   First initialization parameter
    seed2      IN   Second initialization parameter
*/

void drizzleclient_randominit(struct rand_struct *rand_st,
                              uint64_t seed1,
                              uint64_t seed2)
{                                               /* For mysql 3.21.# */
  memset(rand_st, 0, sizeof(*rand_st));      /* Avoid UMC varnings */
  rand_st->max_value= 0x3FFFFFFFL;
  rand_st->max_value_dbl=(double) rand_st->max_value;
  rand_st->seed1=seed1%rand_st->max_value ;
  rand_st->seed2=seed2%rand_st->max_value;
}


/*
    Generate random number.
  SYNOPSIS
    drizzleclient_my_rnd()
    rand_st    INOUT  Structure used for number generation
  RETURN VALUE
    generated pseudo random number
*/

double drizzleclient_my_rnd(struct rand_struct *rand_st)
{
  rand_st->seed1=(rand_st->seed1*3+rand_st->seed2) % rand_st->max_value;
  rand_st->seed2=(rand_st->seed1+rand_st->seed2+33) % rand_st->max_value;
  return (((double) rand_st->seed1)/rand_st->max_value_dbl);
}


/*
    Generate binary hash from raw text string
    Used for Pre-4.1 password handling
  SYNOPSIS
    drizzleclient_hash_password()
    result       OUT store hash in this location
    password     IN  plain text password to build hash
    password_len IN  password length (password may be not null-terminated)
*/

void drizzleclient_hash_password(uint32_t *result, const char *password, uint32_t password_len)
{
  register uint32_t nr=1345345333L, add=7, nr2=0x12345671L;
  uint32_t tmp;
  const char *password_end= password + password_len;
  for (; password < password_end; password++)
  {
    if (*password == ' ' || *password == '\t')
      continue;                                 /* skip space in password */
    tmp= (uint32_t) (unsigned char) *password;
    nr^= (((nr & 63)+add)*tmp)+ (nr << 8);
    nr2+=(nr2 << 8) ^ nr;
    add+=tmp;
  }
  result[0]=nr & (((uint32_t) 1L << 31) -1L); /* Don't use sign bit (str2int) */;
  result[1]=nr2 & (((uint32_t) 1L << 31) -1L);
}

static inline uint8_t char_val(uint8_t X)
{
  return (uint32_t) (X >= '0' && X <= '9' ? X-'0' :
      X >= 'A' && X <= 'Z' ? X-'A'+10 : X-'a'+10);
}


/*
     **************** MySQL 4.1.1 authentication routines *************
*/

/*
    Generate string of printable random characters of requested length
  SYNOPSIS
    drizzleclient_create_random_string()
    to       OUT   buffer for generation; must be at least length+1 bytes
                   long; result string is always null-terminated
    length   IN    how many random characters to put in buffer
    rand_st  INOUT structure used for number generation
*/

void drizzleclient_create_random_string(char *to, uint32_t length, struct rand_struct *rand_st)
{
  char *end= to + length;
  /* Use pointer arithmetics as it is faster way to do so. */
  for (; to < end; to++)
    *to= (char) (drizzleclient_my_rnd(rand_st)*94+33);
  *to= '\0';
}

