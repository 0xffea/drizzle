/* Copyright (C) 2000-2006 MySQL AB

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

/* Remove a row from a MyISAM table */

#include "myisamdef.h"

static int d_search(MI_INFO *info,MI_KEYDEF *keyinfo,uint comp_flag,
                    unsigned char *key,uint key_length,my_off_t page,unsigned char *anc_buff);
static int del(MI_INFO *info,MI_KEYDEF *keyinfo,unsigned char *key,unsigned char *anc_buff,
	       my_off_t leaf_page,unsigned char *leaf_buff,unsigned char *keypos,
	       my_off_t next_block,unsigned char *ret_key);
static int underflow(MI_INFO *info,MI_KEYDEF *keyinfo,unsigned char *anc_buff,
		     my_off_t leaf_page,unsigned char *leaf_buff,unsigned char *keypos);
static uint remove_key(MI_KEYDEF *keyinfo,uint nod_flag,unsigned char *keypos,
		       unsigned char *lastkey,unsigned char *page_end,
		       my_off_t *next_block);
static int _mi_ck_real_delete(register MI_INFO *info,MI_KEYDEF *keyinfo,
			      unsigned char *key, uint key_length, my_off_t *root);


int mi_delete(MI_INFO *info,const unsigned char *record)
{
  uint i;
  unsigned char *old_key;
  int save_errno;
  char lastpos[8];

  MYISAM_SHARE *share=info->s;

	/* Test if record is in datafile */
  if (!(info->update & HA_STATE_AKTIV))
  {
    return(my_errno=HA_ERR_KEY_NOT_FOUND);	/* No database read */
  }
  if (share->options & HA_OPTION_READ_ONLY_DATA)
  {
    return(my_errno=EACCES);
  }
  if (_mi_readinfo(info,F_WRLCK,1))
    return(my_errno);
  if (info->s->calc_checksum)
    info->checksum=(*info->s->calc_checksum)(info,record);
  if ((*share->compare_record)(info,record))
    goto err;				/* Error on read-check */

  if (_mi_mark_file_changed(info))
    goto err;

	/* Remove all keys from the .ISAM file */

  old_key=info->lastkey2;
  for (i=0 ; i < share->base.keys ; i++ )
  {
    if (mi_is_key_active(info->s->state.key_map, i))
    {
      info->s->keyinfo[i].version++;
      {
        if (info->s->keyinfo[i].ck_delete(info,i,old_key,
                _mi_make_key(info,i,old_key,record,info->lastpos)))
          goto err;
      }
      /* The above changed info->lastkey2. Inform mi_rnext_same(). */
      info->update&= ~HA_STATE_RNEXT_SAME;
    }
  }

  if ((*share->delete_record)(info))
    goto err;				/* Remove record from database */
  info->state->checksum-=info->checksum;

  info->update= HA_STATE_CHANGED+HA_STATE_DELETED+HA_STATE_ROW_CHANGED;
  info->state->records--;

  mi_sizestore(lastpos,info->lastpos);
  _mi_writeinfo(info,WRITEINFO_UPDATE_KEYFILE);
  if (info->invalidator != 0)
  {
    (*info->invalidator)(info->filename);
    info->invalidator=0;
  }
  return(0);

err:
  save_errno=my_errno;
  mi_sizestore(lastpos,info->lastpos);
  if (save_errno != HA_ERR_RECORD_CHANGED)
  {
    mi_print_error(info->s, HA_ERR_CRASHED);
    mi_mark_crashed(info);		/* mark table crashed */
  }
  _mi_writeinfo(info,WRITEINFO_UPDATE_KEYFILE);
  info->update|=HA_STATE_WRITTEN;	/* Buffer changed */
  my_errno=save_errno;
  if (save_errno == HA_ERR_KEY_NOT_FOUND)
  {
    mi_print_error(info->s, HA_ERR_CRASHED);
    my_errno=HA_ERR_CRASHED;
  }

  return(my_errno);
} /* mi_delete */


	/* Remove a key from the btree index */

int _mi_ck_delete(register MI_INFO *info, uint keynr, unsigned char *key,
		  uint key_length)
{
  return _mi_ck_real_delete(info, info->s->keyinfo+keynr, key, key_length,
                            &info->s->state.key_root[keynr]);
} /* _mi_ck_delete */


