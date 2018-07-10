/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: key.c
 *
 * Purpose: Implement functions for 802.11i Key management
 *
 * Author: Jerry Chen
 *
 * Date: May 29, 2003
 *
 * Functions:
 *      KeyvInitTable - Init Key management table
 *      KeybGetKey - Get Key from table
 *      KeybSetKey - Set Key to table
 *      KeybRemoveKey - Remove Key from table
 *      KeybGetTransmitKey - Get Transmit Key from table
 *
 * Revision History:
 *
 */

#include "tmacro.h"
#include "key.h"
#include "mac.h"

/*---------------------  Static Definitions -------------------------*/

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Static Definitions -------------------------*/

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/

/*---------------------  Static Functions  --------------------------*/
static void
s_vCheckKeyTableValid(PSKeyManagement pTable, void __iomem *dwIoBase)
{
	int i;

	for (i = 0; i < MAX_KEY_TABLE; i++) {
		if (pTable->KeyTable[i].bInUse &&
		    !pTable->KeyTable[i].PairwiseKey.bKeyValid &&
		    !pTable->KeyTable[i].GroupKey[0].bKeyValid &&
		    !pTable->KeyTable[i].GroupKey[1].bKeyValid &&
		    !pTable->KeyTable[i].GroupKey[2].bKeyValid &&
		    !pTable->KeyTable[i].GroupKey[3].bKeyValid) {
			pTable->KeyTable[i].bInUse = false;
			pTable->KeyTable[i].wKeyCtl = 0;
			pTable->KeyTable[i].bSoftWEP = false;
			MACvDisableKeyEntry(dwIoBase, i);
		}
	}
}

/*---------------------  Export Functions  --------------------------*/

/*
 * Description: Init Key management table
 *
 * Parameters:
 *  In:
 *      pTable          - Pointer to Key table
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void KeyvInitTable(PSKeyManagement pTable, void __iomem *dwIoBase)
{
	int i;
	int jj;

	for (i = 0; i < MAX_KEY_TABLE; i++) {
		pTable->KeyTable[i].bInUse = false;
		pTable->KeyTable[i].PairwiseKey.bKeyValid = false;
		pTable->KeyTable[i].PairwiseKey.pvKeyTable = (void *)&pTable->KeyTable[i];
		for (jj = 0; jj < MAX_GROUP_KEY; jj++) {
			pTable->KeyTable[i].GroupKey[jj].bKeyValid = false;
			pTable->KeyTable[i].GroupKey[jj].pvKeyTable = (void *)&pTable->KeyTable[i];
		}
		pTable->KeyTable[i].wKeyCtl = 0;
		pTable->KeyTable[i].dwGTKeyIndex = 0;
		pTable->KeyTable[i].bSoftWEP = false;
		MACvDisableKeyEntry(dwIoBase, i);
	}
}

/*
 * Description: Get Key from table
 *
 * Parameters:
 *  In:
 *      pTable          - Pointer to Key table
 *      pbyBSSID        - BSSID of Key
 *      dwKeyIndex      - Key Index (0xFFFFFFFF means pairwise key)
 *  Out:
 *      pKey            - Key return
 *
 * Return Value: true if found otherwise false
 *
 */
bool KeybGetKey(
	PSKeyManagement pTable,
	unsigned char *pbyBSSID,
	unsigned long dwKeyIndex,
	PSKeyItem       *pKey
)
{
	int i;

	pr_debug("KeybGetKey()\n");

	*pKey = NULL;
	for (i = 0; i < MAX_KEY_TABLE; i++) {
		if (pTable->KeyTable[i].bInUse &&
		    ether_addr_equal(pTable->KeyTable[i].abyBSSID, pbyBSSID)) {
			if (dwKeyIndex == 0xFFFFFFFF) {
				if (pTable->KeyTable[i].PairwiseKey.bKeyValid) {
					*pKey = &(pTable->KeyTable[i].PairwiseKey);
					return true;
				} else {
					return false;
				}
			} else if (dwKeyIndex < MAX_GROUP_KEY) {
				if (pTable->KeyTable[i].GroupKey[dwKeyIndex].bKeyValid) {
					*pKey = &(pTable->KeyTable[i].GroupKey[dwKeyIndex]);
					return true;
				} else {
					return false;
				}
			} else {
				return false;
			}
		}
	}
	return false;
}

