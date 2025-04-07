// Copyright © 2025 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>

namespace NMib::NCompression
{
	NContainer::CByteVector fg_CompressZstandard(NContainer::CByteVector const &_Source, int _CompressionLevel = 8);
	NContainer::CByteVector fg_DecompressZstandard(NContainer::CByteVector const &_Source);
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NCompression;
#endif
