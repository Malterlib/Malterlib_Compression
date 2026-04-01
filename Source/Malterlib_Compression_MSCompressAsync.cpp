// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Compression_MSCompressAsync.h"
#include "Malterlib_Compression_MSCompress_Constants.h"

#include <Mib/Concurrency/ConcurrencyManager>

namespace NMib::NCompression
{

	NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> fg_CompressMSCompressAsync
		(
			NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> _InputData
			, CMSCompressAsyncOptions _Options
		)
	{
		using namespace NStr;

		co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());

		if (!_Options.m_KnownSize)
			co_return DMibErrorInstance("MSCompress async compression requires m_KnownSize to be set");

		// Hash-chain indexed sliding window for fast match finding
		CSZDDMatchFinder MatchFinder;
		MatchFinder.f_Init();

		// Output buffer
		NContainer::CIOByteVector OutputData;
		OutputData.f_SetLen(NFile::gc_IdealIoSize);
		umint nOutputPos = 0;

		// Write SZDD header directly into output buffer (14 bytes, always fits)
		{
			uint32 Magic = fg_ByteSwapLE(gc_SZDDMagicSig);
			NMemory::fg_MemCopy(OutputData.f_GetArray() + 0, &Magic, 4);

			uint32 MagicConst = fg_ByteSwapLE(gc_SZDDMagicConst);
			NMemory::fg_MemCopy(OutputData.f_GetArray() + 4, &MagicConst, 4);

			OutputData[8] = gc_SZDDCompressionType;
			OutputData[9] = _Options.m_FilenameHint;

			uint32 OrigSize = fg_ByteSwapLE(*_Options.m_KnownSize);
			NMemory::fg_MemCopy(OutputData.f_GetArray() + 10, &OrigSize, 4);

			nOutputPos = gc_SZDDHeaderSize;
		}

		// Block accumulation
		uint8 BlockBuffer[1 + 16];
		umint nBlockDataLen = 0;
		uint8 ControlByte = 0;
		uint8 ControlMask = 0x01;

		uint64 nTotalInput = 0;

		// Lookahead buffer to preserve match quality across chunk boundaries.
		// Holds up to gc_SZDDMaxMatchLength bytes of upcoming input.
		uint8 Lookahead[gc_SZDDMaxMatchLength];
		umint nLookaheadLen = 0;

		// Yield current output buffer and allocate a fresh one.
		// Usage: co_yield fFlushOutputData();
		auto fFlushOutputData = [&]() -> NContainer::CIOByteVector
			{
				OutputData.f_SetLen(nOutputPos, false);
				auto Result = fg_Move(OutputData);
				OutputData.f_SetLen(NFile::gc_IdealIoSize);
				nOutputPos = 0;
				return Result;
			}
		;

		// Emit one symbol from the lookahead: find match, encode back-ref or literal,
		// update window and shift the lookahead buffer. co_yield cannot appear in a
		// lambda, so only the symbol emission is factored out; the block flush that
		// follows each call is kept inline.
		auto fEmitSymbol = [&]
			{
				smint nMatchPos, nMatchLen;
				MatchFinder.f_FindMatch(Lookahead, nLookaheadLen, nMatchPos, nMatchLen);

				umint nConsumed;

				if (umint(nMatchLen) >= gc_SZDDMinMatchLength)
				{
					umint nBestPos = umint(nMatchPos);
					umint nBestLen = umint(nMatchLen);
					BlockBuffer[1 + nBlockDataLen++] = uint8(nBestPos & 0xFF);
					BlockBuffer[1 + nBlockDataLen++] = uint8(((nBestPos >> 4) & 0xF0) | ((nBestLen - gc_SZDDMinMatchLength) & 0x0F));
					nConsumed = nBestLen;
				}
				else
				{
					ControlByte |= ControlMask;
					BlockBuffer[1 + nBlockDataLen++] = Lookahead[0];
					nConsumed = 1;
				}

				for (umint iAdvance = 0; iAdvance < nConsumed; ++iAdvance)
				{
					MatchFinder.f_UpdatePosition(Lookahead[iAdvance]);
					MatchFinder.f_Advance();
				}

				nLookaheadLen -= nConsumed;
				if (nLookaheadLen > 0)
					NMemory::fg_MemMove(Lookahead, Lookahead + nConsumed, nLookaheadLen);

				ControlMask <<= 1;
			}
		;