/*
 * Description: Set Key to table
 *
 * Parameters:
 *  In:
 *      pTable          - Pointer to Key table
 *      pbyBSSID        - BSSID of Key
 *      dwKeyIndex      - Key index (reference to NDIS DDK)
 *      uKeyLength      - Key length
 *      KeyRSC          - Key RSC
 *      pbyKey          - Pointer to key
 *  Out:
 *      none
 *
 * Return Value: true if success otherwise false
 *
 */
bool KeybSetKey(
	PSKeyManagement pTable,
	unsigned char *pbyBSSID,
	unsigned long dwKeyIndex,
	unsigned long uKeyLength,
	u64 *pKeyRSC,
	unsigned char *pbyKey,
	unsigned char byKeyDecMode,
	void __iomem *dwIoBase,
	unsigned char byLocalID
)
{
	int i, j;
	unsigned int ii;
	PSKeyItem   pKey;
	unsigned int uKeyIdx;

	pr_debug("Enter KeybSetKey: %lX\n", dwKeyIndex);

	j = (MAX_KEY_TABLE-1);
	for (i = 0; i < (MAX_KEY_TABLE - 1); i++) {
		if (!pTable->KeyTable[i].bInUse && (j == (MAX_KEY_TABLE-1))) {
			// found empty table
			j = i;
		}
		if (pTable->KeyTable[i].bInUse &&
		    ether_addr_equal(pTable->KeyTable[i].abyBSSID, pbyBSSID)) {
			// found table already exist
			if ((dwKeyIndex & PAIRWISE_KEY) != 0) {
				// Pairwise key
				pKey = &(pTable->KeyTable[i].PairwiseKey);
				pTable->KeyTable[i].wKeyCtl &= 0xFFF0;          // clear pairwise key control filed
				pTable->KeyTable[i].wKeyCtl |= byKeyDecMode;
				uKeyIdx = 4;                                    // use HW key entry 4 for pairwise key
			} else {
				// Group key
				if ((dwKeyIndex & 0x000000FF) >= MAX_GROUP_KEY)
					return false;
				pKey = &(pTable->KeyTable[i].GroupKey[dwKeyIndex & 0x000000FF]);
				if ((dwKeyIndex & TRANSMIT_KEY) != 0)  {
					// Group transmit key
					pTable->KeyTable[i].dwGTKeyIndex = dwKeyIndex;
					pr_debug("Group transmit key(R)[%lX]: %d\n",
						 pTable->KeyTable[i].dwGTKeyIndex, i);
				}
				pTable->KeyTable[i].wKeyCtl &= 0xFF0F;          // clear group key control filed
				pTable->KeyTable[i].wKeyCtl |= (byKeyDecMode << 4);
				pTable->KeyTable[i].wKeyCtl |= 0x0040;          // use group key for group address
				uKeyIdx = (dwKeyIndex & 0x000000FF);
			}
			pTable->KeyTable[i].wKeyCtl |= 0x8000;              // enable on-fly

			pKey->bKeyValid = true;
			pKey->uKeyLength = uKeyLength;
			pKey->dwKeyIndex = dwKeyIndex;
			pKey->byCipherSuite = byKeyDecMode;
			memcpy(pKey->abyKey, pbyKey, uKeyLength);
			if (byKeyDecMode == KEY_CTL_WEP) {
				if (uKeyLength == WLAN_WEP40_KEYLEN)
					pKey->abyKey[15] &= 0x7F;
				if (uKeyLength == WLAN_WEP104_KEYLEN)
					pKey->abyKey[15] |= 0x80;
			}
			MACvSetKeyEntry(dwIoBase, pTable->KeyTable[i].wKeyCtl, i, uKeyIdx, pbyBSSID, (u32 *)pKey->abyKey, byLocalID);

			if ((dwKeyIndex & USE_KEYRSC) == 0) {
				// RSC set by NIC
				pKey->KeyRSC = 0;
			} else {
				pKey->KeyRSC = *pKeyRSC;
			}
			pKey->dwTSC47_16 = 0;
			pKey->wTSC15_0 = 0;

			pr_debug("KeybSetKey(R):\n");
			pr_debug("pKey->bKeyValid: %d\n ", pKey->bKeyValid);
			pr_debug("pKey->abyKey: ");
			for (ii = 0; ii < pKey->uKeyLength; ii++)
				pr_debug("%02x ", pKey->abyKey[ii]);

			pr_debug("\n");

			pr_debug("pKey->dwTSC47_16: %lx\n ", pKey->dwTSC47_16);
			pr_debug("pKey->wTSC15_0: %x\n ", pKey->wTSC15_0);
			pr_debug("pKey->dwKeyIndex: %lx\n ", pKey->dwKeyIndex);

			return true;
		}
	}
	if (j < (MAX_KEY_TABLE-1)) {
		memcpy(pTable->KeyTable[j].abyBSSID, pbyBSSID, ETH_ALEN);
		pTable->KeyTable[j].bInUse = true;
		if ((dwKeyIndex & PAIRWISE_KEY) != 0)  {
			// Pairwise key
			pKey = &(pTable->KeyTable[j].PairwiseKey);
			pTable->KeyTable[j].wKeyCtl &= 0xFFF0;          // clear pairwise key control filed
			pTable->KeyTable[j].wKeyCtl |= byKeyDecMode;
			uKeyIdx = 4;                                    // use HW key entry 4 for pairwise key
		} else {
			// Group key
			if ((dwKeyIndex & 0x000000FF) >= MAX_GROUP_KEY)
				return false;
			pKey = &(pTable->KeyTable[j].GroupKey[dwKeyIndex & 0x000000FF]);
			if ((dwKeyIndex & TRANSMIT_KEY) != 0)  {
				// Group transmit key
				pTable->KeyTable[j].dwGTKeyIndex = dwKeyIndex;
				pr_debug("Group transmit key(N)[%lX]: %d\n",
					 pTable->KeyTable[j].dwGTKeyIndex, j);
			}
			pTable->KeyTable[j].wKeyCtl &= 0xFF0F;          // clear group key control filed
			pTable->KeyTable[j].wKeyCtl |= (byKeyDecMode << 4);
			pTable->KeyTable[j].wKeyCtl |= 0x0040;          // use group key for group address
			uKeyIdx = (dwKeyIndex & 0x000000FF);
		}
		pTable->KeyTable[j].wKeyCtl |= 0x8000;              // enable on-fly

		pKey->bKeyValid = true;
		pKey->uKeyLength = uKeyLength;
		pKey->dwKeyIndex = dwKeyIndex;
		pKey->byCipherSuite = byKeyDecMode;
		memcpy(pKey->abyKey, pbyKey, uKeyLength);
		if (byKeyDecMode == KEY_CTL_WEP) {
			if (uKeyLength == WLAN_WEP40_KEYLEN)
				pKey->abyKey[15] &= 0x7F;
			if (uKeyLength == WLAN_WEP104_KEYLEN)
				pKey->abyKey[15] |= 0x80;
		}
		MACvSetKeyEntry(dwIoBase, pTable->KeyTable[j].wKeyCtl, j, uKeyIdx, pbyBSSID, (u32 *)pKey->abyKey, byLocalID);

		if ((dwKeyIndex & USE_KEYRSC) == 0) {
			// RSC set by NIC
			pKey->KeyRSC = 0;
		} else {
			pKey->KeyRSC = *pKeyRSC;
		}
		pKey->dwTSC47_16 = 0;
		pKey->wTSC15_0 = 0;

		pr_debug("KeybSetKey(N):\n");
		pr_debug("pKey->bKeyValid: %d\n ", pKey->bKeyValid);
		pr_debug("pKey->uKeyLength: %d\n ", (int)pKey->uKeyLength);
		pr_debug("pKey->abyKey: ");
		for (ii = 0; ii < pKey->uKeyLength; ii++)
			pr_debug("%02x ", pKey->abyKey[ii]);

		pr_debug("\n");

		pr_debug("pKey->dwTSC47_16: %lx\n ", pKey->dwTSC47_16);
		pr_debug("pKey->wTSC15_0: %x\n ", pKey->wTSC15_0);
		pr_debug("pKey->dwKeyIndex: %lx\n ", pKey->dwKeyIndex);

		return true;
	}
	return false;
}