static int _mi_ck_real_delete(register MI_INFO *info, MI_KEYDEF *keyinfo,
			      unsigned char *key, uint key_length, my_off_t *root)
{
  int error;
  uint nod_flag;
  my_off_t old_root;
  unsigned char *root_buff;

  if ((old_root=*root) == HA_OFFSET_ERROR)
  {
    mi_print_error(info->s, HA_ERR_CRASHED);
    return(my_errno=HA_ERR_CRASHED);
  }
  if (!(root_buff= (unsigned char*) my_alloca((uint) keyinfo->block_length+
				      MI_MAX_KEY_BUFF*2)))
  {
    return(my_errno=ENOMEM);
  }
  if (!_mi_fetch_keypage(info,keyinfo,old_root,DFLT_INIT_HITS,root_buff,0))
  {
    error= -1;
    goto err;
  }
  if ((error=d_search(info,keyinfo, (SEARCH_SAME), key,key_length,old_root,root_buff)) > 0)
  {
    if (error == 2)
    {
      error=_mi_enlarge_root(info,keyinfo,key,root);
    }
    else /* error == 1 */
    {
      if (mi_getint(root_buff) <= (nod_flag=mi_test_if_nod(root_buff))+3)
      {
	error=0;
	if (nod_flag)
	  *root=_mi_kpos(nod_flag,root_buff+2+nod_flag);
	else
	  *root=HA_OFFSET_ERROR;
	if (_mi_dispose(info,keyinfo,old_root,DFLT_INIT_HITS))
	  error= -1;
      }
      else
	error=_mi_write_keypage(info,keyinfo,old_root,
                                DFLT_INIT_HITS,root_buff);
    }
  }
err:
  my_afree((unsigned char*) root_buff);
  return(error);
} /* _mi_ck_real_delete */


	/*
	** Remove key below key root
	** Return values:
	** 1 if there are less buffers;  In this case anc_buff is not saved
	** 2 if there are more buffers
	** -1 on errors
	*/

static int d_search(register MI_INFO *info, register MI_KEYDEF *keyinfo,
                    uint comp_flag, unsigned char *key, uint key_length,
                    my_off_t page, unsigned char *anc_buff)
{
  int flag,ret_value,save_flag;
  uint length,nod_flag,search_key_length;
  bool last_key;
  unsigned char *leaf_buff,*keypos;
  my_off_t leaf_page= 0, next_block;
  unsigned char lastkey[MI_MAX_KEY_BUFF];

  search_key_length= (comp_flag & SEARCH_FIND) ? key_length : USE_WHOLE_KEY;
  flag=(*keyinfo->bin_search)(info,keyinfo,anc_buff,key, search_key_length,
                              comp_flag, &keypos, lastkey, &last_key);
  if (flag == MI_FOUND_WRONG_KEY)
  {
    return(-1);
  }
  nod_flag=mi_test_if_nod(anc_buff);

  leaf_buff= 0;
  if (nod_flag)
  {
    leaf_page=_mi_kpos(nod_flag,keypos);
    if (!(leaf_buff= (unsigned char*) my_alloca((uint) keyinfo->block_length+
					MI_MAX_KEY_BUFF*2)))
    {
      my_errno=ENOMEM;
      return(-1);
    }
    if (!_mi_fetch_keypage(info,keyinfo,leaf_page,DFLT_INIT_HITS,leaf_buff,0))
      goto err;
  }

  if (flag != 0)
  {
    if (!nod_flag)
    {
      mi_print_error(info->s, HA_ERR_CRASHED);
      my_errno=HA_ERR_CRASHED;		/* This should newer happend */
      goto err;
    }
    save_flag=0;
    ret_value=d_search(info,keyinfo,comp_flag,key,key_length,
                       leaf_page,leaf_buff);
  }
  else
  {						/* Found key */
    uint tmp;
    length=mi_getint(anc_buff);
    if (!(tmp= remove_key(keyinfo,nod_flag,keypos,lastkey,anc_buff+length,
                          &next_block)))
      goto err;

    length-= tmp;

    mi_putint(anc_buff,length,nod_flag);
    if (!nod_flag)
    {						/* On leaf page */
      if (_mi_write_keypage(info,keyinfo,page,DFLT_INIT_HITS,anc_buff))
      {
	return(-1);
      }
      /* Page will be update later if we return 1 */
      return(test(length <= (info->quick_mode ? MI_MIN_KEYBLOCK_LENGTH :
				  (uint) keyinfo->underflow_block_length)));
    }
    save_flag=1;
    ret_value=del(info,keyinfo,key,anc_buff,leaf_page,leaf_buff,keypos,
		  next_block,lastkey);
  }
  if (ret_value >0)
  {
    save_flag=1;
    if (ret_value == 1)
      ret_value= underflow(info,keyinfo,anc_buff,leaf_page,leaf_buff,keypos);
    else
    {				/* This happens only with packed keys */
      if (!_mi_get_last_key(info,keyinfo,anc_buff,lastkey,keypos,&length))
      {
	goto err;
      }
      ret_value=_mi_insert(info,keyinfo,key,anc_buff,keypos,lastkey,
			   (unsigned char*) 0,(unsigned char*) 0,(my_off_t) 0,(bool) 0);
    }
  }
  if (ret_value == 0 && mi_getint(anc_buff) > keyinfo->block_length)
  {
    save_flag=1;
    ret_value=_mi_split_page(info,keyinfo,key,anc_buff,lastkey,0) | 2;
  }
  if (save_flag && ret_value != 1)
    ret_value|=_mi_write_keypage(info,keyinfo,page,DFLT_INIT_HITS,anc_buff);
  my_afree((unsigned char*) leaf_buff);
  return(ret_value);

err:
  my_afree((unsigned char*) leaf_buff);
  return (-1);
} /* d_search */


	/* Remove a key that has a page-reference */

