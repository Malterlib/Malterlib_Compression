// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include "Malterlib_Compression_MSCompress.h"
#include "Malterlib_Compression_MSCompress_Constants.h"

namespace NMib::NCompression
{
	namespace
	{

		void fsg_WriteSZDDHeader(uint8 *_pOutput, uint32 _OriginalSize, uint8 _FilenameHint)
		{
			uint32 Magic = fg_ByteSwapLE(gc_SZDDMagicSig);
			NMemory::fg_MemCopy(_pOutput + 0, &Magic, 4);

			uint32 MagicConst = fg_ByteSwapLE(gc_SZDDMagicConst);
			NMemory::fg_MemCopy(_pOutput + 4, &MagicConst, 4);

			_pOutput[8] = gc_SZDDCompressionType;
			_pOutput[9] = _FilenameHint;

			uint32 OrigSize = fg_ByteSwapLE(_OriginalSize);
			NMemory::fg_MemCopy(_pOutput + 10, &OrigSize, 4);
		}

		struct CSZDDHeader
		{
			uint32 m_MagicSig;
			uint32 m_MagicConst;
			uint8 m_CompressionType;
			uint8 m_FilenameHint;
			uint32 m_OriginalSize;
		};

		CSZDDHeader fsg_ReadSZDDHeader(uint8 const *_pInput, umint _InputLen)
		{
			using namespace NStr;

			if (_InputLen < gc_SZDDHeaderSize)
				DMibError("Not a valid MS-compressed file: file too small");

			CSZDDHeader Header;

			NMemory::fg_MemCopy(&Header.m_MagicSig, _pInput + 0, 4);
			Header.m_MagicSig = fg_ByteSwapLE(Header.m_MagicSig);

			if (Header.m_MagicSig == gc_KWAJMagicSig)
				DMibError("MS-compressed file uses KWAJ format (version 6.22) which is not supported");

			if (Header.m_MagicSig != gc_SZDDMagicSig)
				DMibError("Not a valid MS-compressed file: invalid SZDD signature");

			NMemory::fg_MemCopy(&Header.m_MagicConst, _pInput + 4, 4);
			Header.m_MagicConst = fg_ByteSwapLE(Header.m_MagicConst);

			if (Header.m_MagicConst != gc_SZDDMagicConst)
				DMibError("Not a valid MS-compressed file: invalid magic constant");

			Header.m_CompressionType = _pInput[8];
			if (Header.m_CompressionType != gc_SZDDCompressionType)
				DMibError("Not a valid MS-compressed file: unsupported compression type 0x{nh}"_f << uint32(Header.m_CompressionType));

			Header.m_FilenameHint = _pInput[9];

			NMemory::fg_MemCopy(&Header.m_OriginalSize, _pInput + 10, 4);
			Header.m_OriginalSize = fg_ByteSwapLE(Header.m_OriginalSize);

			return Header;
		}
	}

	uint8 fg_MSCompressFilenameHint(NStr::CStr const &_Filename)
	{
		if (_Filename.f_GetLen() == 0)
			return 0x00;
		return uint8(_Filename[_Filename.f_GetLen() - 1]);
	}

	uint8 fg_MSCompressReadFilenameHint(NContainer::CByteVector const &_CompressedData)
	{
		using namespace NStr;

		fsg_ReadSZDDHeader(_CompressedData.f_GetArray(), _CompressedData.f_GetLen());
		return _CompressedData[9];
	}

