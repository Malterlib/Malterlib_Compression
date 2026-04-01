// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Compression/Huffman>
#include <Mib/Test/Exception>

namespace
{
	struct CHuffman_Tests : public NMib::NTest::CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("Huffman")
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

				auto fTestRoundTrip = [&](uint8 const *_pData, aint _nLen)
					{
						CByteVector Source = fMakeByteVector(_pData, _nLen);
						CByteVector Compressed = fg_CompressHuffman(Source);
						CByteVector Decompressed = fg_DecompressHuffman(Compressed);

						DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
						DMibExpect(Decompressed, ==, Source)(NMib::NTest::ETestFlag_NoValues);
					}
				;

				auto fExpectMalformedHuffman = [&](uint8 const *_pData, aint _nLen)
					{
						DMibExpectException
							(
								fg_DecompressHuffman(fMakeByteVector(_pData, _nLen))
								, DMibErrorInstance("Malformed Huffman compressed data")
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
					CByteVector Compressed = fg_CompressHuffman(Source);

					// Single-symbol data should compress significantly
					DMibExpect(Compressed.f_GetLen(), <, Source.f_GetLen());

					CByteVector Decompressed = fg_DecompressHuffman(Compressed);
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
					CByteVector Source;
					CByteVector Compressed = fg_CompressHuffman(Source);
					CByteVector Decompressed = fg_DecompressHuffman(Compressed);
					DMibExpect(Decompressed.f_GetLen(), ==, (umint)0);
				};

				DMibTestCategory("ExplicitMaxDestinationLen")
				{
					uint8 Data[] = {0x10, 0x20, 0x10, 0x20, 0x10};
					CByteVector Source = fMakeByteVector(Data, sizeof(Data));
					CByteVector Compressed = fg_CompressHuffman(Source);
					CByteVector Decompressed = fg_DecompressHuffman(Compressed, Source.f_GetLen());

					DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
					DMibExpect(Decompressed, ==, Source)(NMib::NTest::ETestFlag_NoValues);
				};

				DMibTestCategory("DeepTree")
				{
					// Fibonacci-frequency distribution for 27 symbols creates a tree of
					// depth 26, requiring codes up to 26 bits. With 7-bit alignment offset
					// this needs a 33-bit window, which would overflow a uint32 window.
					constexpr aint c_nSymbols = 27;
					int32 Freq[c_nSymbols];
					Freq[0] = 1;
					Freq[1] = 1;
					for (aint i = 2; i < c_nSymbols; ++i)
						Freq[i] = Freq[i - 1] + Freq[i - 2];

					aint nTotalLen = 0;
					for (aint i = 0; i < c_nSymbols; ++i)
						nTotalLen += Freq[i];

					CByteVector Source;
					Source.f_SetLen(nTotalLen);
					uint8 *pData = Source.f_GetArray();
					aint iPos = 0;
					for (aint i = 0; i < c_nSymbols; ++i)
						for (int32 j = 0; j < Freq[i]; ++j)
							pData[iPos++] = (uint8)i;

					CByteVector Compressed = fg_CompressHuffman(Source);
					CByteVector Decompressed = fg_DecompressHuffman(Compressed);

					DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
					DMibExpect(Decompressed, ==, Source)(NMib::NTest::ETestFlag_NoValues);
				};

				DMibTestCategory("MalformedInput")
				{
					DMibTestCategory("ShortLength")
					{
						uint8 Data[] = {0x01, 0x02, 0x03};
						fExpectMalformedHuffman(Data, sizeof(Data));
					};

					DMibTestCategory("MissingNodeCount")
					{
						uint8 Data[] = {0x01, 0x00, 0x00, 0x00};
						fExpectMalformedHuffman(Data, sizeof(Data));
					};

					DMibTestCategory("EmptyWithTrailingByte")
					{
						uint8 Data[] = {0x00, 0x00, 0x00, 0x00, 0xFF};
						fExpectMalformedHuffman(Data, sizeof(Data));
					};

					DMibTestCategory("TruncatedNodeTable")
					{
						uint8 Data[] = {0x01, 0x00, 0x00, 0x00, 0x00};
						fExpectMalformedHuffman(Data, sizeof(Data));
					};

					DMibTestCategory("InconsistentFrequencySum")
					{
						uint8 Data[] =
							{
								0x02, 0x00, 0x00, 0x00
								, 0x00
								, 0x01, 0x00, 0x00, 0x00
								, 'A'
							}
						;
						fExpectMalformedHuffman(Data, sizeof(Data));
					};

					DMibTestCategory("TruncatedBitstream")
					{
						uint8 Data[] = {0x00, 0xFF};
						CByteVector Compressed = fg_CompressHuffman(fMakeByteVector(Data, sizeof(Data)));
						Compressed.f_SetLen(Compressed.f_GetLen() - 1);

						DMibExpectException
							(
								fg_DecompressHuffman(Compressed)
								, DMibErrorInstance("Malformed Huffman compressed data")
							)
						;
					};

					DMibTestCategory("TrailingByte")
					{
						uint8 Data[] = {0x10, 0x20, 0x10, 0x20, 0x10};
						CByteVector Compressed = fg_CompressHuffman(fMakeByteVector(Data, sizeof(Data)));
						Compressed.f_InsertLast(uint8(0));
						fExpectMalformedHuffman(Compressed.f_GetArray(), aint(Compressed.f_GetLen()));
					};

					DMibTestCategory("TrailingBits")
					{
						uint8 Data[] = {'A', 'B'};
						CByteVector Compressed = fg_CompressHuffman(fMakeByteVector(Data, sizeof(Data)));
						Compressed.f_GetLast() |= 0xFC;
						fExpectMalformedHuffman(Compressed.f_GetArray(), aint(Compressed.f_GetLen()));
					};

					DMibTestCategory("DuplicateAscii")
					{
						// Header with two nodes sharing the same ASCII value
						uint8 Data[] =
							{
								// nDesLen = 2 (little-endian uint32)
								0x02, 0x00, 0x00, 0x00
								// nNodes - 1 = 1 (two nodes)
								, 0x01
								// Node 0: frequency = 1, ascii = 'A'
								, 0x01, 0x00, 0x00, 0x00, 'A'
								// Node 1: frequency = 1, ascii = 'A' (duplicate)
								, 0x01, 0x00, 0x00, 0x00, 'A'
								// bitstream byte
								, 0x00
							}
						;
						fExpectMalformedHuffman(Data, sizeof(Data));
					};

					DMibTestCategory("ExceedsMaxDestinationLen")
					{
						uint8 Data[] =
							{
								0x20, 0x00, 0x00, 0x00
								, 0x00
								, 0x20, 0x00, 0x00, 0x00, 'A'
							}
						;

						DMibExpectException
							(
								fg_DecompressHuffman(fMakeByteVector(Data, sizeof(Data)), 31)
								, DMibErrorInstance("Huffman decompression output exceeds maximum buffer length")
							)
						;
					};
				};
			};
		}
	};

	DMibTestRegister(CHuffman_Tests, Malterlib::Compression);
}
