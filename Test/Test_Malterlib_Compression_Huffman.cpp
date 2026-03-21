// Copyright © 2026 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Compression/Huffman>

namespace
{
	struct CHuffman_Tests : public NMib::NTest::CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("Huffman")
			{
				NMib::NCompression::TCCompress_Huffman<> Compression;

				NMib::NMisc::CRandomShiftRNG Random;
				uint8 pTestBuffer[2048];
				for(aint i = 0; i < 2048; ++i)
				{
					pTestBuffer[i] = Random.f_GetValue<uint8>();
				}

				aint nDest;
				void *pDest;
				Compression.f_CompressHuffman(pTestBuffer,2048,pDest,nDest);

				aint nDestUncomp;
				void *pDestUncomp;
				Compression.f_DecompressHuffman(pDest,nDest,pDestUncomp,nDestUncomp);

				uint8 * pTestFinalBuffer = (uint8*)pDestUncomp;
				bool bDecompressDiffer = false;
				for (aint i = 0; i < 2048; ++i)
				{
					if (pTestFinalBuffer[i] != pTestBuffer[i])
						bDecompressDiffer = true;
				}

				DMibTest(DMibExpr(!bDecompressDiffer));

				NMib::NMemory::fg_FreeNoSize(pDest);
				NMib::NMemory::fg_FreeNoSize(pDestUncomp);
			};
		}
	};

	DMibTestRegister(CHuffman_Tests, Malterlib::Compression);
}
