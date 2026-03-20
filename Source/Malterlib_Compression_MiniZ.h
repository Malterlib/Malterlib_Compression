// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

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