static int del(register MI_INFO *info, register MI_KEYDEF *keyinfo, unsigned char *key,
	       unsigned char *anc_buff, my_off_t leaf_page, unsigned char *leaf_buff,
	       unsigned char *keypos,		/* Pos to where deleted key was */
	       my_off_t next_block,
	       unsigned char *ret_key)		/* key before keypos in anc_buff */
{
  int ret_value,length;
  uint a_length,nod_flag,tmp;
  my_off_t next_page;
  unsigned char keybuff[MI_MAX_KEY_BUFF],*endpos,*next_buff,*key_start, *prev_key;
  MYISAM_SHARE *share=info->s;
  MI_KEY_PARAM s_temp;

  endpos=leaf_buff+mi_getint(leaf_buff);
  if (!(key_start=_mi_get_last_key(info,keyinfo,leaf_buff,keybuff,endpos,
				   &tmp)))
    return(-1);

  if ((nod_flag=mi_test_if_nod(leaf_buff)))
  {
    next_page= _mi_kpos(nod_flag,endpos);
    if (!(next_buff= (unsigned char*) my_alloca((uint) keyinfo->block_length+
					MI_MAX_KEY_BUFF*2)))
      return(-1);
    if (!_mi_fetch_keypage(info,keyinfo,next_page,DFLT_INIT_HITS,next_buff,0))
      ret_value= -1;
    else
    {
      if ((ret_value=del(info,keyinfo,key,anc_buff,next_page,next_buff,
			 keypos,next_block,ret_key)) >0)
      {
	endpos=leaf_buff+mi_getint(leaf_buff);
	if (ret_value == 1)
	{
	  ret_value=underflow(info,keyinfo,leaf_buff,next_page,
			      next_buff,endpos);
	  if (ret_value == 0 && mi_getint(leaf_buff) > keyinfo->block_length)
	  {
	    ret_value=_mi_split_page(info,keyinfo,key,leaf_buff,ret_key,0) | 2;
	  }
	}
	else
	{
	  if (!_mi_get_last_key(info,keyinfo,leaf_buff,keybuff,endpos,
				&tmp))
	    goto err;
	  ret_value=_mi_insert(info,keyinfo,key,leaf_buff,endpos,keybuff,
			       (unsigned char*) 0,(unsigned char*) 0,(my_off_t) 0,0);
	}
      }
      if (_mi_write_keypage(info,keyinfo,leaf_page,DFLT_INIT_HITS,leaf_buff))
	goto err;
    }
    my_afree((unsigned char*) next_buff);
    return(ret_value);
  }

	/* Remove last key from leaf page */

  mi_putint(leaf_buff,key_start-leaf_buff,nod_flag);
  if (_mi_write_keypage(info,keyinfo,leaf_page,DFLT_INIT_HITS,leaf_buff))
    goto err;

	/* Place last key in ancestor page on deleted key position */

  a_length=mi_getint(anc_buff);
  endpos=anc_buff+a_length;
  if (keypos != anc_buff+2+share->base.key_reflength &&
      !_mi_get_last_key(info,keyinfo,anc_buff,ret_key,keypos,&tmp))
    goto err;
  prev_key=(keypos == anc_buff+2+share->base.key_reflength ?
	    0 : ret_key);
  length=(*keyinfo->pack_key)(keyinfo,share->base.key_reflength,
			      keypos == endpos ? (unsigned char*) 0 : keypos,
			      prev_key, prev_key,
			      keybuff,&s_temp);
  if (length > 0)
    bmove_upp((unsigned char*) endpos+length,(unsigned char*) endpos,(uint) (endpos-keypos));
  else
    memcpy(keypos,keypos-length, (int) (endpos-keypos)+length);
  (*keyinfo->store_key)(keyinfo,keypos,&s_temp);
  /* Save pointer to next leaf */
  if (!(*keyinfo->get_key)(keyinfo,share->base.key_reflength,&keypos,ret_key))
    goto err;
  _mi_kpointer(info,keypos - share->base.key_reflength,next_block);
  mi_putint(anc_buff,a_length+length,share->base.key_reflength);

  return( mi_getint(leaf_buff) <=
	       (info->quick_mode ? MI_MIN_KEYBLOCK_LENGTH :
		(uint) keyinfo->underflow_block_length));
err:
  return(-1);
} /* del */


	/* Balances adjacent pages if underflow occours */

