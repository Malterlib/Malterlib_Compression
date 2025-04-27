// Copyright © 2025 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyDefines>
#include <Mib/Concurrency/AsyncGenerator>

namespace NMib::NCompression
{
	struct CZStandardCompressionOptions
	{
		uint64 m_KnownSize = TCLimitsInt<mint>::mc_Max; // Specify size if known to make decompression more efficient
		int32 m_CompressionLevel = 8;
		mint m_CompressionThreads = NSys::fg_Thread_GetPhysicalCores(); // The number of threads to use for compression
	};

	NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> fg_CompressZstandardAsync
		(
			NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> _InputData
			, CZStandardCompressionOptions _Options = {}
		)
	;

	NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> fg_DecompressZstandardAsync(NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> _InputData);
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NCompression;
#endif