/*
 * Description: Remove Key from table
 *
 * Parameters:
 *  In:
 *      pTable          - Pointer to Key table
 *      pbyBSSID        - BSSID of Key
 *      dwKeyIndex      - Key Index (reference to NDIS DDK)
 *  Out:
 *      none
 *
 * Return Value: true if success otherwise false
 *
 */
bool KeybRemoveKey(
	PSKeyManagement pTable,
	unsigned char *pbyBSSID,
	unsigned long dwKeyIndex,
	void __iomem *dwIoBase
)
{
	int  i;

	if (is_broadcast_ether_addr(pbyBSSID)) {
		// delete all keys
		if ((dwKeyIndex & PAIRWISE_KEY) != 0) {
			for (i = 0; i < MAX_KEY_TABLE; i++)
				pTable->KeyTable[i].PairwiseKey.bKeyValid = false;

			s_vCheckKeyTableValid(pTable, dwIoBase);
			return true;
		} else if ((dwKeyIndex & 0x000000FF) < MAX_GROUP_KEY) {
			for (i = 0; i < MAX_KEY_TABLE; i++) {
				pTable->KeyTable[i].GroupKey[dwKeyIndex & 0x000000FF].bKeyValid = false;
				if ((dwKeyIndex & 0x7FFFFFFF) == (pTable->KeyTable[i].dwGTKeyIndex & 0x7FFFFFFF)) {
					// remove Group transmit key
					pTable->KeyTable[i].dwGTKeyIndex = 0;
				}
			}
			s_vCheckKeyTableValid(pTable, dwIoBase);
			return true;
		} else {
			return false;
		}
	}

	for (i = 0; i < MAX_KEY_TABLE; i++) {
		if (pTable->KeyTable[i].bInUse &&
		    ether_addr_equal(pTable->KeyTable[i].abyBSSID, pbyBSSID)) {
			if ((dwKeyIndex & PAIRWISE_KEY) != 0) {
				pTable->KeyTable[i].PairwiseKey.bKeyValid = false;
				s_vCheckKeyTableValid(pTable, dwIoBase);
				return true;
			} else if ((dwKeyIndex & 0x000000FF) < MAX_GROUP_KEY) {
				pTable->KeyTable[i].GroupKey[dwKeyIndex & 0x000000FF].bKeyValid = false;
				if ((dwKeyIndex & 0x7FFFFFFF) == (pTable->KeyTable[i].dwGTKeyIndex & 0x7FFFFFFF)) {
					// remove Group transmit key
					pTable->KeyTable[i].dwGTKeyIndex = 0;
				}
				s_vCheckKeyTableValid(pTable, dwIoBase);
				return true;
			} else {
				return false;
			}
		}
	}
	return false;
}

