// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Compression_MSCompress_Constants.h"

namespace NMib::NCompression
{
	void CSZDDMatchFinder::f_Init()
	{
		// Positions 0..N-F-1 are pre-filled with spaces; positions N-F..N-1
		// (the lookahead area) are zeroed. This matches expand.exe behaviour.
		for (umint iWindow = 0; iWindow < gc_SZDDWindowSize - gc_SZDDMaxMatchLength; ++iWindow)
			m_Window[iWindow] = 0x20;
		for (umint iWindow = gc_SZDDWindowSize - gc_SZDDMaxMatchLength; iWindow < gc_SZDDWindowSize; ++iWindow)
			m_Window[iWindow] = 0x00;
		m_iWinPos = gc_SZDDWindowSize - gc_SZDDMaxMatchLength;

		for (umint iWindow = 0; iWindow < gc_SZDDWindowSize; ++iWindow)
		{
			m_ChainNext[iWindow] = gc_SZDDChainNil;
			m_ChainPrev[iWindow] = gc_SZDDChainNil;
		}
		for (umint iBucket = 0; iBucket < gc_SZDDNumHashBuckets; ++iBucket)
			m_HashHead[iBucket] = gc_SZDDChainNil;

		// Pre-fill hash chains matching the initialized window contents.
		for (umint iWindow = 0; iWindow < gc_SZDDWindowSize; ++iWindow)
			fp_InsertHead(iWindow, smint(m_Window[iWindow]));
	}

	/// Remove the old entry at m_iWinPos from its hash chain, write the new byte
	/// data into the window, and insert m_iWinPos into the new byte's chain.
	void CSZDDMatchFinder::f_UpdatePosition(uint8 _FirstByte)
	{
		umint iPos = m_iWinPos;
		uint8 OldByte = m_Window[iPos];

		fp_Remove(iPos, smint(OldByte));
		m_Window[iPos] = _FirstByte;
		fp_InsertHead(iPos, smint(_FirstByte));
	}

	/// Find the longest match for _pLookahead in the window. Only positions
	/// whose first byte equals _pLookahead[0] are scanned. The match is limited
	/// so that it does not overlap with the write destination (m_iWinPos).
	void CSZDDMatchFinder::f_FindMatch(uint8 const *_pLookahead, umint _nLookaheadLen, smint &o_MatchPosition, smint &o_MatchLength)
	{
		o_MatchLength = 0;
		o_MatchPosition = 0;

		if (_nLookaheadLen == 0)
			return;

		smint iNode = m_HashHead[_pLookahead[0]];

		while (iNode != gc_SZDDChainNil)
		{
			umint iSearchPos = umint(iNode);

			if (iSearchPos != m_iWinPos) // Skip write position itself
			{
				// Limit match length by circular distance from source to write position.
				// The decompressor copies one byte at a time and writes at iWinPos, so if
				// the match source overlaps the write range, the decompressor reads bytes
				// already overwritten in earlier copy steps, producing wrong output.
				umint nDistToWrite = (m_iWinPos - iSearchPos + gc_SZDDWindowSize) & (gc_SZDDWindowSize - 1);
				umint nMaxMatch = fg_Min(_nLookaheadLen, nDistToWrite);

				// First byte is guaranteed to match (same hash bucket)
				umint nMatchLen = 1;
				while (nMatchLen < nMaxMatch)
				{
					if (m_Window[(iSearchPos + nMatchLen) & (gc_SZDDWindowSize - 1)] != _pLookahead[nMatchLen])
						break;
					++nMatchLen;
				}

				if (smint(nMatchLen) > o_MatchLength)
				{
					o_MatchPosition = iNode;
					o_MatchLength = smint(nMatchLen);
					if (nMatchLen == _nLookaheadLen)
						return; // Can't do better
				}
			}

			iNode = m_ChainNext[iNode];
		}
	}

	/// Advance the window position by one byte.
	void CSZDDMatchFinder::f_Advance()
	{
		m_iWinPos = (m_iWinPos + 1) & (gc_SZDDWindowSize - 1);
	}

	void CSZDDMatchFinder::fp_InsertHead(umint _iPos, smint _iBucket)
	{
		smint iOldHead = m_HashHead[_iBucket];
		m_ChainNext[_iPos] = iOldHead;
		m_ChainPrev[_iPos] = gc_SZDDChainNil;
		if (iOldHead != gc_SZDDChainNil)
			m_ChainPrev[iOldHead] = smint(_iPos);
		m_HashHead[_iBucket] = smint(_iPos);
	}

	void CSZDDMatchFinder::fp_Remove(umint _iPos, smint _iBucket)
	{
		smint iPrev = m_ChainPrev[_iPos];
		smint iNext = m_ChainNext[_iPos];

		if (iPrev == gc_SZDDChainNil)
			m_HashHead[_iBucket] = iNext; // Was head
		else
			m_ChainNext[iPrev] = iNext;

		if (iNext != gc_SZDDChainNil)
			m_ChainPrev[iNext] = iPrev;

		m_ChainNext[_iPos] = gc_SZDDChainNil;
		m_ChainPrev[_iPos] = gc_SZDDChainNil;
	}
}