		// Write a completed 8-symbol block to the output buffer.
		auto fWriteBlock = [&]
			{
				BlockBuffer[0] = ControlByte;
				NMemory::fg_MemCopy(OutputData.f_GetArray() + nOutputPos, BlockBuffer, 1 + nBlockDataLen);
				nOutputPos += 1 + nBlockDataLen;
				ControlByte = 0;
				ControlMask = 0x01;
				nBlockDataLen = 0;
			}
		;

		// Process input chunks streaming
		for (auto iData = co_await fg_Move(_InputData).f_GetPipelinedIterator(); iData; co_await ++iData)
		{
			auto &&InData = *iData;
			uint8 const *pChunk = InData.f_GetArray();
			umint nChunkLen = InData.f_GetLen();
			nTotalInput += nChunkLen;
			umint iChunkPos = 0;

			// Refill lookahead from chunk, then compress symbols while the lookahead is full.
			while (true)
			{
				while (nLookaheadLen < gc_SZDDMaxMatchLength && iChunkPos < nChunkLen)
					Lookahead[nLookaheadLen++] = pChunk[iChunkPos++];

				// Only process when the lookahead is full, to preserve cross-chunk match quality
				if (nLookaheadLen < gc_SZDDMaxMatchLength)
					break;

				fEmitSymbol();

				if (ControlMask == 0)
				{
					if (nOutputPos + gc_SZDDMaxBlockSize > OutputData.f_GetLen())
						co_yield fFlushOutputData();
					fWriteBlock();
				}
			}
		}

		// Process remaining lookahead bytes after all chunks consumed
		while (nLookaheadLen > 0)
		{
			fEmitSymbol();

			if (ControlMask == 0)
			{
				if (nOutputPos + gc_SZDDMaxBlockSize > OutputData.f_GetLen())
					co_yield fFlushOutputData();
				fWriteBlock();
			}
		}

		if (nTotalInput != *_Options.m_KnownSize)
			co_return DMibErrorInstance("MSCompress: actual input size ({}) does not match m_KnownSize ({})"_f << nTotalInput << *_Options.m_KnownSize);

		// Write partial block
		if (ControlMask != 0x01)
		{
			if (nOutputPos + gc_SZDDMaxBlockSize > OutputData.f_GetLen())
				co_yield fFlushOutputData();

			BlockBuffer[0] = ControlByte;
			NMemory::fg_MemCopy(OutputData.f_GetArray() + nOutputPos, BlockBuffer, 1 + nBlockDataLen);
			nOutputPos += 1 + nBlockDataLen;
		}

		// Yield remaining output
		if (nOutputPos)
		{
			OutputData.f_SetLen(nOutputPos, false);
			co_yield fg_Move(OutputData);
		}