/*
 * Description: Remove Key from table
 *
 * Parameters:
 *  In:
 *      pTable          - Pointer to Key table
 *      pbyBSSID        - BSSID of Key
 *  Out:
 *      none
 *
 * Return Value: true if success otherwise false
 *
 */
bool KeybRemoveAllKey(
	PSKeyManagement pTable,
	unsigned char *pbyBSSID,
	void __iomem *dwIoBase
)
{
	int i, u;

	for (i = 0; i < MAX_KEY_TABLE; i++) {
		if (pTable->KeyTable[i].bInUse &&
		    ether_addr_equal(pTable->KeyTable[i].abyBSSID, pbyBSSID)) {
			pTable->KeyTable[i].PairwiseKey.bKeyValid = false;
			for (u = 0; u < MAX_GROUP_KEY; u++)
				pTable->KeyTable[i].GroupKey[u].bKeyValid = false;

			pTable->KeyTable[i].dwGTKeyIndex = 0;
			s_vCheckKeyTableValid(pTable, dwIoBase);
			return true;
		}
	}
	return false;
}

/*
 * Description: Remove WEP Key from table
 *
 * Parameters:
 *  In:
 *      pTable          - Pointer to Key table
 *  Out:
 *      none
 *
 * Return Value: true if success otherwise false
 *
 */
void KeyvRemoveWEPKey(
	PSKeyManagement pTable,
	unsigned long dwKeyIndex,
	void __iomem *dwIoBase
)
{
	if ((dwKeyIndex & 0x000000FF) < MAX_GROUP_KEY) {
		if (pTable->KeyTable[MAX_KEY_TABLE-1].bInUse) {
			if (pTable->KeyTable[MAX_KEY_TABLE-1].GroupKey[dwKeyIndex & 0x000000FF].byCipherSuite == KEY_CTL_WEP) {
				pTable->KeyTable[MAX_KEY_TABLE-1].GroupKey[dwKeyIndex & 0x000000FF].bKeyValid = false;
				if ((dwKeyIndex & 0x7FFFFFFF) == (pTable->KeyTable[MAX_KEY_TABLE-1].dwGTKeyIndex & 0x7FFFFFFF)) {
					// remove Group transmit key
					pTable->KeyTable[MAX_KEY_TABLE-1].dwGTKeyIndex = 0;
				}
			}
		}
		s_vCheckKeyTableValid(pTable, dwIoBase);
	}
}

