/* Copyright (C) 2000 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


#ifndef DRIZZLED_MEMORY_ROOT_H
#define DRIZZLED_MEMORY_ROOT_H

#include <cstddef>

#include <drizzled/definitions.h>

#if defined(__cplusplus)
extern "C" {
#endif

namespace drizzled
{
namespace memory
{

static const int KEEP_PREALLOC= 1;
/* move used to free list and reuse them */
static const int MARK_BLOCKS_FREE= 2;

namespace internal
{

class UsedMemory
{			   /* struct for once_alloc (block) */
public:
  UsedMemory *next;	   /* Next block in use */
  size_t left;		   /* memory left in block  */            
  size_t size;		   /* size of block */
};

}

static const size_t ROOT_MIN_BLOCK_SIZE= (MALLOC_OVERHEAD + sizeof(internal::UsedMemory) + 8);



class Root
{
public:
  /* blocks with free memory in it */
  internal::UsedMemory *free;

  /* blocks almost without free memory */
  internal::UsedMemory *used;

  /* preallocated block */
  internal::UsedMemory *pre_alloc;

  /* if block have less memory it will be put in 'used' list */
  size_t min_malloc;
  size_t block_size;               /* initial block size */
  unsigned int block_num;          /* allocated blocks counter */
  /*
     first free block in queue test counter (if it exceed
     MAX_BLOCK_USAGE_BEFORE_DROP block will be dropped in 'used' list)
  */
  unsigned int first_block_usage;

  void (*error_handler)(void);
};

inline static bool alloc_root_inited(Root *root)
{
  return root->min_malloc != 0;
}

void init_alloc_root(Root *mem_root,
                     size_t block_size= ROOT_MIN_BLOCK_SIZE);
void *alloc_root(Root *mem_root, size_t Size);
void *multi_alloc_root(Root *mem_root, ...);
void free_root(Root *root, myf MyFLAGS);
void set_prealloc_root(Root *root, char *ptr);
void reset_root_defaults(Root *mem_root, size_t block_size,
                         size_t prealloc_size);
char *strdup_root(Root *root,const char *str);
char *strmake_root(Root *root,const char *str,size_t len);
void *memdup_root(Root *root,const void *str, size_t len);

}
}

#if defined(__cplusplus)
}
#endif
#endif /* DRIZZLED_MEMORY_ROOT_H */
