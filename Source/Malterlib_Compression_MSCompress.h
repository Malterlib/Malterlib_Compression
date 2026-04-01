// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>

namespace NMib::NCompression
{
	/// Options for MSCompress compression
	struct CMSCompressOptions
	{
		uint8 m_FilenameHint = 0x00; ///< Last character of original filename (stored in SZDD header at offset 0x09), or 0x00 if not stored
	};

	/// Extract the filename hint byte from a filename.
	/// Returns the last character of the filename, which is stored in the SZDD header
	/// so that FILENAME.EX_ can be restored to FILENAME.EXE.
	/// Returns 0x00 if the filename is empty.
	uint8 fg_MSCompressFilenameHint(NStr::CStr const &_Filename);

	/// Compress data in Microsoft SZDD format (compatible with COMPRESS.EXE / EXPAND.EXE)
	NContainer::CByteVector fg_CompressMSCompress(NContainer::CByteVector const &_Source, CMSCompressOptions _Options = {});

	constexpr umint gc_MSCompressDefaultMaxDecompressedLen = umint(256) * 1024 * 1024;

	/// Decompress data in Microsoft SZDD format.
	/// _nMaxDecompressedLen limits the output allocation to guard against malicious headers on untrusted input.
	NContainer::CByteVector fg_DecompressMSCompress(NContainer::CByteVector const &_Source, umint _nMaxDecompressedLen = gc_MSCompressDefaultMaxDecompressedLen);

	/// Read the filename hint byte from SZDD compressed data without decompressing.
	/// Returns 0x00 if not stored.
	uint8 fg_MSCompressReadFilenameHint(NContainer::CByteVector const &_CompressedData);
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NCompression;
#endif