void KeyvRemoveAllWEPKey(
	PSKeyManagement pTable,
	void __iomem *dwIoBase
)
{
	int i;

	for (i = 0; i < MAX_GROUP_KEY; i++)
		KeyvRemoveWEPKey(pTable, i, dwIoBase);
}

/*
 * Description: Get Transmit Key from table
 *
 * Parameters:
 *  In:
 *      pTable          - Pointer to Key table
 *      pbyBSSID        - BSSID of Key
 *  Out:
 *      pKey            - Key return
 *
 * Return Value: true if found otherwise false
 *
 */
bool KeybGetTransmitKey(
	PSKeyManagement pTable,
	unsigned char *pbyBSSID,
	unsigned long dwKeyType,
	PSKeyItem       *pKey
)
{
	int i, ii;

	*pKey = NULL;
	for (i = 0; i < MAX_KEY_TABLE; i++) {
		if (pTable->KeyTable[i].bInUse &&
		    ether_addr_equal(pTable->KeyTable[i].abyBSSID, pbyBSSID)) {
			if (dwKeyType == PAIRWISE_KEY) {
				if (pTable->KeyTable[i].PairwiseKey.bKeyValid) {
					*pKey = &(pTable->KeyTable[i].PairwiseKey);

					pr_debug("KeybGetTransmitKey:");
					pr_debug("PAIRWISE_KEY: KeyTable.abyBSSID: ");
					for (ii = 0; ii < 6; ii++)
						pr_debug("%x ",
							 pTable->KeyTable[i].abyBSSID[ii]);

					pr_debug("\n");

					return true;
				} else {
					pr_debug("PairwiseKey.bKeyValid == false\n");
					return false;
				}
			} // End of Type == PAIRWISE
			else {
				if (pTable->KeyTable[i].dwGTKeyIndex == 0) {
					pr_debug("ERROR: dwGTKeyIndex == 0 !!!\n");
					return false;
				}
				if (pTable->KeyTable[i].GroupKey[(pTable->KeyTable[i].dwGTKeyIndex&0x000000FF)].bKeyValid) {
					*pKey = &(pTable->KeyTable[i].GroupKey[(pTable->KeyTable[i].dwGTKeyIndex&0x000000FF)]);

					pr_debug("KeybGetTransmitKey:");
					pr_debug("GROUP_KEY: KeyTable.abyBSSID\n");
					for (ii = 0; ii < 6; ii++)
						pr_debug("%x ",
							 pTable->KeyTable[i].abyBSSID[ii]);

					pr_debug("\n");
					pr_debug("dwGTKeyIndex: %lX\n",
						 pTable->KeyTable[i].dwGTKeyIndex);

					return true;
				} else {
					pr_debug("GroupKey.bKeyValid == false\n");
					return false;
				}
			} // End of Type = GROUP
		} // BSSID match
	}
	pr_debug("ERROR: NO Match BSSID !!! ");
	for (ii = 0; ii < 6; ii++)
		pr_debug("%02x ", *(pbyBSSID+ii));

	pr_debug("\n");
	return false;
}

/*
 * Description: Check Pairewise Key
 *
 * Parameters:
 *  In:
 *      pTable          - Pointer to Key table
 *  Out:
 *      none
 *
 * Return Value: true if found otherwise false
 *
 */
bool KeybCheckPairewiseKey(
	PSKeyManagement pTable,
	PSKeyItem       *pKey
)
{
	int i;

	*pKey = NULL;
	for (i = 0; i < MAX_KEY_TABLE; i++) {
		if (pTable->KeyTable[i].bInUse &&
		    pTable->KeyTable[i].PairwiseKey.bKeyValid) {
			*pKey = &(pTable->KeyTable[i].PairwiseKey);
			return true;
		}
	}
	return false;
}

/*
 * Description: Set Key to table
 *
 * Parameters:
 *  In:
 *      pTable          - Pointer to Key table
 *      dwKeyIndex      - Key index (reference to NDIS DDK)
 *      uKeyLength      - Key length
 *      KeyRSC          - Key RSC
 *      pbyKey          - Pointer to key
 *  Out:
 *      none
 *
 * Return Value: true if success otherwise false
 *
 */
