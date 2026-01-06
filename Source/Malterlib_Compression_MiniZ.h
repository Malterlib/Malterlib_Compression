// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once
#include <Mib/Core/Core>

namespace NMib::NCompression
{
	class CCompress_MiniZ
	{
	public:

		NContainer::CByteVector f_Compress(NContainer::CByteVector const& _Input);
		NContainer::CByteVector f_Decompress(NContainer::CByteVector const& _Input);

	private:
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NCompression;
#endif
