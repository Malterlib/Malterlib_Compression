// Copyright © 2026 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Compression/ZLib>

namespace
{
	struct CZlib_Tests : public NMib::NTest::CTest
	{
		void f_DoTests()
		{
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

	DMibTestRegister(CZlib_Tests, Malterlib::Compression);
}
