// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Compression/MiniZ>
#include <Mib/Test/Exception>

namespace
{
	struct CMiniZ_Tests : public NMib::NTest::CTest
	{
		void f_DoTests()
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
					uint8 pTestBuffer[2048];
					for (aint i = 0; i < 2048; ++i)
						pTestBuffer[i] = Random.f_GetValue<uint8>();

					fTestRoundTrip(pTestBuffer, 2048);
				};

				DMibTestCategory("SingleByte")
				{
					uint8 Data[] = {42};
					fTestRoundTrip(Data, 1);
				};

				DMibTestCategory("SingleSymbol")
				{
					uint8 Data[1024];
					for (aint i = 0; i < 1024; ++i)
						Data[i] = 0xAA;
					fTestRoundTrip(Data, 1024);
				};

				DMibTestCategory("TwoSymbols")
				{
					uint8 Data[512];
					for (aint i = 0; i < 512; ++i)
						Data[i] = (i & 1) ? 0xFF : 0x00;
					fTestRoundTrip(Data, 512);
				};

				DMibTestCategory("AllByteValues")
				{
					uint8 Data[256];
					for (aint i = 0; i < 256; ++i)
						Data[i] = (uint8)i;
					fTestRoundTrip(Data, 256);
				};

				DMibTestCategory("SkewedDistribution")
				{
					uint8 Data[1024];
					for (aint i = 0; i < 1024; ++i)
						Data[i] = (i < 900) ? 'A' : (uint8)(i & 0x1F);
					fTestRoundTrip(Data, 1024);
				};

				DMibTestCategory("SmallSizes")
				{
					for (aint nSize = 2; nSize <= 16; ++nSize)
					{
						DMibTestCategory("{} bytes"_f << nSize)
						{
							uint8 Data[16];
							for (aint i = 0; i < nSize; ++i)
								Data[i] = (uint8)(i * 17);
							fTestRoundTrip(Data, nSize);
						};
					}
				};

				DMibTestCategory("CompressionEfficiency")
				{
					uint8 Data[4096];
					for (aint i = 0; i < 4096; ++i)
						Data[i] = 'X';

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
					for (aint i = 0; i < c_nSize; ++i)
						Data[i] = Random.f_GetValue<uint8>();
					fTestRoundTrip(Data, c_nSize);
				};

				DMibTestCategory("RepeatedPattern")
				{
					uint8 Data[2048];
					for (aint i = 0; i < 2048; ++i)
						Data[i] = (uint8)(i % 5);
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
						for (aint i = 0; i < 256; ++i)
							Source[i] = (uint8)i;

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
	};

	DMibTestRegister(CMiniZ_Tests, Malterlib::Compression);
}