	NContainer::CByteVector fg_CompressMSCompress(NContainer::CByteVector const &_Source, CMSCompressOptions _Options)
	{
		using namespace NStr;

		umint nSourceLen = _Source.f_GetLen();

		if (nSourceLen > TCLimitsInt<uint32>::mc_Max)
			DMibError("MSCompress input exceeds maximum 4 GB file size");

		// Worst case: header + every byte is a literal
		// Each block: 1 control byte + 8 literal bytes = 9 bytes per 8 symbols
		// Use uint64 to avoid overflow on 32-bit platforms
		uint64 nMaxCompressed64 = uint64(gc_SZDDHeaderSize) + (uint64(nSourceLen / 8) + 2) * 9;
		if (nMaxCompressed64 > TCLimitsInt<umint>::mc_Max)
			DMibError("MSCompress input too large for worst-case output buffer on this platform");
		umint nMaxCompressed = umint(nMaxCompressed64);
		NContainer::CByteVector Output;
		Output.f_SetLen(nMaxCompressed);
		uint8 *pOutput = Output.f_GetArray();

		fsg_WriteSZDDHeader(pOutput, uint32(nSourceLen), _Options.m_FilenameHint);

		if (nSourceLen == 0)
		{
			Output.f_SetLen(gc_SZDDHeaderSize);
			return Output;
		}

		uint8 const *pSource = _Source.f_GetArray();

		// Hash-chain indexed sliding window for fast match finding
		CSZDDMatchFinder MatchFinder;
		MatchFinder.f_Init();

		// Block accumulation
		uint8 BlockBuffer[1 + 16]; // 1 control byte + max 16 data bytes (8 back-refs * 2 bytes each)
		umint nBlockDataLen = 0;
		uint8 ControlByte = 0;
		uint8 ControlMask = 0x01;
		umint nOutputPos = gc_SZDDHeaderSize;

		umint iSrcPos = 0;

		while (iSrcPos < nSourceLen)
		{
			umint nLookahead = fg_Min(gc_SZDDMaxMatchLength, nSourceLen - iSrcPos);

			// Find the best match in the window
			smint nMatchPos, nMatchLen;
			MatchFinder.f_FindMatch(pSource + iSrcPos, nLookahead, nMatchPos, nMatchLen);

			if (umint(nMatchLen) >= gc_SZDDMinMatchLength)
			{
				// Emit back-reference
				umint nBestPos = umint(nMatchPos);
				umint nBestLen = umint(nMatchLen);
				uint8 Byte1 = uint8(nBestPos & 0xFF);
				uint8 Byte2 = uint8(((nBestPos >> 4) & 0xF0) | ((nBestLen - gc_SZDDMinMatchLength) & 0x0F));
				BlockBuffer[1 + nBlockDataLen++] = Byte1;
				BlockBuffer[1 + nBlockDataLen++] = Byte2;

				// Update window and hash chains for all consumed bytes
				for (umint i = 0; i < nBestLen; ++i)
				{
					MatchFinder.f_UpdatePosition(pSource[iSrcPos + i]);
					MatchFinder.f_Advance();
				}
				iSrcPos += nBestLen;
			}
			else
			{
				// Emit literal
				ControlByte |= ControlMask;
				BlockBuffer[1 + nBlockDataLen++] = pSource[iSrcPos];

				MatchFinder.f_UpdatePosition(pSource[iSrcPos]);
				MatchFinder.f_Advance();
				iSrcPos += 1;
			}

			ControlMask <<= 1;

			if (ControlMask == 0)
			{
				// Block full: write control byte + data
				BlockBuffer[0] = ControlByte;
				NMemory::fg_MemCopy(pOutput + nOutputPos, BlockBuffer, 1 + nBlockDataLen);
				nOutputPos += 1 + nBlockDataLen;

				ControlByte = 0;
				ControlMask = 0x01;
				nBlockDataLen = 0;
			}
		}

		// Write partial block if any pending symbols
		if (ControlMask != 0x01)
		{
			BlockBuffer[0] = ControlByte;
			NMemory::fg_MemCopy(pOutput + nOutputPos, BlockBuffer, 1 + nBlockDataLen);
			nOutputPos += 1 + nBlockDataLen;
		}

		Output.f_SetLen(nOutputPos);
		return Output;
	}

	NContainer::CByteVector fg_DecompressMSCompress(NContainer::CByteVector const &_Source, umint _nMaxDecompressedLen)
	{
		using namespace NStr;

		uint8 const *pInput = _Source.f_GetArray();
		umint nInputLen = _Source.f_GetLen();

		CSZDDHeader Header = fsg_ReadSZDDHeader(pInput, nInputLen);

		if (Header.m_OriginalSize == 0)
			return {};

		if (Header.m_OriginalSize > _nMaxDecompressedLen)
			DMibError("MSCompress decompressed size ({}) exceeds maximum allowed ({})"_f << Header.m_OriginalSize << _nMaxDecompressedLen);

		NContainer::CByteVector Output;
		Output.f_SetLen(Header.m_OriginalSize);
		uint8 *pOutput = Output.f_GetArray();

		// Initialize sliding window: positions 0..N-F-1 with spaces, N-F..N-1 with zero
		uint8 Window[gc_SZDDWindowSize];
		for (umint i = 0; i < gc_SZDDWindowSize - gc_SZDDMaxMatchLength; ++i)
			Window[i] = 0x20;
		for (umint i = gc_SZDDWindowSize - gc_SZDDMaxMatchLength; i < gc_SZDDWindowSize; ++i)
			Window[i] = 0x00;
		umint iWinPos = gc_SZDDWindowSize - gc_SZDDMaxMatchLength;

		umint iInputPos = gc_SZDDHeaderSize;
		umint iOutputPos = 0;

		while (iInputPos < nInputLen && iOutputPos < Header.m_OriginalSize)
		{
			uint8 ControlByte = pInput[iInputPos++];

			for (uint8 Mask = 0x01; Mask != 0 && iOutputPos < Header.m_OriginalSize; Mask <<= 1)
			{
				if (ControlByte & Mask)
				{
					// Literal
					if (iInputPos >= nInputLen)
						DMibError("Truncated MS-compressed data: unexpected end of stream in literal");

					uint8 Byte = pInput[iInputPos++];
					pOutput[iOutputPos++] = Byte;
					Window[iWinPos] = Byte;
					iWinPos = (iWinPos + 1) & (gc_SZDDWindowSize - 1);
				}
				else
				{
					// Back-reference
					if (iInputPos + 1 >= nInputLen)
						DMibError("Truncated MS-compressed data: unexpected end of stream in back-reference");

					uint8 Byte1 = pInput[iInputPos++];
					uint8 Byte2 = pInput[iInputPos++];

					umint nMatchPos = umint(Byte1) | (umint(Byte2 & 0xF0) << 4);
					umint nMatchLen = umint(Byte2 & 0x0F) + gc_SZDDMinMatchLength;

					for (umint i = 0; i < nMatchLen && iOutputPos < Header.m_OriginalSize; ++i)
					{
						uint8 Byte = Window[nMatchPos];
						pOutput[iOutputPos++] = Byte;
						Window[iWinPos] = Byte;
						nMatchPos = (nMatchPos + 1) & (gc_SZDDWindowSize - 1);
						iWinPos = (iWinPos + 1) & (gc_SZDDWindowSize - 1);
					}
				}
			}
		}

		if (iOutputPos < Header.m_OriginalSize)
			DMibError("Truncated MS-compressed data: decompressed {} bytes but expected {}"_f << iOutputPos << Header.m_OriginalSize);

		return Output;
	}
}
