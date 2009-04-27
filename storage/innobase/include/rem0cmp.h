/*****************************************************************************

Copyright (c) 1994, 2009, Innobase Oy. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place, Suite 330, Boston, MA 02111-1307 USA

*****************************************************************************/

/***********************************************************************
Comparison services for records

Created 7/1/1994 Heikki Tuuri
************************************************************************/

#ifndef rem0cmp_h
#define rem0cmp_h

#include "univ.i"
#include "data0data.h"
#include "data0type.h"
#include "dict0dict.h"
#include "rem0rec.h"

/*****************************************************************
Returns TRUE if two columns are equal for comparison purposes. */
UNIV_INTERN
ibool
cmp_cols_are_equal(
/*===============*/
					/* out: TRUE if the columns are
					considered equal in comparisons */
	const dict_col_t*	col1,	/* in: column 1 */
	const dict_col_t*	col2,	/* in: column 2 */
	ibool			check_charsets);
					/* in: whether to check charsets */
/*****************************************************************
This function is used to compare two data fields for which we know the
data type. */
UNIV_INLINE
int
cmp_data_data(
/*==========*/
				/* out: 1, 0, -1, if data1 is greater, equal,
				less than data2, respectively */
	ulint		mtype,	/* in: main type */
	ulint		prtype,	/* in: precise type */
	const byte*	data1,	/* in: data field (== a pointer to a memory
				buffer) */
	ulint		len1,	/* in: data field length or UNIV_SQL_NULL */
	const byte*	data2,	/* in: data field (== a pointer to a memory
				buffer) */
	ulint		len2);	/* in: data field length or UNIV_SQL_NULL */
/*****************************************************************
This function is used to compare two data fields for which we know the
data type. */
UNIV_INTERN
int
cmp_data_data_slow(
/*===============*/
				/* out: 1, 0, -1, if data1 is greater, equal,
				less than data2, respectively */
	ulint		mtype,	/* in: main type */
	ulint		prtype,	/* in: precise type */
	const byte*	data1,	/* in: data field (== a pointer to a memory
				buffer) */
	ulint		len1,	/* in: data field length or UNIV_SQL_NULL */
	const byte*	data2,	/* in: data field (== a pointer to a memory
				buffer) */
	ulint		len2);	/* in: data field length or UNIV_SQL_NULL */
/*****************************************************************
This function is used to compare two dfields where at least the first
has its data type field set. */
UNIV_INLINE
int
cmp_dfield_dfield(
/*==============*/
				/* out: 1, 0, -1, if dfield1 is greater, equal,
				less than dfield2, respectively */
	const dfield_t*	dfield1,/* in: data field; must have type field set */
	const dfield_t*	dfield2);/* in: data field */
/*****************************************************************
This function is used to compare a data tuple to a physical record.
Only dtuple->n_fields_cmp first fields are taken into account for
the the data tuple! If we denote by n = n_fields_cmp, then rec must
have either m >= n fields, or it must differ from dtuple in some of
the m fields rec has. If rec has an externally stored field we do not
compare it but return with value 0 if such a comparison should be
made. */
UNIV_INTERN
int
cmp_dtuple_rec_with_match(
/*======================*/
				/* out: 1, 0, -1, if dtuple is greater, equal,
				less than rec, respectively, when only the
				common first fields are compared, or
				until the first externally stored field in
				rec */
	const dtuple_t*	dtuple,	/* in: data tuple */
	const rec_t*	rec,	/* in: physical record which differs from
				dtuple in some of the common fields, or which
				has an equal number or more fields than
				dtuple */
	const ulint*	offsets,/* in: array returned by rec_get_offsets() */
	ulint*		matched_fields, /* in/out: number of already completely
				matched fields; when function returns,
				contains the value for current comparison */
	ulint*		matched_bytes); /* in/out: number of already matched
				bytes within the first field not completely
				matched; when function returns, contains the
				value for current comparison */
/******************************************************************
Compares a data tuple to a physical record. */
UNIV_INTERN
int
cmp_dtuple_rec(
/*===========*/
				/* out: 1, 0, -1, if dtuple is greater, equal,
				less than rec, respectively; see the comments
				for cmp_dtuple_rec_with_match */
	const dtuple_t*	dtuple,	/* in: data tuple */
	const rec_t*	rec,	/* in: physical record */
	const ulint*	offsets);/* in: array returned by rec_get_offsets() */
/******************************************************************
Checks if a dtuple is a prefix of a record. The last field in dtuple
is allowed to be a prefix of the corresponding field in the record. */
UNIV_INTERN
ibool
cmp_dtuple_is_prefix_of_rec(
/*========================*/
				/* out: TRUE if prefix */
	const dtuple_t*	dtuple,	/* in: data tuple */
	const rec_t*	rec,	/* in: physical record */
	const ulint*	offsets);/* in: array returned by rec_get_offsets() */
#ifndef UNIV_HOTBACKUP
/*****************************************************************
Compare two physical records that contain the same number of columns,
none of which are stored externally. */
UNIV_INTERN
int
cmp_rec_rec_simple(
/*===============*/
					/* out: 1, 0 , -1 if rec1 is greater,
					equal, less, respectively, than rec2 */
	const rec_t*		rec1,	/* in: physical record */
	const rec_t*		rec2,	/* in: physical record */
	const ulint*		offsets1,/* in: rec_get_offsets(rec1, index) */
	const ulint*		offsets2,/* in: rec_get_offsets(rec2, index) */
	const dict_index_t*	index);	/* in: data dictionary index */
#endif /* !UNIV_HOTBACKUP */
/*****************************************************************
This function is used to compare two physical records. Only the common
first fields are compared, and if an externally stored field is
encountered, then 0 is returned. */
UNIV_INTERN
int
cmp_rec_rec_with_match(
/*===================*/
				/* out: 1, 0 , -1 if rec1 is greater, equal,
				less, respectively, than rec2; only the common
				first fields are compared */
	const rec_t*	rec1,	/* in: physical record */
	const rec_t*	rec2,	/* in: physical record */
	const ulint*	offsets1,/* in: rec_get_offsets(rec1, index) */
	const ulint*	offsets2,/* in: rec_get_offsets(rec2, index) */
	dict_index_t*	index,	/* in: data dictionary index */
	ulint*		matched_fields, /* in/out: number of already completely
				matched fields; when the function returns,
				contains the value the for current
				comparison */
	ulint*		matched_bytes);/* in/out: number of already matched
				bytes within the first field not completely
				matched; when the function returns, contains
				the value for the current comparison */
/*****************************************************************
This function is used to compare two physical records. Only the common
first fields are compared. */
UNIV_INLINE
int
cmp_rec_rec(
/*========*/
				/* out: 1, 0 , -1 if rec1 is greater, equal,
				less, respectively, than rec2; only the common
				first fields are compared */
	const rec_t*	rec1,	/* in: physical record */
	const rec_t*	rec2,	/* in: physical record */
	const ulint*	offsets1,/* in: rec_get_offsets(rec1, index) */
	const ulint*	offsets2,/* in: rec_get_offsets(rec2, index) */
	dict_index_t*	index);	/* in: data dictionary index */


#ifndef UNIV_NONINL
#include "rem0cmp.ic"
#endif

#endif