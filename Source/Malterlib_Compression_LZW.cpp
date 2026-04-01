// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include "Malterlib_Compression_LZW.h"

namespace NMib::NCompression
{
	namespace
	{
		static constexpr umint gc_HeaderSize = sizeof(uint32) + sizeof(uint8);
		static constexpr umint gc_MinBitsPerCode = 9;
		static constexpr umint gc_MaxBitsPerCode = 20;
	}

	NContainer::CByteVector fg_CompressLZW(NContainer::CByteVector const &_Source, umint _nBitsPerCode)
	{
		if (_nBitsPerCode < gc_MinBitsPerCode || _nBitsPerCode > gc_MaxBitsPerCode)
			DMibError("LZW bits per code must be between 9 and 20");

		uint8 const *pSrc = _Source.f_GetArray();
		umint nSrcLen = _Source.f_GetLen();

		if (nSrcLen > TCLimitsInt<uint32>::mc_Max)
			DMibError("LZW compression input exceeds uint32 maximum length");

		if (nSrcLen == 0)
		{
			NContainer::CByteVector Dest;
			Dest.f_SetLen(gc_HeaderSize);
			NMemory::fg_MemClear(Dest.f_GetArray(), gc_HeaderSize);
			Dest.f_GetArray()[sizeof(uint32)] = uint8(_nBitsPerCode);
			return Dest;
		}

		umint nMaxCodes = umint(1) << _nBitsPerCode;

		// Hash table for dictionary: maps (prefix_code, byte) -> code
		// Open addressing with load factor <= 0.5
		umint nHashBits = 1;
		while ((umint(1) << nHashBits) < nMaxCodes * 2)
			nHashBits++;
		umint nHashSize = umint(1) << nHashBits;
		umint nHashMask = nHashSize - 1;

		NContainer::TCVector<uint32> HashKeys;
		NContainer::TCVector<uint32> HashCodes;
		NContainer::TCVector<uint8> HashOccupied;
		HashKeys.f_SetLen(nHashSize);
		HashCodes.f_SetLen(nHashSize);
		HashOccupied.f_SetLen(nHashSize);
		// Cache raw pointers — vectors never resize after this point
		uint32 *pHashKeys = HashKeys.f_GetArray();
		uint32 *pHashCodes = HashCodes.f_GetArray();
		uint8 *pHashOccupied = HashOccupied.f_GetArray();
		NMemory::fg_MemClear(pHashOccupied, nHashSize);

		auto fLookup = [&](uint32 _nPrefix, uint8 _nByte, uint32 &o_nCode) -> bool
			{
				uint32 nKey = (_nPrefix << 8) | _nByte;
				umint iSlot = umint(nKey * uint32(2654435769u)) >> (32 - nHashBits);
				iSlot &= nHashMask;
				while (pHashOccupied[iSlot])
				{
					if (pHashKeys[iSlot] == nKey)
					{
						o_nCode = pHashCodes[iSlot];
						return true;
					}
					iSlot = (iSlot + 1) & nHashMask;
				}
				return false;
			}
		;

		auto fInsert = [&](uint32 _nPrefix, uint8 _nByte, uint32 _nCode)
			{
				uint32 nKey = (_nPrefix << 8) | _nByte;
				umint iSlot = umint(nKey * uint32(2654435769u)) >> (32 - nHashBits);
				iSlot &= nHashMask;
				while (pHashOccupied[iSlot])
					iSlot = (iSlot + 1) & nHashMask;
				pHashKeys[iSlot] = nKey;
				pHashCodes[iSlot] = _nCode;
				pHashOccupied[iSlot] = 1;
			}
		;

		// Allocate output: header + worst case (one code per input byte) + uint64 write padding
		uint64 nAllocSize64 = uint64(gc_HeaderSize) + ((uint64(nSrcLen) * _nBitsPerCode + 7) >> 3) + sizeof(uint64);
		if (nAllocSize64 > TCLimitsInt<umint>::mc_Max)
			DMibError("LZW compression output exceeds maximum buffer length");
		umint nAllocSize = umint(nAllocSize64);

		NContainer::CByteVector Dest;
		Dest.f_SetLen(nAllocSize);
		uint8 *pDest = Dest.f_GetArray();
		NMemory::fg_MemClear(pDest, nAllocSize);

		// Write header: uint32 LE source length + uint8 bits per code
		{
			uint32 nLen = fg_ByteSwapLE(uint32(nSrcLen));
			NMemory::fg_MemCopy(pDest, &nLen, sizeof(nLen));
		}
		pDest[sizeof(uint32)] = uint8(_nBitsPerCode);

		uint64 iBitOffset = uint64(gc_HeaderSize) << 3;

		auto fWriteCode = [&](uint32 _nCode)
			{
				auto *pDestByte = pDest + umint(iBitOffset >> 3);
				uint64 Temp;
				NMemory::fg_MemCopy(&Temp, pDestByte, sizeof(Temp));
				Temp = fg_ByteSwapLE(Temp);
				Temp |= uint64(_nCode) << (iBitOffset & 7);
				Temp = fg_ByteSwapLE(Temp);
				NMemory::fg_MemCopy(pDestByte, &Temp, sizeof(Temp));
				iBitOffset += _nBitsPerCode;
			}
		;

		// Compress
		uint32 nNextCode = 256;
		uint32 nCurrentCode = pSrc[0];

		for (umint i = 1; i < nSrcLen; ++i)
		{
			uint8 nByte = pSrc[i];
			uint32 nChildCode;

			if (fLookup(nCurrentCode, nByte, nChildCode))
				nCurrentCode = nChildCode;
			else
			{
				fWriteCode(nCurrentCode);

				if (nNextCode < nMaxCodes)
				{
					fInsert(nCurrentCode, nByte, nNextCode);
					nNextCode++;
				}

				nCurrentCode = nByte;
			}
		}

		// Output final code
		fWriteCode(nCurrentCode);

		// Trim to actual size
		umint nBitstreamBits = iBitOffset - (uint64(gc_HeaderSize) << 3);
		umint nFinalSize = gc_HeaderSize + ((nBitstreamBits + 7) >> 3);
		Dest.f_SetLen(nFinalSize);

		return Dest;
	}

