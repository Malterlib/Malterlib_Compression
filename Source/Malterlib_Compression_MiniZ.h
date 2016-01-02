// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once
#include <Mib/Core/Core>

namespace NMib
{
	namespace NDataProcessing
	{

		class CCompress_MiniZ
		{
		public:

			NContainer::TCVector<uint8> f_Compress(NContainer::TCVector<uint8> const& _Input);
			NContainer::TCVector<uint8> f_Decompress(NContainer::TCVector<uint8> const& _Input);

		private:
		};

	} // Namespace NDataProcessing

} // Namspace NMib