		co_return {};
	}

	NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> fg_DecompressMSCompressAsync
		(
			NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> _InputData
			, umint _nMaxDecompressedLen
		)
	{
		using namespace NStr;

		co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());

		// Sliding window: positions 0..N-F-1 with spaces, N-F..N-1 with zero
		uint8 Window[gc_SZDDWindowSize];
		for (umint i = 0; i < gc_SZDDWindowSize - gc_SZDDMaxMatchLength; ++i)
			Window[i] = 0x20;
		for (umint i = gc_SZDDWindowSize - gc_SZDDMaxMatchLength; i < gc_SZDDWindowSize; ++i)
			Window[i] = 0x00;
		umint iWinPos = gc_SZDDWindowSize - gc_SZDDMaxMatchLength;

		// Output buffer
		NContainer::CIOByteVector OutputData;
		OutputData.f_SetLen(NFile::gc_IdealIoSize);
		umint nOutputPos = 0;

		auto fFlushOutputData = [&]() -> NContainer::CIOByteVector
			{
				auto Result = fg_Move(OutputData);
				OutputData.f_SetLen(NFile::gc_IdealIoSize);
				nOutputPos = 0;
				return Result;
			}
		;

		// Carry buffer for bytes that span chunk boundaries.
		// Before processing a control byte we require gc_SZDDMaxBlockSize (17) bytes.
		// If fewer remain we carry them to the next chunk. Max carry = 16 bytes.
		uint8 CarryBuffer[gc_SZDDMaxBlockSize];
		umint nCarryLen = 0;

		// Header accumulation
		uint8 HeaderBytes[gc_SZDDHeaderSize];
		umint nHeaderRead = 0;
		uint32 nOriginalSize = 0;
		umint nTotalOutput = 0;

		for (auto iData = co_await fg_Move(_InputData).f_GetPipelinedIterator(); iData; co_await ++iData)
		{
			auto &&InData = *iData;
			uint8 const *pChunk = InData.f_GetArray();
			umint nChunkLen = InData.f_GetLen();
			umint iChunkPos = 0;

			// Read header bytes if we haven't finished yet
			if (nHeaderRead < gc_SZDDHeaderSize)
			{
				umint nNeed = gc_SZDDHeaderSize - nHeaderRead;
				umint nCopy = fg_Min(nNeed, nChunkLen);
				NMemory::fg_MemCopy(HeaderBytes + nHeaderRead, pChunk, nCopy);
				nHeaderRead += nCopy;
				iChunkPos = nCopy;

				if (nHeaderRead < gc_SZDDHeaderSize)
					continue;

				// Validate header
				uint32 MagicSig;
				NMemory::fg_MemCopy(&MagicSig, HeaderBytes, 4);
				MagicSig = fg_ByteSwapLE(MagicSig);

				if (MagicSig == gc_KWAJMagicSig)
					co_return DMibErrorInstance("MS-compressed file uses KWAJ format (version 6.22) which is not supported");

				if (MagicSig != gc_SZDDMagicSig)
					co_return DMibErrorInstance("Not a valid MS-compressed file: invalid SZDD signature");

				uint32 MagicConst;
				NMemory::fg_MemCopy(&MagicConst, HeaderBytes + 4, 4);
				MagicConst = fg_ByteSwapLE(MagicConst);

				if (MagicConst != gc_SZDDMagicConst)
					co_return DMibErrorInstance("Not a valid MS-compressed file: invalid magic constant");

				if (HeaderBytes[8] != gc_SZDDCompressionType)
					co_return DMibErrorInstance("Not a valid MS-compressed file: unsupported compression type 0x{nh}"_f << uint32(HeaderBytes[8]));

				// Read original size from header
				NMemory::fg_MemCopy(&nOriginalSize, HeaderBytes + 10, 4);
				nOriginalSize = fg_ByteSwapLE(nOriginalSize);

				if (nOriginalSize > _nMaxDecompressedLen)
					co_return DMibErrorInstance("MSCompress decompressed size ({}) exceeds maximum allowed ({})"_f << nOriginalSize << _nMaxDecompressedLen);
			}

			// Stop processing compressed data once we've produced all expected output
			if (nTotalOutput >= nOriginalSize)
				continue;

			// Merge carry bytes with current chunk
			umint nNewBytes = nChunkLen - iChunkPos;
			umint nAvailable = nCarryLen + nNewBytes;
			uint8 const *pPayload;
			NContainer::CByteVector MergedBuffer;

			if (nCarryLen > 0)
			{
				MergedBuffer.f_SetLen(nAvailable);
				NMemory::fg_MemCopy(MergedBuffer.f_GetArray(), CarryBuffer, nCarryLen);
				NMemory::fg_MemCopy(MergedBuffer.f_GetArray() + nCarryLen, pChunk + iChunkPos, nNewBytes);
				pPayload = MergedBuffer.f_GetArray();
				nCarryLen = 0;
			}
			else
			{
				pPayload = pChunk + iChunkPos;
			}

			umint iPayloadPos = 0;

			// Emit one decompressed byte to the output buffer and window.
			auto fEmitDecompressed = [&](uint8 _Byte)
				{
					OutputData[nOutputPos++] = _Byte;
					Window[iWinPos] = _Byte;
					iWinPos = (iWinPos + 1) & (gc_SZDDWindowSize - 1);
					++nTotalOutput;
				}
			;

			// Process complete blocks. Only start a control byte if we have enough bytes
			// to handle the worst case (17 bytes), so we never split mid-block.
			while (iPayloadPos + gc_SZDDMaxBlockSize <= nAvailable && nTotalOutput < nOriginalSize)
			{
				uint8 ControlByte = pPayload[iPayloadPos++];

				for (uint8 Mask = 0x01; Mask != 0 && nTotalOutput < nOriginalSize; Mask <<= 1)
				{
					if (ControlByte & Mask)
					{
						if (nOutputPos >= OutputData.f_GetLen())
							co_yield fFlushOutputData();
						fEmitDecompressed(pPayload[iPayloadPos++]);
					}
					else
					{
						uint8 Byte1 = pPayload[iPayloadPos++];
						uint8 Byte2 = pPayload[iPayloadPos++];
						umint nMatchPos = umint(Byte1) | (umint(Byte2 & 0xF0) << 4);
						umint nMatchLen = umint(Byte2 & 0x0F) + gc_SZDDMinMatchLength;

						for (umint iMatch = 0; iMatch < nMatchLen && nTotalOutput < nOriginalSize; ++iMatch)
						{
							if (nOutputPos >= OutputData.f_GetLen())
								co_yield fFlushOutputData();
							fEmitDecompressed(Window[nMatchPos]);
							nMatchPos = (nMatchPos + 1) & (gc_SZDDWindowSize - 1);
						}
					}
				}
			}

			// Carry remaining bytes (< gc_SZDDMaxBlockSize) to next chunk, but only if
			// we still need more output. Once nOriginalSize is reached, trailing bytes are
			// ignored — do not copy them into the fixed-size CarryBuffer.
			if (nTotalOutput < nOriginalSize)
			{
				nCarryLen = nAvailable - iPayloadPos;
				if (nCarryLen > 0)
					NMemory::fg_MemCopy(CarryBuffer, pPayload + iPayloadPos, nCarryLen);
			}
		}

		// Process any final carry bytes (last block may be partial — fewer than 8 symbols)
		if (nCarryLen > 0 && nTotalOutput < nOriginalSize)
		{
			uint8 const *pPayload = CarryBuffer;
			umint iPayloadPos = 0;

			auto fEmitDecompressed = [&](uint8 _Byte)
				{
					OutputData[nOutputPos++] = _Byte;
					Window[iWinPos] = _Byte;
					iWinPos = (iWinPos + 1) & (gc_SZDDWindowSize - 1);
					++nTotalOutput;
				}
			;

			while (iPayloadPos < nCarryLen && nTotalOutput < nOriginalSize)
			{
				uint8 ControlByte = pPayload[iPayloadPos++];

				for (uint8 Mask = 0x01; Mask != 0 && iPayloadPos < nCarryLen && nTotalOutput < nOriginalSize; Mask <<= 1)
				{
					if (ControlByte & Mask)
					{
						if (nOutputPos >= OutputData.f_GetLen())
							co_yield fFlushOutputData();
						fEmitDecompressed(pPayload[iPayloadPos++]);
					}
					else
					{
						if (iPayloadPos + 1 >= nCarryLen)
							co_return DMibErrorInstance("Truncated MS-compressed data: unexpected end of stream in back-reference");

						uint8 Byte1 = pPayload[iPayloadPos++];
						uint8 Byte2 = pPayload[iPayloadPos++];
						umint nMatchPos = umint(Byte1) | (umint(Byte2 & 0xF0) << 4);
						umint nMatchLen = umint(Byte2 & 0x0F) + gc_SZDDMinMatchLength;

						for (umint iMatch = 0; iMatch < nMatchLen && nTotalOutput < nOriginalSize; ++iMatch)
						{
							if (nOutputPos >= OutputData.f_GetLen())
								co_yield fFlushOutputData();
							fEmitDecompressed(Window[nMatchPos]);
							nMatchPos = (nMatchPos + 1) & (gc_SZDDWindowSize - 1);
						}
					}
				}
			}
		}

		if (nHeaderRead < gc_SZDDHeaderSize)
			co_return DMibErrorInstance("Not a valid MS-compressed file: file too small");

		if (nTotalOutput < nOriginalSize)
			co_return DMibErrorInstance("Truncated MS-compressed data: decompressed {} bytes but expected {}"_f << nTotalOutput << nOriginalSize);

		// Yield remaining output
		if (nOutputPos)
		{
			OutputData.f_SetLen(nOutputPos, false);
			co_yield fg_Move(OutputData);
		}

		co_return {};
	}
}
