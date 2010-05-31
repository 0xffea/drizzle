/* Copyright (c) 2009 PrimeBase Technologies GmbH, Germany
 *
 * PrimeBase Media Stream for MySQL
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Barry Leslie
 *
 * System dump table.
 *
 */
#ifdef DRIZZLED
#include "config.h"
#include <drizzled/common.h>
#include <drizzled/session.h>
#include <drizzled/field/blob.h>
#endif

#include "cslib/CSConfig.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <time.h>

//#include "mysql_priv.h"
#include "cslib/CSGlobal.h"
#include "cslib/CSStrUtil.h"

#include "ha_pbms.h"
//#include <plugin.h>

#include "ms_mysql.h"
#include "Repository_ms.h"
#include "Database_ms.h"
#include "Compactor_ms.h"
#include "OpenTable_ms.h"
#include "Util_ms.h"
#include "Discover_ms.h"
#include "Transaction_ms.h"
#include "SysTab_variable.h"
#include "backup_ms.h"


#include "SysTab_enabled.h"


DT_FIELD_INFO pbms_enabled_info[]=
{
	{"Name",			32,		NULL, MYSQL_TYPE_VARCHAR,	&UTF8_CHARSET,	NOT_NULL_FLAG,	"PBMS enabled engine name"},
	{"IsServer",		3,		NULL, MYSQL_TYPE_VARCHAR,	&UTF8_CHARSET,					NOT_NULL_FLAG,	"Enabled at server level."},
	{"Transactional",	5,		NULL, MYSQL_TYPE_VARCHAR,	&UTF8_CHARSET,					NOT_NULL_FLAG,	"Does the engine support transactions."},
	{"API-Version",		NULL,	NULL, MYSQL_TYPE_LONG,		NULL,							NOT_NULL_FLAG,	"The PBMS enabled api version used."},
	{NULL,NULL, NULL, MYSQL_TYPE_STRING,NULL, 0, NULL}
};

DT_KEY_INFO pbms_enabled_keys[]=
{
	{"pbms_enabled_pk", PRI_KEY_FLAG, {"Name", NULL}},
	{NULL, 0, {NULL}}
};


/*
 * -------------------------------------------------------------------------
 * DUMP TABLE
 */
//-----------------------
MSEnabledTable::MSEnabledTable(MSSystemTableShare *share, TABLE *table):
MSOpenSystemTable(share, table),
iEnabledIndex(0)
{
}

//-----------------------
MSEnabledTable::~MSEnabledTable()
{
}

//-----------------------
void MSEnabledTable::seqScanInit()
{
	iEnabledIndex = 0;
}
//-----------------------
bool MSEnabledTable::seqScanNext(char *buf)
{
	TABLE		*table = mySQLTable;
	Field		*curr_field;
	byte		*save;
	MY_BITMAP	*save_write_set;
	const char *yesno;
	const PBMSEngineRec *eng;
	
	enter_();
	
	eng = MSEngine::getEngineInfoAt(iEnabledIndex++);
	if (!eng)
		return_(false);
	
	save_write_set = table->write_set;
	table->write_set = NULL;

	memset(buf, 0xFF, table->s->null_bytes);
 	for (Field **field=table->field ; *field ; field++) {
 		curr_field = *field;
		save = curr_field->ptr;
#if MYSQL_VERSION_ID < 50114
		curr_field->ptr = (byte *) buf + curr_field->offset();
#else
		curr_field->ptr = (byte *) buf + curr_field->offset(curr_field->table->record[0]);
#endif

		switch (curr_field->field_name[0]) {
			case 'N':
				ASSERT(strcmp(curr_field->field_name, "Name") == 0);
				curr_field->store(eng->ms_engine_name, strlen(eng->ms_engine_name), &UTF8_CHARSET);
				setNotNullInRecord(curr_field, buf);
				break;

			case 'I':
				ASSERT(strcmp(curr_field->field_name, "IsServer") == 0);
				if (eng->ms_internal)
					yesno = "Yes";
				else
					yesno = "No";
					
				curr_field->store(yesno, strlen(yesno), &UTF8_CHARSET);
				setNotNullInRecord(curr_field, buf);
				break;

			case 'T': 
				ASSERT(strcmp(curr_field->field_name, "Transactional") == 0);
				if (eng->ms_internal || eng->ms_version < 2 )
					yesno = "Maybe";
				else if (eng->ms_has_transactions)
					yesno = "Yes";
				else
					yesno = "No";
					
				curr_field->store(yesno, strlen(yesno), &UTF8_CHARSET);
				setNotNullInRecord(curr_field, buf);
				break;

			case 'A':
				ASSERT(strcmp(curr_field->field_name, "API-Version") == 0);
				curr_field->store(eng->ms_version, true);
				break;

		}
		curr_field->ptr = save;
	}

	table->write_set = save_write_set;
	return_(true);
}

//-----------------------
void MSEnabledTable::seqScanPos(uint8_t *pos)
{
	int32_t index = iEnabledIndex -1;
	if (index < 0)
		index = 0; // This is probably an error condition.
		
	mi_int4store(pos, index);
}

//-----------------------
void MSEnabledTable::seqScanRead(uint8_t *pos, char *buf)
{
	iEnabledIndex = mi_uint4korr(pos);
	seqScanNext(buf);
}