static int underflow(register MI_INFO *info, register MI_KEYDEF *keyinfo,
		     unsigned char *anc_buff,
		     my_off_t leaf_page,/* Ancestor page and underflow page */
		     unsigned char *leaf_buff,
		     unsigned char *keypos)	/* Position to pos after key */
{
  int t_length;
  uint length,anc_length,buff_length,leaf_length,p_length,s_length,nod_flag,
       key_reflength,key_length;
  my_off_t next_page;
  unsigned char anc_key[MI_MAX_KEY_BUFF],leaf_key[MI_MAX_KEY_BUFF],
        *buff,*endpos,*next_keypos,*anc_pos,*half_pos,*temp_pos,*prev_key,
        *after_key;
  MI_KEY_PARAM s_temp;
  MYISAM_SHARE *share=info->s;

  buff=info->buff;
  info->buff_used=1;
  next_keypos=keypos;
  nod_flag=mi_test_if_nod(leaf_buff);
  p_length=nod_flag+2;
  anc_length=mi_getint(anc_buff);
  leaf_length=mi_getint(leaf_buff);
  key_reflength=share->base.key_reflength;
  if (info->s->keyinfo+info->lastinx == keyinfo)
    info->page_changed=1;

  if ((keypos < anc_buff+anc_length && (info->state->records & 1)) ||
      keypos == anc_buff+2+key_reflength)
  {					/* Use page right of anc-page */
    if (keyinfo->flag & HA_BINARY_PACK_KEY)
    {
      if (!(next_keypos=_mi_get_key(info, keyinfo,
				    anc_buff, buff, keypos, &length)))
	goto err;
    }
    else
    {
      /* Got to end of found key */
      buff[0]=buff[1]=0;	/* Avoid length error check if packed key */
      if (!(*keyinfo->get_key)(keyinfo,key_reflength,&next_keypos,
			       buff))
      goto err;
    }
    next_page= _mi_kpos(key_reflength,next_keypos);
    if (!_mi_fetch_keypage(info,keyinfo,next_page,DFLT_INIT_HITS,buff,0))
      goto err;
    buff_length=mi_getint(buff);

    /* find keys to make a big key-page */
    memcpy(next_keypos - key_reflength, buff + 2, key_reflength);
    if (!_mi_get_last_key(info,keyinfo,anc_buff,anc_key,next_keypos,&length)
	|| !_mi_get_last_key(info,keyinfo,leaf_buff,leaf_key,
			     leaf_buff+leaf_length,&length))
      goto err;

    /* merge pages and put parting key from anc_buff between */
    prev_key=(leaf_length == p_length ? (unsigned char*) 0 : leaf_key);
    t_length=(*keyinfo->pack_key)(keyinfo,nod_flag,buff+p_length,
				  prev_key, prev_key,
				  anc_key, &s_temp);
    length=buff_length-p_length;
    endpos=buff+length+leaf_length+t_length;
    /* buff will always be larger than before !*/
    bmove_upp((unsigned char*) endpos, (unsigned char*) buff+buff_length,length);
    memcpy(buff, leaf_buff, leaf_length);
    (*keyinfo->store_key)(keyinfo,buff+leaf_length,&s_temp);
    buff_length=(uint) (endpos-buff);
    mi_putint(buff,buff_length,nod_flag);

    /* remove key from anc_buff */

    if (!(s_length=remove_key(keyinfo,key_reflength,keypos,anc_key,
                              anc_buff+anc_length,(my_off_t *) 0)))
      goto err;

    anc_length-=s_length;
    mi_putint(anc_buff,anc_length,key_reflength);

    if (buff_length <= keyinfo->block_length)
    {						/* Keys in one page */
      memcpy(leaf_buff, buff, buff_length);
      if (_mi_dispose(info,keyinfo,next_page,DFLT_INIT_HITS))
       goto err;
    }
    else
    {						/* Page is full */
      endpos=anc_buff+anc_length;
      if (keypos != anc_buff+2+key_reflength &&
	  !_mi_get_last_key(info,keyinfo,anc_buff,anc_key,keypos,&length))
	goto err;
      if (!(half_pos=_mi_find_half_pos(nod_flag, keyinfo, buff, leaf_key,
				       &key_length, &after_key)))
	goto err;
      length=(uint) (half_pos-buff);
      memcpy(leaf_buff, buff, length);
      mi_putint(leaf_buff,length,nod_flag);

      /* Correct new keypointer to leaf_page */
      half_pos=after_key;
      _mi_kpointer(info,leaf_key+key_length,next_page);
      /* Save key in anc_buff */
      prev_key=(keypos == anc_buff+2+key_reflength ? (unsigned char*) 0 : anc_key),
      t_length=(*keyinfo->pack_key)(keyinfo,key_reflength,
				    (keypos == endpos ? (unsigned char*) 0 :
				     keypos),
				    prev_key, prev_key,
				    leaf_key, &s_temp);
      if (t_length >= 0)
	bmove_upp((unsigned char*) endpos+t_length,(unsigned char*) endpos,
		  (uint) (endpos-keypos));
      else
	memcpy(keypos,keypos-t_length,(uint) (endpos-keypos)+t_length);
      (*keyinfo->store_key)(keyinfo,keypos,&s_temp);
      mi_putint(anc_buff,(anc_length+=t_length),key_reflength);

	/* Store key first in new page */
      if (nod_flag)
	memcpy(buff + 2, half_pos - nod_flag, nod_flag);
      if (!(*keyinfo->get_key)(keyinfo,nod_flag,&half_pos,leaf_key))
	goto err;
      t_length=(int) (*keyinfo->pack_key)(keyinfo, nod_flag, (unsigned char*) 0,
					  (unsigned char*) 0, (unsigned char *) 0,
					  leaf_key, &s_temp);
      /* t_length will always be > 0 for a new page !*/
      length=(uint) ((buff+mi_getint(buff))-half_pos);
      memcpy(buff + p_length + t_length, half_pos, length);
      (*keyinfo->store_key)(keyinfo,buff+p_length,&s_temp);
      mi_putint(buff,length+t_length+p_length,nod_flag);

      if (_mi_write_keypage(info,keyinfo,next_page,DFLT_INIT_HITS,buff))
	goto err;
    }
    if (_mi_write_keypage(info,keyinfo,leaf_page,DFLT_INIT_HITS,leaf_buff))
      goto err;
    return(anc_length <= ((info->quick_mode ? MI_MIN_BLOCK_LENGTH :
				(uint) keyinfo->underflow_block_length)));
  }

  keypos=_mi_get_last_key(info,keyinfo,anc_buff,anc_key,keypos,&length);
  if (!keypos)
    goto err;
  next_page= _mi_kpos(key_reflength,keypos);
  if (!_mi_fetch_keypage(info,keyinfo,next_page,DFLT_INIT_HITS,buff,0))
      goto err;
  buff_length=mi_getint(buff);
  endpos=buff+buff_length;

  /* find keys to make a big key-page */
  memcpy(next_keypos - key_reflength, leaf_buff+2, key_reflength);
  next_keypos=keypos;
  if (!(*keyinfo->get_key)(keyinfo,key_reflength,&next_keypos,
			   anc_key))
    goto err;
  if (!_mi_get_last_key(info,keyinfo,buff,leaf_key,endpos,&length))
    goto err;

  /* merge pages and put parting key from anc_buff between */
  prev_key=(leaf_length == p_length ? (unsigned char*) 0 : leaf_key);
  t_length=(*keyinfo->pack_key)(keyinfo,nod_flag,
				(leaf_length == p_length ?
                                 (unsigned char*) 0 : leaf_buff+p_length),
				prev_key, prev_key,
				anc_key, &s_temp);
  if (t_length >= 0)
    memcpy(endpos+t_length,leaf_buff+p_length, leaf_length-p_length);
  else						/* We gained space */
    memcpy(endpos, leaf_buff+((int) p_length-t_length),
           leaf_length - p_length + t_length);

  (*keyinfo->store_key)(keyinfo,endpos,&s_temp);
  buff_length=buff_length+leaf_length-p_length+t_length;
  mi_putint(buff,buff_length,nod_flag);

  /* remove key from anc_buff */
  if (!(s_length= remove_key(keyinfo,key_reflength,keypos,anc_key,
                             anc_buff+anc_length,(my_off_t *) 0)))
    goto err;

  anc_length-=s_length;
  mi_putint(anc_buff,anc_length,key_reflength);

  if (buff_length <= keyinfo->block_length)
  {						/* Keys in one page */
    if (_mi_dispose(info,keyinfo,leaf_page,DFLT_INIT_HITS))
      goto err;
  }
  else
  {						/* Page is full */
    if (keypos == anc_buff+2+key_reflength)
      anc_pos=0;				/* First key */
    else if (!_mi_get_last_key(info,keyinfo,anc_buff,anc_pos=anc_key,keypos,
			       &length))
      goto err;
    endpos=_mi_find_half_pos(nod_flag,keyinfo,buff,leaf_key,
			     &key_length, &half_pos);
    if (!endpos)
      goto err;
    _mi_kpointer(info,leaf_key+key_length,leaf_page);
    /* Save key in anc_buff */

    temp_pos=anc_buff+anc_length;
    t_length=(*keyinfo->pack_key)(keyinfo,key_reflength,
				  keypos == temp_pos ? (unsigned char*) 0
				  : keypos,
				  anc_pos, anc_pos,
				  leaf_key,&s_temp);
    if (t_length > 0)
      bmove_upp((unsigned char*) temp_pos+t_length,(unsigned char*) temp_pos,
		(uint) (temp_pos-keypos));
    else
      memcpy(keypos,keypos-t_length,(uint) (temp_pos-keypos)+t_length);
    (*keyinfo->store_key)(keyinfo,keypos,&s_temp);
    mi_putint(anc_buff,(anc_length+=t_length),key_reflength);

    /* Store first key on new page */
    if (nod_flag)
      memcpy(leaf_buff+2, half_pos - nod_flag, nod_flag);
    if (!(length=(*keyinfo->get_key)(keyinfo,nod_flag,&half_pos,leaf_key)))
      goto err;
    t_length=(*keyinfo->pack_key)(keyinfo,nod_flag, (unsigned char*) 0,
				  (unsigned char*) 0, (unsigned char*) 0, leaf_key, &s_temp);
    length=(uint) ((buff+buff_length)-half_pos);
    memcpy(leaf_buff + p_length + t_length, half_pos, length);
    (*keyinfo->store_key)(keyinfo,leaf_buff+p_length,&s_temp);
    mi_putint(leaf_buff,length+t_length+p_length,nod_flag);
    if (_mi_write_keypage(info,keyinfo,leaf_page,DFLT_INIT_HITS,leaf_buff))
      goto err;
    mi_putint(buff,endpos-buff,nod_flag);
  }
  if (_mi_write_keypage(info,keyinfo,next_page,DFLT_INIT_HITS,buff))
    goto err;
  return(anc_length <= (uint) keyinfo->block_length/2);

err:
  return(-1);
} /* underflow */


	/*
	  remove a key from packed buffert
	  The current code doesn't handle the case that the next key may be
	  packed better against the previous key if there is a case difference
	  returns how many chars was removed or 0 on error
	*/

