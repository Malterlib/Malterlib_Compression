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
						DMibExpectException(fg_DecompressHuffman(fMakeByteVector(_pData, _nLen)), DMibErrorInstance("Malformed Huffman compressed data"));
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
					for (aint iSymbol = 2; iSymbol < c_nSymbols; ++iSymbol)
						Freq[iSymbol] = Freq[iSymbol - 1] + Freq[iSymbol - 2];

					aint nTotalLen = 0;
					for (aint iSymbol = 0; iSymbol < c_nSymbols; ++iSymbol)
						nTotalLen += Freq[iSymbol];

					CByteVector Source;
					Source.f_SetLen(nTotalLen);
					uint8 *pData = Source.f_GetArray();
					aint iPos = 0;
					for (aint iSymbol = 0; iSymbol < c_nSymbols; ++iSymbol)
					{
						for (int32 iRepeat = 0; iRepeat < Freq[iSymbol]; ++iRepeat)
							pData[iPos++] = (uint8)iSymbol;
					}

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
								// malterlib-format rows
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

						DMibExpectException(fg_DecompressHuffman(Compressed), DMibErrorInstance("Malformed Huffman compressed data"));
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
								// malterlib-format rows
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
								// malterlib-format rows
								0x20, 0x00, 0x00, 0x00
								, 0x00
								, 0x20, 0x00, 0x00, 0x00, 'A'
							}
						;

						DMibExpectException(fg_DecompressHuffman(fMakeByteVector(Data, sizeof(Data)), 31), DMibErrorInstance("Huffman decompression output exceeds maximum buffer length"));
					};
				};
			};
		}
	};

	DMibTestRegister(CHuffman_Tests, Malterlib::Compression);
}
