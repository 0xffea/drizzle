/* Copyright (c) 2010 PrimeBase Technologies GmbH, Germany
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
 * 2010-05-25
 *
 * PBMS daemon global parameters.
 *
 */

#ifndef __PARAMETERS_MS_H__
#define __PARAMETERS_MS_H__

class PBMSParameters {
	public:
	
	static uint32_t getPortNumber();

	static uint32_t getServerID();
	
	static uint64_t getRepoThreshold();

	static uint64_t getTempLogThreshold();
	
	static uint32_t getTempBlobTimeout();
	
	static uint32_t getGarbageThreshold();
	
	static uint32_t getMaxKeepAlive();
	
	static uint64_t getBackupDatabaseID();
	static void setBackupDatabaseID(uint64_t id);
	
	static const char *getDefaultMetaDataHeaders();
	
	static void blackListedDB(const char *db);

	static bool isBlackListedDB(const char *db);
	
	static bool isBLOBDatabase(const char *db);
	
	static bool isBLOBTable(const char *db, const char *table);

	static bool isPBMSEventsEnabled();
	
#ifdef DRIZZLED
	static int32_t getBeforeUptateEventPosition();
	
	static int32_t getBeforeInsertEventPosition();	
#endif
};

#endif // __PARAMETERS_MS_H__