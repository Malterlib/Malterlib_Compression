// Copyright © 2026 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>

namespace NMib::NCompression
{
	constexpr umint gc_LZWDefaultBitsPerCode = 12;
	constexpr umint gc_LZWDefaultMaxDecompressedLen = umint(256) * 1024 * 1024;

	NContainer::CByteVector fg_CompressLZW(NContainer::CByteVector const &_Source, umint _nBitsPerCode = gc_LZWDefaultBitsPerCode);
	NContainer::CByteVector fg_DecompressLZW(NContainer::CByteVector const &_Source, umint _nMaxDestinationLen = gc_LZWDefaultMaxDecompressedLen);
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NCompression;
#endif
