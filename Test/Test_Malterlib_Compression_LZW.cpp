// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Compression/Huffman>
#include <Mib/Compression/LZW>
#include <Mib/Compression/ZLib>
#include <Mib/Test/Exception>

namespace
{
	struct CCompression_Tests : public NMib::NTest::CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("LZW")
			{
				using namespace NMib::NContainer;
				using namespace NMib::NCompression;
				using namespace NMib::NStr;

				auto fMakeByteVector = [](uint8 const *_pData, aint _nLen) -> CByteVector
					{
						CByteVector Vec;
						Vec.f_SetLen(_nLen);
						NMib::NMemory::fg_MemCopy(Vec.f_GetArray(), _pData, _nLen);
						return Vec;
					}
				;

				auto fTestRoundTrip = [&](uint8 const *_pData, aint _nLen, umint _nBitsPerCode = gc_LZWDefaultBitsPerCode)
					{
						CByteVector Source = fMakeByteVector(_pData, _nLen);
						CByteVector Compressed = fg_CompressLZW(Source, _nBitsPerCode);
						CByteVector Decompressed = fg_DecompressLZW(Compressed);

						DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
						DMibExpect(Decompressed, ==, Source)(NMib::NTest::ETestFlag_NoValues);
					}
				;

				auto fExpectMalformedLZW = [&](uint8 const *_pData, aint _nLen)
					{
						DMibExpectException
							(
								fg_DecompressLZW(fMakeByteVector(_pData, _nLen))
								, DMibErrorInstance("Malformed LZW compressed data")
							)
						;
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
					CByteVector Compressed = fg_CompressLZW(Source);

					// Highly repetitive data should compress significantly
					DMibExpect(Compressed.f_GetLen(), <, Source.f_GetLen());

					CByteVector Decompressed = fg_DecompressLZW(Compressed);
					DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
				};

				DMibTestCategory("LargeInput")
				{
					constexpr aint c_nSize = 32768;
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
					for (umint nBits = 9; nBits <= 16; ++nBits)
					{
						DMibTestCategory("{} bits"_f << nBits)
						{
							CByteVector Source;
							CByteVector Compressed = fg_CompressLZW(Source, nBits);
							CByteVector Decompressed = fg_DecompressLZW(Compressed);

							DMibExpect(Compressed.f_GetLen(), ==, (umint)5);
							DMibExpect(Compressed[4], ==, uint8(nBits));
							DMibExpect(Decompressed.f_GetLen(), ==, (umint)0);
						};
					}
				};

				DMibTestCategory("DifferentBitsPerCode")
				{
					NMib::NMisc::CRandomShiftRNG Random;
					uint8 Data[1024];
					for (aint i = 0; i < 1024; ++i)
						Data[i] = Random.f_GetValue<uint8>();

					for (umint nBits = 9; nBits <= 16; ++nBits)
					{
						DMibTestCategory("{} bits"_f << nBits)
						{
							fTestRoundTrip(Data, 1024, nBits);
						};
					}
				};

				DMibTestCategory("DictionaryFull")
				{
					// With 9-bit codes, dictionary has only 256 entries (codes 256-511)
					// Use enough data to exceed the dictionary capacity
					uint8 Data[2048];
					NMib::NMisc::CRandomShiftRNG Random;
					for (aint i = 0; i < 2048; ++i)
						Data[i] = Random.f_GetValue<uint8>();
					fTestRoundTrip(Data, 2048, 9);
				};

				DMibTestCategory("ExplicitMaxDestinationLen")
				{
					uint8 Data[] = {0x10, 0x20, 0x10, 0x20, 0x10};
					CByteVector Source = fMakeByteVector(Data, sizeof(Data));
					CByteVector Compressed = fg_CompressLZW(Source);
					CByteVector Decompressed = fg_DecompressLZW(Compressed, Source.f_GetLen());

					DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
					DMibExpect(Decompressed, ==, Source)(NMib::NTest::ETestFlag_NoValues);
				};

				DMibTestCategory("MalformedInput")
				{
					DMibTestCategory("ShortHeader")
					{
						uint8 Data[] = {0x01, 0x02, 0x03, 0x04};
						fExpectMalformedLZW(Data, sizeof(Data));
					};

					DMibTestCategory("EmptyWithTrailingByte")
					{
						uint8 Data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0xFF};
						fExpectMalformedLZW(Data, sizeof(Data));
					};

					DMibTestCategory("InvalidBitsPerCodeLow")
					{
						// bitsPerCode = 8 (too small)
						uint8 Data[] = {0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00};
						fExpectMalformedLZW(Data, sizeof(Data));
					};

					DMibTestCategory("InvalidBitsPerCodeHigh")
					{
						// bitsPerCode = 21 (too large)
						uint8 Data[] = {0x01, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00};
						fExpectMalformedLZW(Data, sizeof(Data));
					};

					DMibTestCategory("EmptyWithInvalidBitsPerCode")
					{
						uint8 Data[] = {0x00, 0x00, 0x00, 0x00, 0x00};
						fExpectMalformedLZW(Data, sizeof(Data));
					};

					DMibTestCategory("TruncatedBitstream")
					{
						uint8 Data[] = {0x00, 0xFF};
						CByteVector Compressed = fg_CompressLZW(fMakeByteVector(Data, sizeof(Data)));
						Compressed.f_SetLen(Compressed.f_GetLen() - 1);

						DMibExpectException
							(
								fg_DecompressLZW(Compressed)
								, DMibErrorInstance("Malformed LZW compressed data")
							)
						;
					};

					DMibTestCategory("TrailingByte")
					{
						uint8 Data[] = {0x10, 0x20, 0x10, 0x20, 0x10};
						CByteVector Compressed = fg_CompressLZW(fMakeByteVector(Data, sizeof(Data)));
						Compressed.f_InsertLast(uint8(0));
						fExpectMalformedLZW(Compressed.f_GetArray(), aint(Compressed.f_GetLen()));
					};

					DMibTestCategory("TrailingBits")
					{
						// 3-byte input produces 3 codes * 12 bits = 36 bits = 4.5 bytes
						// Last byte has 4 trailing bits that must be zero
						uint8 Data[] = {'A', 'B', 'C'};
						CByteVector Compressed = fg_CompressLZW(fMakeByteVector(Data, sizeof(Data)));
						Compressed.f_GetLast() |= 0xF0;
						fExpectMalformedLZW(Compressed.f_GetArray(), aint(Compressed.f_GetLen()));
					};

					DMibTestCategory("ExceedsMaxDestinationLen")
					{
						uint8 Data[] = {0x10, 0x20, 0x10, 0x20, 0x10};
						CByteVector Compressed = fg_CompressLZW(fMakeByteVector(Data, sizeof(Data)));

						DMibExpectException
							(
								fg_DecompressLZW(Compressed, 4)
								, DMibErrorInstance("LZW decompression output exceeds maximum buffer length")
							)
						;
					};
				};
			};
		}
	};

	DMibTestRegister(CCompression_Tests, Malterlib::Compression);
}
