// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once
#include <Mib/Core/Core>

namespace NMib::NCompression
{
	NContainer::CByteVector fg_CompressMiniZ(NContainer::CByteVector const& _Input);
	NContainer::CByteVector fg_DecompressMiniZ(NContainer::CByteVector const& _Input);
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NCompression;
#endif
