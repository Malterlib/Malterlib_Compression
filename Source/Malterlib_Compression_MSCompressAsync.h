// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/Concurrency/ConcurrencyDefines>
#include <Mib/Concurrency/AsyncGenerator>
#include <Mib/Storage/Optional>
#include "Malterlib_Compression_MSCompress.h"

namespace NMib::NCompression
{
	struct CMSCompressAsyncOptions
	{
		NStorage::TCOptional<uint32> m_KnownSize; ///< Original file size (required for compression header; max 4 GB)
		uint8 m_FilenameHint = 0x00;              ///< Last character of original filename, or 0x00
	};

	/// Compress an async byte stream in Microsoft SZDD format.
	/// _Options.m_KnownSize must be set to the exact uncompressed size (required for the SZDD header).
	auto fg_CompressMSCompressAsync(NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> _InputData, CMSCompressAsyncOptions _Options)
		-> NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector>
	;

	/// Decompress an async byte stream in Microsoft SZDD format.
	/// _nMaxDecompressedLen limits total output to guard against malicious headers on untrusted input.
	auto fg_DecompressMSCompressAsync(NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector> _InputData, umint _nMaxDecompressedLen = gc_MSCompressDefaultMaxDecompressedLen)
		-> NConcurrency::TCAsyncGenerator<NContainer::CIOByteVector>
	;
}

#ifndef DMibPNoShortCuts
using namespace NMib::NCompression;
#endif
