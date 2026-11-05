// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Compression/MiniZ>
#include <Mib/Test/Exception>

namespace
{
	struct CMiniZ_Tests : public NMib::NTest::CTest
	{
		void f_DoTests();
	};

	void CMiniZ_Tests::f_DoTests()
	{
		DMibTestSuite("MiniZ")
		{
			using namespace NMib::NContainer;
			using namespace NMib::NCompression;
			using namespace NMib::NStr;

			auto fMakeByteVector = [](uint8 const *_pData, aint _nLen) -> CByteVector
				{
					CByteVector Vec;
					Vec.f_SetLen(_nLen);
					if (_nLen)
						NMib::NMemory::fg_MemCopy(Vec.f_GetArray(), _pData, _nLen);
					return Vec;
				}
			;

			auto fTestRoundTrip = [&](uint8 const *_pData, aint _nLen)
				{
					CByteVector Source = fMakeByteVector(_pData, _nLen);
					CByteVector Compressed = fg_CompressMiniZ(Source);
					CByteVector Decompressed = fg_DecompressMiniZ(Compressed);

					DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
					DMibExpect(Decompressed, ==, Source)(NMib::NTest::ETestFlag_NoValues);
				}
			;

			DMibTestCategory("Random")
			{
				NMib::NMisc::CRandomShiftRNG Random;
				uint8 TestBuffer[2048];
				for (aint iByte = 0; iByte < 2048; ++iByte)
					TestBuffer[iByte] = Random.f_GetValue<uint8>();

				fTestRoundTrip(TestBuffer, 2048);
			};

			DMibTestCategory("SingleByte")
			{
				uint8 Data[] = {42};
				fTestRoundTrip(Data, 1);
			};

			DMibTestCategory("SingleSymbol")
			{
				uint8 Data[1024];
				for (aint iByte = 0; iByte < 1024; ++iByte)
					Data[iByte] = 0xAA;
				fTestRoundTrip(Data, 1024);
			};

			DMibTestCategory("TwoSymbols")
			{
				uint8 Data[512];
				for (aint iByte = 0; iByte < 512; ++iByte)
					Data[iByte] = (iByte & 1) ? 0xFF : 0x00;
				fTestRoundTrip(Data, 512);
			};

			DMibTestCategory("AllByteValues")
			{
				uint8 Data[256];
				for (aint iByte = 0; iByte < 256; ++iByte)
					Data[iByte] = (uint8)iByte;
				fTestRoundTrip(Data, 256);
			};

			DMibTestCategory("SkewedDistribution")
			{
				uint8 Data[1024];
				for (aint iByte = 0; iByte < 1024; ++iByte)
					Data[iByte] = (iByte < 900) ? 'A' : (uint8)(iByte & 0x1F);
				fTestRoundTrip(Data, 1024);
			};

			DMibTestCategory("SmallSizes")
			{
				for (aint nSize = 2; nSize <= 16; ++nSize)
				{
					DMibTestCategory("{} bytes"_f << nSize)
					{
						uint8 Data[16];
						for (aint iByte = 0; iByte < nSize; ++iByte)
							Data[iByte] = (uint8)(iByte * 17);
						fTestRoundTrip(Data, nSize);
					};
				}
			};

			DMibTestCategory("CompressionEfficiency")
			{
				uint8 Data[4096];
				for (aint iByte = 0; iByte < 4096; ++iByte)
					Data[iByte] = 'X';

				CByteVector Source = fMakeByteVector(Data, 4096);
				CByteVector Compressed = fg_CompressMiniZ(Source);

				// Highly repetitive data should compress significantly
				DMibExpect(Compressed.f_GetLen(), <, Source.f_GetLen());

				CByteVector Decompressed = fg_DecompressMiniZ(Compressed);
				DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
			};

			DMibTestCategory("LargeInput")
			{
				constexpr aint c_nSize = 65536;
				uint8 Data[c_nSize];
				NMib::NMisc::CRandomShiftRNG Random;
				for (aint iByte = 0; iByte < c_nSize; ++iByte)
					Data[iByte] = Random.f_GetValue<uint8>();
				fTestRoundTrip(Data, c_nSize);
			};

			DMibTestCategory("RepeatedPattern")
			{
				uint8 Data[2048];
				for (aint iByte = 0; iByte < 2048; ++iByte)
					Data[iByte] = (uint8)(iByte % 5);
				fTestRoundTrip(Data, 2048);
			};

			DMibTestCategory("Empty")
			{
				CByteVector Source;
				CByteVector Compressed = fg_CompressMiniZ(Source);
				CByteVector Decompressed = fg_DecompressMiniZ(Compressed);
				DMibExpect(Decompressed.f_GetLen(), ==, (umint)0);
			};

			DMibTestCategory("MalformedInput")
			{
				using namespace NMib::NStr;

				DMibTestCategory("TruncatedData")
				{
					uint8 Data[] = {0x01, 0x02, 0x03};
					DMibExpectException
						(
							fg_DecompressMiniZ(fMakeByteVector(Data, sizeof(Data)))
							, DMibErrorInstance
							(
								"MiniZ decompression failed: cannot make progress (corrupted or truncated input) (input size: 3 bytes, consumed: 3 bytes, output so far: 0 bytes)"
							)
						)
					;
				};

				DMibTestCategory("GarbageData")
				{
					uint8 Data[] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8};
					DMibExpectException
						(
							fg_DecompressMiniZ(fMakeByteVector(Data, sizeof(Data)))
							, DMibErrorInstance("MiniZ decompression failed: failed (corrupted data) (input size: 8 bytes, consumed: 1 bytes, output so far: 0 bytes)")
						)
					;
				};

				DMibTestCategory("TruncatedCompressedStream")
				{
					uint8 Source[256];
					for (aint iByte = 0; iByte < 256; ++iByte)
						Source[iByte] = (uint8)iByte;

					CByteVector Compressed = fg_CompressMiniZ(fMakeByteVector(Source, sizeof(Source)));

					// Truncate the compressed data
					umint nTruncatedLen = Compressed.f_GetLen() / 2;
					Compressed.f_SetLen(nTruncatedLen);

					DMibExpectException
						(
							fg_DecompressMiniZ(Compressed)
							, DMibErrorInstance
							(
								"MiniZ decompression failed: cannot make progress (corrupted or truncated input) (input size: {} bytes, consumed: {} bytes, output so far: 125 bytes)"_f
								<< nTruncatedLen
								<< nTruncatedLen
							)
						)
					;
				};

				DMibTestCategory("SingleByte")
				{
					uint8 Data[] = {0x00};
					DMibExpectException
						(
							fg_DecompressMiniZ(fMakeByteVector(Data, sizeof(Data)))
							, DMibErrorInstance
							(
								"MiniZ decompression failed: cannot make progress (corrupted or truncated input) (input size: 1 bytes, consumed: 1 bytes, output so far: 0 bytes)"
							)
						)
					;
				};

				DMibTestCategory("EmptyInput")
				{
					DMibExpectException
						(
							fg_DecompressMiniZ(fMakeByteVector(nullptr, 0))
							, DMibErrorInstance
							(
								"MiniZ decompression failed: cannot make progress (corrupted or truncated input) (input size: 0 bytes, consumed: 0 bytes, output so far: 0 bytes)"
							)
						)
					;
				};
			};
		};
	}

	DMibTestRegister(CMiniZ_Tests, Malterlib::Compression);
}