static uint remove_key(MI_KEYDEF *keyinfo, uint nod_flag,
		       unsigned char *keypos,	/* Where key starts */
		       unsigned char *lastkey,	/* key to be removed */
		       unsigned char *page_end, /* End of page */
		       my_off_t *next_block)	/* ptr to next block */
{
  int s_length;
  unsigned char *start;

  start=keypos;
  if (!(keyinfo->flag &
	(HA_PACK_KEY | HA_SPACE_PACK_USED | HA_VAR_LENGTH_KEY |
	 HA_BINARY_PACK_KEY)))
  {
    s_length=(int) (keyinfo->keylength+nod_flag);
    if (next_block && nod_flag)
      *next_block= _mi_kpos(nod_flag,keypos+s_length);
  }
  else
  {					 /* Let keypos point at next key */
    /* Calculate length of key */
    if (!(*keyinfo->get_key)(keyinfo,nod_flag,&keypos,lastkey))
      return(0);				/* Error */

    if (next_block && nod_flag)
      *next_block= _mi_kpos(nod_flag,keypos);
    s_length=(int) (keypos-start);
    if (keypos != page_end)
    {
      if (keyinfo->flag & HA_BINARY_PACK_KEY)
      {
	unsigned char *old_key=start;
	uint next_length,prev_length,prev_pack_length;
	get_key_length(next_length,keypos);
	get_key_pack_length(prev_length,prev_pack_length,old_key);
	if (next_length > prev_length)
	{
	  /* We have to copy data from the current key to the next key */
	  bmove_upp(keypos, (lastkey+next_length),
		    (next_length-prev_length));
	  keypos-=(next_length-prev_length)+prev_pack_length;
	  store_key_length(keypos,prev_length);
	  s_length=(int) (keypos-start);
	}
      }
      else
      {
	/* Check if a variable length first key part */
	if ((keyinfo->seg->flag & HA_PACK_KEY) && *keypos & 128)
	{
	  /* Next key is packed against the current one */
	  uint next_length,prev_length,prev_pack_length,lastkey_length,
	    rest_length;
	  if (keyinfo->seg[0].length >= 127)
	  {
	    if (!(prev_length=mi_uint2korr(start) & 32767))
	      goto end;
	    next_length=mi_uint2korr(keypos) & 32767;
	    keypos+=2;
	    prev_pack_length=2;
	  }
	  else
	  {
	    if (!(prev_length= *start & 127))
	      goto end;				/* Same key as previous*/
	    next_length= *keypos & 127;
	    keypos++;
	    prev_pack_length=1;
	  }
	  if (!(*start & 128))
	    prev_length=0;			/* prev key not packed */
	  if (keyinfo->seg[0].flag & HA_NULL_PART)
	    lastkey++;				/* Skip null marker */
	  get_key_length(lastkey_length,lastkey);
	  if (!next_length)			/* Same key after */
	  {
	    next_length=lastkey_length;
	    rest_length=0;
	  }
	  else
	    get_key_length(rest_length,keypos);

	  if (next_length >= prev_length)
	  {		/* Key after is based on deleted key */
	    uint pack_length,tmp;
	    bmove_upp(keypos, (lastkey+next_length),
		      tmp=(next_length-prev_length));
	    rest_length+=tmp;
	    pack_length= prev_length ? get_pack_length(rest_length): 0;
	    keypos-=tmp+pack_length+prev_pack_length;
	    s_length=(int) (keypos-start);
	    if (prev_length)			/* Pack against prev key */
	    {
	      *keypos++= start[0];
	      if (prev_pack_length == 2)
		*keypos++= start[1];
	      store_key_length(keypos,rest_length);
	    }
	    else
	    {
	      /* Next key is not packed anymore */
	      if (keyinfo->seg[0].flag & HA_NULL_PART)
	      {
		rest_length++;			/* Mark not null */
	      }
	      if (prev_pack_length == 2)
	      {
		mi_int2store(keypos,rest_length);
	      }
	      else
		*keypos= rest_length;
	    }
	  }
	}
      }
    }
  }
end:
  assert(page_end-start >= s_length);
  memcpy(start, start + s_length, page_end-start-s_length);
  return s_length;
} /* remove_key */