	NContainer::CByteVector fg_DecompressLZW(NContainer::CByteVector const &_Source, umint _nMaxDestinationLen)
	{
		uint8 const *pSrc = _Source.f_GetArray();
		umint nSrcLen = _Source.f_GetLen();

		auto fThrowMalformed = []
			{
				DMibError("Malformed LZW compressed data");
			}
		;
		auto fThrowTooLarge = []
			{
				DMibError("LZW decompression output exceeds maximum buffer length");
			}
		;

		if (nSrcLen < gc_HeaderSize)
			fThrowMalformed();

		// Read header
		uint32 nDesLenRaw;
		NMemory::fg_MemCopy(&nDesLenRaw, pSrc, sizeof(nDesLenRaw));
		umint nDesLen = fg_ByteSwapLE(nDesLenRaw);
		umint nBitsPerCode = pSrc[sizeof(uint32)];

		if (nBitsPerCode < gc_MinBitsPerCode || nBitsPerCode > gc_MaxBitsPerCode)
			fThrowMalformed();

		if (nDesLen == 0)
		{
			if (nSrcLen != gc_HeaderSize)
				fThrowMalformed();
			return {};
		}

		if (nDesLen > _nMaxDestinationLen)
			fThrowTooLarge();

		umint nMaxCodes = umint(1) << nBitsPerCode;
		uint32 nCodeMask = (uint32(1) << nBitsPerCode) - 1;

		// Dictionary entries for codes 256+
		struct CDictEntry
		{
			uint32 m_PrefixCode;
			uint8 m_Byte;
			uint8 m_FirstByte;
			umint m_Length;
		};

		NContainer::TCVector<CDictEntry> Dict;
		Dict.f_Reserve(fg_Min(nMaxCodes - 256, nDesLen));

		NContainer::CByteVector Dest;
		Dest.f_SetLen(nDesLen);
		uint8 *pDest = Dest.f_GetArray();

		uint64 iBitOffset = uint64(gc_HeaderSize) << 3;
		uint64 nSrcBits = uint64(nSrcLen) << 3;
		umint iDest = 0;

		auto fReadCode = [&]() -> uint32
			{
				if (iBitOffset + nBitsPerCode > nSrcBits)
					fThrowMalformed();
				umint iByte = umint(iBitOffset >> 3);
				uint64 Temp = 0;
				if (iByte < nSrcLen)
					NMemory::fg_MemCopy(&Temp, pSrc + iByte, fg_Min(sizeof(Temp), nSrcLen - iByte));
				Temp = fg_ByteSwapLE(Temp);
				uint32 nCode = uint32(Temp >> (iBitOffset & 7)) & nCodeMask;
				iBitOffset += nBitsPerCode;
				return nCode;
			}
		;

		auto fFirstByte = [&](uint32 _nCode) -> uint8
			{
				if (_nCode < 256)
					return uint8(_nCode);
				return Dict.f_GetArray()[_nCode - 256].m_FirstByte;
			}
		;

		auto fCodeLength = [&](uint32 _nCode) -> umint
			{
				if (_nCode < 256)
					return umint(1);
				return Dict.f_GetArray()[_nCode - 256].m_Length;
			}
		;

		auto fDecodeInto = [&](uint32 _nCode) -> umint
			{
				if (_nCode < 256)
				{
					if (iDest >= nDesLen)
						fThrowMalformed();
					pDest[iDest] = uint8(_nCode);
					return umint(1);
				}

				umint nDictIdx = _nCode - 256;
				if (nDictIdx >= Dict.f_GetLen())
					fThrowMalformed();

				CDictEntry const *pDict = Dict.f_GetArray();
				umint nLen = pDict[nDictIdx].m_Length;
				if (iDest + nLen > nDesLen)
					fThrowMalformed();

				// Walk chain backwards, writing bytes from end to start
				uint32 nCode = _nCode;
				umint iWrite = iDest + nLen;
				while (nCode >= 256)
				{
					CDictEntry const &Entry = pDict[nCode - 256];
					pDest[--iWrite] = Entry.m_Byte;
					nCode = Entry.m_PrefixCode;
				}
				pDest[--iWrite] = uint8(nCode);

				return nLen;
			}
		;

		auto fAddEntry = [&](uint32 _nPrefix, uint8 _nByte)
			{
				CDictEntry Entry;
				Entry.m_PrefixCode = _nPrefix;
				Entry.m_Byte = _nByte;
				Entry.m_FirstByte = fFirstByte(_nPrefix);
				Entry.m_Length = fCodeLength(_nPrefix) + 1;
				Dict.f_InsertLast(Entry);
			}
		;

		// Read and output first code (must be a literal byte)
		uint32 nCode = fReadCode();
		if (nCode >= 256)
			fThrowMalformed();
		pDest[iDest++] = uint8(nCode);

		uint32 nPrevCode = nCode;
		uint32 nNextCode = 256;

		while (iDest < nDesLen)
		{
			nCode = fReadCode();

			if (nCode < 256)
			{
				// Literal byte
				if (iDest >= nDesLen)
					fThrowMalformed();
				pDest[iDest] = uint8(nCode);

				if (nNextCode < nMaxCodes)
				{
					fAddEntry(nPrevCode, uint8(nCode));
					nNextCode++;
				}

				iDest += 1;
			}
			else if (nCode < nNextCode)
			{
				// Known dictionary entry
				if (nNextCode < nMaxCodes)
				{
					fAddEntry(nPrevCode, fFirstByte(nCode));
					nNextCode++;
				}

				umint nLen = fDecodeInto(nCode);
				iDest += nLen;
			}
			else if (nCode == nNextCode)
			{
				// Special case: code not yet in dictionary
				if (nNextCode >= nMaxCodes)
					fThrowMalformed();

				uint8 nFirstByte = fFirstByte(nPrevCode);
				fAddEntry(nPrevCode, nFirstByte);
				nNextCode++;

				umint nLen = fDecodeInto(nCode);
				iDest += nLen;
			}
			else
				fThrowMalformed();

			nPrevCode = nCode;
		}

		// Validate: no extra source data and trailing bits must be zero
		umint nBitstreamBitsUsed = iBitOffset - (uint64(gc_HeaderSize) << 3);
		umint nExpectedSrcLen = gc_HeaderSize + ((nBitstreamBitsUsed + 7) >> 3);
		if (nExpectedSrcLen != nSrcLen)
			fThrowMalformed();

		if ((nBitstreamBitsUsed & 7) != 0)
		{
			if ((pSrc[nSrcLen - 1] & uint8(0xFF << (nBitstreamBitsUsed & 7))) != 0)
				fThrowMalformed();
		}

		return Dest;
	}
}
