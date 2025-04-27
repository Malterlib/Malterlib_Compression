// Copyright © 2025 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Compression_ZstandardAsync.h"

#include <Mib/Concurrency/ConcurrencyManager>

#include <zstd.h>
#include <zstd_errors.h>

namespace NMib::NCompression
{
	NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> fg_CompressZstandardAsync
		(
			NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> _InputData
			, CZStandardCompressionOptions _Options
		)
	{
		using namespace NStr;

		co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());

		auto *pZstdStream = ZSTD_createCStream();

		if (!pZstdStream)
			co_return DMibErrorInstance("Failed to create zstd compression stream");

		auto Cleanup = g_OnScopeExit / [&]
			{
				ZSTD_freeCStream(pZstdStream);
			}
		;

		if (auto Result = ZSTD_CCtx_reset(pZstdStream, ZSTD_reset_session_and_parameters); ZSTD_isError(Result))
			co_return DMibErrorInstance("Failed to reset zstd compression context: {}"_f << ZSTD_getErrorName(Result));

		if (auto Result = ZSTD_CCtx_setParameter(pZstdStream, ZSTD_c_compressionLevel, _Options.m_CompressionLevel); ZSTD_isError(Result))
			co_return DMibErrorInstance("Failed to set zstd compression level: {}"_f << ZSTD_getErrorName(Result));

		if (auto Result = ZSTD_CCtx_setParameter(pZstdStream, ZSTD_c_nbWorkers, _Options.m_CompressionThreads); ZSTD_isError(Result))
			co_return DMibErrorInstance("Failed to set zstd workers: {}"_f << ZSTD_getErrorName(Result));

		if (_Options.m_KnownSize != TCLimitsInt<mint>::mc_Max)
		{
			if (auto Result = ZSTD_CCtx_setPledgedSrcSize(pZstdStream, _Options.m_KnownSize); ZSTD_isError(Result))
				co_return DMibErrorInstance("Failed to set zstd pledged source size: {}"_f << ZSTD_getErrorName(Result));
		}

		NContainer::CIOByteVector OutputData;
		ZSTD_outBuffer OutputBuffer;

		auto fResetOutputBuffer = [&]
			{
				OutputData.f_SetLen(NFile::gc_IdealIoSize);
				OutputBuffer = {.dst = OutputData.f_GetArray(), .size = OutputData.f_GetLen(), .pos = 0};
			}
		;

		fResetOutputBuffer();

		for (auto iData = co_await fg_Move(_InputData).f_GetPipelinedIterator(); iData; co_await ++iData)
		{
			auto &&InData = *iData;

			ZSTD_inBuffer InputBuffer{.src = InData.f_GetArray(), .size = InData.f_GetLen(), .pos = 0};
			while (InputBuffer.pos < InputBuffer.size)
			{
				auto CompressResult = ZSTD_compressStream2(pZstdStream, &OutputBuffer, &InputBuffer, ZSTD_e_continue);
				if (ZSTD_isError(CompressResult))
					co_return DMibErrorInstance("Error compressing data: {}"_f << ZSTD_getErrorName(CompressResult));

				if (OutputBuffer.pos == OutputBuffer.size)
				{
					co_yield fg_Move(OutputData);
					fResetOutputBuffer();
				}
			}
		}

		while (true)
		{
			ZSTD_inBuffer InputBuffer{.src = nullptr, .size = 0, .pos = 0};

			auto CompressResult = ZSTD_compressStream2(pZstdStream, &OutputBuffer, &InputBuffer, ZSTD_e_end);
			if (ZSTD_isError(CompressResult))
				co_return DMibErrorInstance("Error compressing data (end): {}"_f << ZSTD_getErrorName(CompressResult));

			if (CompressResult == 0)
				break;

			if (OutputBuffer.pos == OutputBuffer.size)
			{
				co_yield fg_Move(OutputData);
				fResetOutputBuffer();
			}
		}

		if (OutputBuffer.pos)
		{
			OutputData.f_SetLen(OutputBuffer.pos, false);
			co_yield fg_Move(OutputData);
		}

		co_return {};
	}

	NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> fg_DecompressZstandardAsync(NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> _InputData)
	{
		using namespace NStr;

		co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());

		auto *pZstdStream = ZSTD_createDStream();

		if (!pZstdStream)
			co_return DMibErrorInstance("Failed to create zstd stream");

		auto Cleanup = g_OnScopeExit / [&]
			{
				ZSTD_freeDStream(pZstdStream);
			}
		;

		if (auto Result = ZSTD_initDStream(pZstdStream); ZSTD_isError(Result))
			co_return DMibErrorInstance("Failed to init zstd decompression context: {}"_f << ZSTD_getErrorName(Result));

		NContainer::CIOByteVector OutputData;
		ZSTD_outBuffer OutputBuffer;

		auto fResetOutputBuffer = [&]
			{
				OutputData.f_SetLen(NFile::gc_IdealIoSize);
				OutputBuffer = {.dst = OutputData.f_GetArray(), .size = OutputData.f_GetLen(), .pos = 0};
			}
		;

		fResetOutputBuffer();

		bool bFinished = false;

		for (auto iData = co_await fg_Move(_InputData).f_GetPipelinedIterator(); iData; co_await ++iData)
		{
			auto &&InData = *iData;

			ZSTD_inBuffer InputBuffer{.src = InData.f_GetArray(), .size = InData.f_GetLen(), .pos = 0};

			while (InputBuffer.pos < InputBuffer.size)
			{
				auto DecompressResult = ZSTD_decompressStream(pZstdStream, &OutputBuffer, &InputBuffer);
				if (ZSTD_isError(DecompressResult))
					co_return DMibErrorInstance("Error decompressing data: {}"_f << ZSTD_getErrorName(DecompressResult));

				bFinished = DecompressResult == 0;

				if (OutputBuffer.pos == OutputBuffer.size)
				{
					co_yield fg_Move(OutputData);
					fResetOutputBuffer();
				}
			}
		}

		while (!bFinished)
		{
			ZSTD_inBuffer InputBuffer{.src = nullptr, .size = 0, .pos = 0};
			auto PosPreOutput = OutputBuffer.pos;
			auto DecompressResult = ZSTD_decompressStream(pZstdStream, &OutputBuffer, &InputBuffer);
			if (ZSTD_isError(DecompressResult))
				co_return DMibErrorInstance("Error decompressing data (end): {}"_f << ZSTD_getErrorName(DecompressResult));

			if (DecompressResult == 0)
				break;
			else if (OutputBuffer.pos == PosPreOutput)
				co_return DMibErrorInstance("Error decompressing data (end): No progress was made");

			if (OutputBuffer.pos == OutputBuffer.size)
			{
				co_yield fg_Move(OutputData);
				fResetOutputBuffer();
			}
		}

		if (OutputBuffer.pos)
		{
			OutputData.f_SetLen(OutputBuffer.pos, false);
			co_yield fg_Move(OutputData);
		}
		
		co_return {};
	}
}