bool KeybSetDefaultKey(
	PSKeyManagement pTable,
	unsigned long dwKeyIndex,
	unsigned long uKeyLength,
	u64 *pKeyRSC,
	unsigned char *pbyKey,
	unsigned char byKeyDecMode,
	void __iomem *dwIoBase,
	unsigned char byLocalID
)
{
	unsigned int ii;
	PSKeyItem   pKey;
	unsigned int uKeyIdx;

	pr_debug("Enter KeybSetDefaultKey: %1x, %d\n",
		 (int)dwKeyIndex, (int)uKeyLength);

	if ((dwKeyIndex & PAIRWISE_KEY) != 0) // Pairwise key
		return false;
	else if ((dwKeyIndex & 0x000000FF) >= MAX_GROUP_KEY)
		return false;

	if (uKeyLength > MAX_KEY_LEN)
		return false;

	pTable->KeyTable[MAX_KEY_TABLE - 1].bInUse = true;
	for (ii = 0; ii < ETH_ALEN; ii++)
		pTable->KeyTable[MAX_KEY_TABLE - 1].abyBSSID[ii] = 0xFF;

	// Group key
	pKey = &(pTable->KeyTable[MAX_KEY_TABLE - 1].GroupKey[dwKeyIndex & 0x000000FF]);
	if ((dwKeyIndex & TRANSMIT_KEY) != 0)  {
		// Group transmit key
		pTable->KeyTable[MAX_KEY_TABLE-1].dwGTKeyIndex = dwKeyIndex;
		pr_debug("Group transmit key(R)[%lX]: %d\n",
			 pTable->KeyTable[MAX_KEY_TABLE-1].dwGTKeyIndex,
			 MAX_KEY_TABLE-1);

	}
	pTable->KeyTable[MAX_KEY_TABLE-1].wKeyCtl &= 0x7F00;          // clear all key control filed
	pTable->KeyTable[MAX_KEY_TABLE-1].wKeyCtl |= (byKeyDecMode << 4);
	pTable->KeyTable[MAX_KEY_TABLE-1].wKeyCtl |= (byKeyDecMode);
	pTable->KeyTable[MAX_KEY_TABLE-1].wKeyCtl |= 0x0044;          // use group key for all address
	uKeyIdx = (dwKeyIndex & 0x000000FF);

	if ((uKeyLength == WLAN_WEP232_KEYLEN) &&
	    (byKeyDecMode == KEY_CTL_WEP)) {
		pTable->KeyTable[MAX_KEY_TABLE-1].wKeyCtl |= 0x4000;              // disable on-fly disable address match
		pTable->KeyTable[MAX_KEY_TABLE-1].bSoftWEP = true;
	} else {
		if (!pTable->KeyTable[MAX_KEY_TABLE-1].bSoftWEP)
			pTable->KeyTable[MAX_KEY_TABLE-1].wKeyCtl |= 0xC000;          // enable on-fly disable address match
	}

	pKey->bKeyValid = true;
	pKey->uKeyLength = uKeyLength;
	pKey->dwKeyIndex = dwKeyIndex;
	pKey->byCipherSuite = byKeyDecMode;
	memcpy(pKey->abyKey, pbyKey, uKeyLength);
	if (byKeyDecMode == KEY_CTL_WEP) {
		if (uKeyLength == WLAN_WEP40_KEYLEN)
			pKey->abyKey[15] &= 0x7F;
		if (uKeyLength == WLAN_WEP104_KEYLEN)
			pKey->abyKey[15] |= 0x80;
	}
	MACvSetKeyEntry(dwIoBase, pTable->KeyTable[MAX_KEY_TABLE-1].wKeyCtl, MAX_KEY_TABLE-1, uKeyIdx, pTable->KeyTable[MAX_KEY_TABLE-1].abyBSSID, (u32 *)pKey->abyKey, byLocalID);

	if ((dwKeyIndex & USE_KEYRSC) == 0) {
		// RSC set by NIC
		pKey->KeyRSC = 0;
	} else {
		pKey->KeyRSC = *pKeyRSC;
	}
	pKey->dwTSC47_16 = 0;
	pKey->wTSC15_0 = 0;

	pr_debug("KeybSetKey(R):\n");
	pr_debug("pKey->bKeyValid: %d\n", pKey->bKeyValid);
	pr_debug("pKey->uKeyLength: %d\n", (int)pKey->uKeyLength);
	pr_debug("pKey->abyKey:\n");
	for (ii = 0; ii < pKey->uKeyLength; ii++)
		pr_debug("%x", pKey->abyKey[ii]);

	pr_debug("\n");

	pr_debug("pKey->dwTSC47_16: %lx\n", pKey->dwTSC47_16);
	pr_debug("pKey->wTSC15_0: %x\n", pKey->wTSC15_0);
	pr_debug("pKey->dwKeyIndex: %lx\n", pKey->dwKeyIndex);

	return true;
}

