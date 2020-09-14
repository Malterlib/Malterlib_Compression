// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Compression/Huffman>
#include <Mib/Compression/ZLib>

namespace
{
	class CCompression_Tests : public NMib::NTest::CTest
	{
	public:

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
			DMibTestSuite("GZip")
			{
				using namespace NMib::NCompression;
				using namespace NMib::NContainer;
				using namespace NMib::NStream;

				NMib::NMisc::CRandomShiftRNG Random;
				CByteVector TestBuffer;
				TestBuffer.f_SetLen(2048);
				for(aint i = 0; i < 2048; ++i)
					TestBuffer[i] = Random.f_GetValue<uint8>();

				CByteVector CompressedData;
				{
					CBinaryStreamMemoryPtr<> SourceStream;
					SourceStream.f_OpenRead(TestBuffer);

					CBinaryStreamMemory<> DestinationStream;

					fg_CompressGZip(SourceStream, DestinationStream);
					CompressedData = DestinationStream.f_MoveVectorOptimized();
				}

				CByteVector DecompressedData;
				{
					CBinaryStreamMemoryPtr<> SourceStream;
					SourceStream.f_OpenRead(CompressedData);

					CBinaryStreamMemory<> DestinationStream;

					fg_DecompressGZip(SourceStream, DestinationStream);
					DecompressedData = DestinationStream.f_MoveVectorOptimized();
				}

				DMibExpect(DecompressedData, ==, TestBuffer);
			};
		}
	};

	DMibTestRegister(CCompression_Tests, Malterlib::Compression);
}
