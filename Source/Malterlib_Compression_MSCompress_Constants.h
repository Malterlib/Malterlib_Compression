// Copyright Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>

namespace NMib::NCompression
{
	// SZDD algorithm constants
	static constexpr umint gc_SZDDWindowSize = 4096;      // Sliding window size in bytes
	static constexpr umint gc_SZDDMaxMatchLength = 16;     // Maximum match length
	static constexpr umint gc_SZDDMinMatchLength = 3;      // Minimum match length for back-reference
	static constexpr umint gc_SZDDHeaderSize = 14;         // SZDD header size in bytes

	// SZDD header magic values
	static constexpr uint32 gc_SZDDMagicSig = 0x44445A53;    // "SZDD" as little-endian uint32
	static constexpr uint32 gc_SZDDMagicConst = 0x3327F088;
	static constexpr uint8 gc_SZDDCompressionType = 0x41;     // 'A'

	// KWAJ header magic (version 6.22 format, detected but not supported)
	static constexpr uint32 gc_KWAJMagicSig = 0x4A41574B;    // "KWAJ" as little-endian uint32

	// Maximum bytes a single SZDD block can consume from compressed input: 1 control + 8 * 2 back-ref = 17
	static constexpr umint gc_SZDDMaxBlockSize = 17;

	static constexpr umint gc_SZDDNumHashBuckets = 256;
	static constexpr smint gc_SZDDChainNil = -1;

	/// Hash-chain indexed sliding window for LZSS match finding.
	/// Maintains one linked list per first-byte value (256 buckets). For each
	/// position being compressed, only positions sharing the same first byte are
	/// scanned. Average chain length is gc_SZDDWindowSize / 256 = 16, so the
	/// search is O(ChainLen * MaxMatchLength) per symbol instead of the
	/// O(WindowSize * MaxMatchLength) of a plain linear scan.
	struct CSZDDMatchFinder
	{
		uint8 m_Window[gc_SZDDWindowSize];
		umint m_iWinPos;

		// Doubly-linked hash chains for O(1) removal
		smint m_ChainNext[gc_SZDDWindowSize];
		smint m_ChainPrev[gc_SZDDWindowSize]; // gc_SZDDChainNil = head of chain
		smint m_HashHead[gc_SZDDNumHashBuckets];

		void f_Init()
		{
			// Positions 0..N-F-1 are pre-filled with spaces; positions N-F..N-1
			// (the lookahead area) are zeroed. This matches expand.exe behaviour.
			for (umint i = 0; i < gc_SZDDWindowSize - gc_SZDDMaxMatchLength; ++i)
				m_Window[i] = 0x20;
			for (umint i = gc_SZDDWindowSize - gc_SZDDMaxMatchLength; i < gc_SZDDWindowSize; ++i)
				m_Window[i] = 0x00;
			m_iWinPos = gc_SZDDWindowSize - gc_SZDDMaxMatchLength;

			for (umint i = 0; i < gc_SZDDWindowSize; ++i)
			{
				m_ChainNext[i] = gc_SZDDChainNil;
				m_ChainPrev[i] = gc_SZDDChainNil;
			}
			for (umint i = 0; i < gc_SZDDNumHashBuckets; ++i)
				m_HashHead[i] = gc_SZDDChainNil;

			// Pre-fill hash chains matching the initialized window contents.
			for (umint i = 0; i < gc_SZDDWindowSize; ++i)
				fp_InsertHead(i, smint(m_Window[i]));
		}

		/// Remove the old entry at m_iWinPos from its hash chain, write the new byte
		/// data into the window, and insert m_iWinPos into the new byte's chain.
		void f_UpdatePosition(uint8 _FirstByte)
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
		void f_FindMatch(uint8 const *_pLookahead, umint _nLookaheadLen, smint &o_MatchPosition, smint &o_MatchLength)
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
		void f_Advance()
		{
			m_iWinPos = (m_iWinPos + 1) & (gc_SZDDWindowSize - 1);
		}

	private:
		void fp_InsertHead(umint _iPos, smint _iBucket)
		{
			smint iOldHead = m_HashHead[_iBucket];
			m_ChainNext[_iPos] = iOldHead;
			m_ChainPrev[_iPos] = gc_SZDDChainNil;
			if (iOldHead != gc_SZDDChainNil)
				m_ChainPrev[iOldHead] = smint(_iPos);
			m_HashHead[_iBucket] = smint(_iPos);
		}

		void fp_Remove(umint _iPos, smint _iBucket)
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
	};
}
