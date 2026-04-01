// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>

namespace NMib::NCompression
{
	NContainer::CByteVector fg_CompressZstandard(NContainer::CByteVector const &_Source, int32 _CompressionLevel = 8);
	NContainer::CByteVector fg_DecompressZstandard(NContainer::CByteVector const &_Source);
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NCompression;
#endif
