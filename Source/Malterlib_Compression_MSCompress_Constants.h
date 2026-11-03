// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>

namespace NMib::NCompression
{
	// SZDD algorithm constants
	static constexpr umint gc_SZDDWindowSize = 4096; // Sliding window size in bytes
	static constexpr umint gc_SZDDMaxMatchLength = 16; // Maximum match length
	static constexpr umint gc_SZDDMinMatchLength = 3; // Minimum match length for back-reference
	static constexpr umint gc_SZDDHeaderSize = 14; // SZDD header size in bytes

	// SZDD header magic values
	static constexpr uint32 gc_SZDDMagicSig = 0x44445A53; // "SZDD" as little-endian uint32
	static constexpr uint32 gc_SZDDMagicConst = 0x3327F088;
	static constexpr uint8 gc_SZDDCompressionType = 0x41; // 'A'

	// KWAJ header magic (version 6.22 format, detected but not supported)
	static constexpr uint32 gc_KWAJMagicSig = 0x4A41574B; // "KWAJ" as little-endian uint32

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

		void f_Init();
		void f_UpdatePosition(uint8 _FirstByte);
		void f_FindMatch(uint8 const *_pLookahead, umint _nLookaheadLen, smint &o_MatchPosition, smint &o_MatchLength);
		void f_Advance();

	private:
		void fp_InsertHead(umint _iPos, smint _iBucket);
		void fp_Remove(umint _iPos, smint _iBucket);
	};
}
