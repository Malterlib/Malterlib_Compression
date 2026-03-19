// Copyright © 2026 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>

namespace NMib::NCompression
{
	constexpr umint gc_HuffmanDefaultMaxDecompressedLen = umint(256) * 1024 * 1024;

	NContainer::CByteVector fg_CompressHuffman(NContainer::CByteVector const &_Source);
	NContainer::CByteVector fg_DecompressHuffman(NContainer::CByteVector const &_Source, umint _nMaxDestinationLen = gc_HuffmanDefaultMaxDecompressedLen);
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NCompression;
#endif