/*
 * Description: Set Key to table
 *
 * Parameters:
 *  In:
 *      pTable          - Pointer to Key table
 *      dwKeyIndex      - Key index (reference to NDIS DDK)
 *      uKeyLength      - Key length
 *      KeyRSC          - Key RSC
 *      pbyKey          - Pointer to key
 *  Out:
 *      none
 *
 * Return Value: true if success otherwise false
 *
 */
bool KeybSetAllGroupKey(
	PSKeyManagement pTable,
	unsigned long dwKeyIndex,
	unsigned long uKeyLength,
	u64 *pKeyRSC,
	unsigned char *pbyKey,
	unsigned char byKeyDecMode,
	void __iomem *dwIoBase,
	unsigned char byLocalID
)
{
	int         i;
	unsigned int ii;
	PSKeyItem   pKey;
	unsigned int uKeyIdx;

	pr_debug("Enter KeybSetAllGroupKey: %lX\n", dwKeyIndex);

	if ((dwKeyIndex & PAIRWISE_KEY) != 0) // Pairwise key
		return false;
	else if ((dwKeyIndex & 0x000000FF) >= MAX_GROUP_KEY)
		return false;

	for (i = 0; i < MAX_KEY_TABLE - 1; i++) {
		if (pTable->KeyTable[i].bInUse) {
			// found table already exist
			// Group key
			pKey = &(pTable->KeyTable[i].GroupKey[dwKeyIndex & 0x000000FF]);
			if ((dwKeyIndex & TRANSMIT_KEY) != 0)  {
				// Group transmit key
				pTable->KeyTable[i].dwGTKeyIndex = dwKeyIndex;
				pr_debug("Group transmit key(R)[%lX]: %d\n",
					 pTable->KeyTable[i].dwGTKeyIndex, i);

			}
			pTable->KeyTable[i].wKeyCtl &= 0xFF0F;          // clear group key control filed
			pTable->KeyTable[i].wKeyCtl |= (byKeyDecMode << 4);
			pTable->KeyTable[i].wKeyCtl |= 0x0040;          // use group key for group address
			uKeyIdx = (dwKeyIndex & 0x000000FF);

			pTable->KeyTable[i].wKeyCtl |= 0x8000;              // enable on-fly

			pKey->bKeyValid = true;
			pKey->uKeyLength = uKeyLength;
			pKey->dwKeyIndex = dwKeyIndex;
			pKey->byCipherSuite = byKeyDecMode;
			memcpy(pKey->abyKey, pbyKey, uKeyLength);
			if (byKeyDecMode == KEY_CTL_WEP) {
				if (uKeyLength == WLAN_WEP40_KEYLEN)
					pKey->abyKey[15] &= 0x7F;
				if (uKeyLength == WLAN_WEP104_KEYLEN)
					pKey->abyKey[15] |= 0x80;
			}
			MACvSetKeyEntry(dwIoBase, pTable->KeyTable[i].wKeyCtl, i, uKeyIdx, pTable->KeyTable[i].abyBSSID, (u32 *)pKey->abyKey, byLocalID);

			if ((dwKeyIndex & USE_KEYRSC) == 0) {
				// RSC set by NIC
				pKey->KeyRSC = 0;
			} else {
				pKey->KeyRSC = *pKeyRSC;
			}
			pKey->dwTSC47_16 = 0;
			pKey->wTSC15_0 = 0;

			pr_debug("KeybSetKey(R):\n");
			pr_debug("pKey->bKeyValid: %d\n ", pKey->bKeyValid);
			pr_debug("pKey->uKeyLength: %d\n ",
				 (int)pKey->uKeyLength);
			pr_debug("pKey->abyKey: ");
			for (ii = 0; ii < pKey->uKeyLength; ii++)
				pr_debug("%02x ", pKey->abyKey[ii]);

			pr_debug("\n");

		} // (pTable->KeyTable[i].bInUse == true)
	}
	return true;
}
