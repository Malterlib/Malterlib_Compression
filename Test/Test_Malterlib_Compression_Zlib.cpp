// Copyright © 2026 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Compression/ZLib>
#include <Mib/Test/Exception>

namespace
{
	struct CZlib_Tests : public NMib::NTest::CTest
	{
		void f_DoTests()
		{
			using namespace NMib::NCompression;
			using namespace NMib::NContainer;
			using namespace NMib::NStream;
			using namespace NMib::NStr;

			auto fMakeByteVector = [](uint8 const *_pData, aint _nLen) -> CByteVector
				{
					CByteVector Vec;
					Vec.f_SetLen(_nLen);
					NMib::NMemory::fg_MemCopy(Vec.f_GetArray(), _pData, _nLen);
					return Vec;
				}
			;

			// ── ZLib format (raw deflate) ──────────────────────────────────

			DMibTestSuite("ZLib")
			{
				auto fTestRoundTrip = [&](uint8 const *_pData, aint _nLen)
					{
						CByteVector Source = fMakeByteVector(_pData, _nLen);
						CByteVector Compressed = fg_CompressZLib(Source);
						CByteVector Decompressed = fg_DecompressZLib(Compressed);

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
					CByteVector Compressed = fg_CompressZLib(Source);

					// Highly repetitive data should compress significantly
					DMibExpect(Compressed.f_GetLen(), <, Source.f_GetLen());

					CByteVector Decompressed = fg_DecompressZLib(Compressed);
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
					CByteVector Compressed = fg_CompressZLib(Source);
					CByteVector Decompressed = fg_DecompressZLib(Compressed);
					DMibExpect(Decompressed.f_GetLen(), ==, (umint)0);
				};

				DMibTestCategory("MalformedInput")
				{
					DMibTestCategory("GarbageData")
					{
						uint8 Data[] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8};
						DMibExpectException
							(
								fg_DecompressZLib(fMakeByteVector(Data, sizeof(Data)))
								, DMibErrorInstance("End of stream Overrun")
							)
						;
					};

					DMibTestCategory("TruncatedCompressedStream")
					{
						uint8 Source[256];
						for (aint i = 0; i < 256; ++i)
							Source[i] = (uint8)i;

						CByteVector Compressed = fg_CompressZLib(fMakeByteVector(Source, sizeof(Source)));
						Compressed.f_SetLen(Compressed.f_GetLen() / 2);

						DMibExpectException
							(
								fg_DecompressZLib(Compressed)
								, DMibErrorInstance("End of stream Overrun")
							)
						;
					};

					DMibTestCategory("SingleByte")
					{
						uint8 Data[] = {0x00};
						DMibExpectException
							(
								fg_DecompressZLib(fMakeByteVector(Data, sizeof(Data)))
								, DMibErrorInstance("End of stream Overrun")
							)
						;
					};
				};
			};

			// ── GZip format (ByteVector API) ───────────────────────────────

			DMibTestSuite("GZipByteVector")
			{
				auto fTestRoundTrip = [&](uint8 const *_pData, aint _nLen)
					{
						CByteVector Source = fMakeByteVector(_pData, _nLen);
						CByteVector Compressed = fg_CompressGZip(Source);
						CByteVector Decompressed = fg_DecompressGZip(Compressed);

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
					CByteVector Compressed = fg_CompressGZip(Source);

					DMibExpect(Compressed.f_GetLen(), <, Source.f_GetLen());

					CByteVector Decompressed = fg_DecompressGZip(Compressed);
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
					CByteVector Compressed = fg_CompressGZip(Source);
					CByteVector Decompressed = fg_DecompressGZip(Compressed);
					DMibExpect(Decompressed.f_GetLen(), ==, (umint)0);
				};

				DMibTestCategory("MalformedInput")
				{
					DMibTestCategory("GarbageData")
					{
						uint8 Data[] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8};
						DMibExpectException
							(
								fg_DecompressGZip(fMakeByteVector(Data, sizeof(Data)))
								, DMibErrorInstance("Compression error: -3")
							)
						;
					};
				};
			};

			// ── GZip format (Stream API) ───────────────────────────────────

			DMibTestSuite("GZipStream")
			{
				auto fTestStreamRoundTrip = [&](uint8 const *_pData, aint _nLen, ECompressZlibLevel _Level = ECompressZlibLevel_Best)
					{
						CByteVector Source = fMakeByteVector(_pData, _nLen);

						CByteVector CompressedData;
						{
							CBinaryStreamMemoryPtr<> SourceStream;
							SourceStream.f_OpenRead(Source);

							CBinaryStreamMemory<> DestinationStream;

							fg_CompressGZip(SourceStream, DestinationStream, _Level);
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

						DMibExpect(DecompressedData.f_GetLen(), ==, Source.f_GetLen());
						DMibExpect(DecompressedData, ==, Source)(NMib::NTest::ETestFlag_NoValues);
					}
				;

				DMibTestCategory("Random")
				{
					NMib::NMisc::CRandomShiftRNG Random;
					uint8 pTestBuffer[2048];
					for (aint i = 0; i < 2048; ++i)
						pTestBuffer[i] = Random.f_GetValue<uint8>();

					fTestStreamRoundTrip(pTestBuffer, 2048);
				};

				DMibTestCategory("SingleByte")
				{
					uint8 Data[] = {42};
					fTestStreamRoundTrip(Data, 1);
				};

				DMibTestCategory("SingleSymbol")
				{
					uint8 Data[1024];
					for (aint i = 0; i < 1024; ++i)
						Data[i] = 0xAA;
					fTestStreamRoundTrip(Data, 1024);
				};

				DMibTestCategory("AllByteValues")
				{
					uint8 Data[256];
					for (aint i = 0; i < 256; ++i)
						Data[i] = (uint8)i;
					fTestStreamRoundTrip(Data, 256);
				};

				DMibTestCategory("LargeInput")
				{
					constexpr aint c_nSize = 65536;
					uint8 Data[c_nSize];
					NMib::NMisc::CRandomShiftRNG Random;
					for (aint i = 0; i < c_nSize; ++i)
						Data[i] = Random.f_GetValue<uint8>();
					fTestStreamRoundTrip(Data, c_nSize);
				};

				DMibTestCategory("Empty")
				{
					CByteVector Source;

					CBinaryStreamMemoryPtr<> SourceStream;
					SourceStream.f_OpenRead(Source);

					CBinaryStreamMemory<> DestinationStream;

					fg_CompressGZip(SourceStream, DestinationStream);
					CByteVector CompressedData = DestinationStream.f_MoveVectorOptimized();

					CBinaryStreamMemoryPtr<> CompressedStream;
					CompressedStream.f_OpenRead(CompressedData);

					CBinaryStreamMemory<> DecompressedStream;

					fg_DecompressGZip(CompressedStream, DecompressedStream);
					CByteVector DecompressedData = DecompressedStream.f_MoveVectorOptimized();

					DMibExpect(DecompressedData.f_GetLen(), ==, (umint)0);
				};

				DMibTestCategory("CompressionLevels")
				{
					NMib::NMisc::CRandomShiftRNG Random;
					uint8 Data[2048];
					for (aint i = 0; i < 2048; ++i)
						Data[i] = Random.f_GetValue<uint8>();

					// Test all compression levels produce valid output
					for (aint nLevel = ECompressZlibLevel_Fastest; nLevel <= ECompressZlibLevel_Best; ++nLevel)
					{
						DMibTestCategory("Level {}"_f << nLevel)
						{
							fTestStreamRoundTrip(Data, 2048, (ECompressZlibLevel)nLevel);
						};
					}
				};

				DMibTestCategory("CompressionLevelEfficiency")
				{
					// Repetitive data to see compression level differences
					uint8 Data[4096];
					for (aint i = 0; i < 4096; ++i)
						Data[i] = (uint8)(i % 13);

					CByteVector Source = fMakeByteVector(Data, 4096);

					CByteVector CompressedFastest;
					{
						CBinaryStreamMemoryPtr<> Src;
						Src.f_OpenRead(Source);
						CBinaryStreamMemory<> Dst;
						fg_CompressGZip(Src, Dst, ECompressZlibLevel_Fastest);
						CompressedFastest = Dst.f_MoveVectorOptimized();
					}

					CByteVector CompressedBest;
					{
						CBinaryStreamMemoryPtr<> Src;
						Src.f_OpenRead(Source);
						CBinaryStreamMemory<> Dst;
						fg_CompressGZip(Src, Dst, ECompressZlibLevel_Best);
						CompressedBest = Dst.f_MoveVectorOptimized();
					}

					// Best compression should produce output no larger than fastest
					DMibExpect(CompressedBest.f_GetLen(), <=, CompressedFastest.f_GetLen());

					// Both should compress below original size for repetitive data
					DMibExpect(CompressedFastest.f_GetLen(), <, Source.f_GetLen());
					DMibExpect(CompressedBest.f_GetLen(), <, Source.f_GetLen());
				};
			};

			// ── ZLib vs GZip format cross-check ────────────────────────────

			DMibTestSuite("ZLibVsGZip")
			{
				DMibTestCategory("DifferentFormats")
				{
					// Verify ZLib and GZip produce different compressed output
					// but both decompress to the same original data
					NMib::NMisc::CRandomShiftRNG Random;
					uint8 Data[512];
					for (aint i = 0; i < 512; ++i)
						Data[i] = Random.f_GetValue<uint8>();

					CByteVector Source = fMakeByteVector(Data, 512);

					CByteVector ZLibCompressed = fg_CompressZLib(Source);
					CByteVector GZipCompressed = fg_CompressGZip(Source);

					// Both formats have different headers, so compressed data should differ
					DMibExpect(ZLibCompressed, !=, GZipCompressed);

					// Both should decompress correctly with their respective decompressors
					CByteVector ZLibDecompressed = fg_DecompressZLib(ZLibCompressed);
					CByteVector GZipDecompressed = fg_DecompressGZip(GZipCompressed);

					DMibExpect(ZLibDecompressed, ==, Source)(NMib::NTest::ETestFlag_NoValues);
					DMibExpect(GZipDecompressed, ==, Source)(NMib::NTest::ETestFlag_NoValues);
				};
			};

			// ── TCBinaryStream_ZLib (streaming wrapper) ────────────────────

			DMibTestSuite("BinaryStreamZLib")
			{
				DMibTestCategory("WriteAndReadBack")
				{
					uint8 Data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

					CBinaryStreamMemory<> BackingStream;

					// Write compressed data
					{
						TCBinaryStream_ZLib<> ZLibStream;
						ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Write);
						ZLibStream.f_FeedBytes(Data, sizeof(Data));
						ZLibStream.f_Close();
					}

					// Read back decompressed data
					BackingStream.f_SetPosition(0);
					{
						TCBinaryStream_ZLib<> ZLibStream;
						ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Read);

						uint8 Result[8] = {};
						ZLibStream.f_ConsumeBytes(Result, 8);

						CByteVector Expected = fMakeByteVector(Data, sizeof(Data));
						CByteVector Actual = fMakeByteVector(Result, sizeof(Result));
						DMibExpect(Actual, ==, Expected)(NMib::NTest::ETestFlag_NoValues);

						ZLibStream.f_Close();
					}
				};

				DMibTestCategory("LargeStreamWriteRead")
				{
					constexpr aint c_nSize = 16384;
					uint8 SourceData[c_nSize];
					NMib::NMisc::CRandomShiftRNG Random;
					for (aint i = 0; i < c_nSize; ++i)
						SourceData[i] = Random.f_GetValue<uint8>();

					CBinaryStreamMemory<> BackingStream;

					{
						TCBinaryStream_ZLib<> ZLibStream;
						ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Write);
						ZLibStream.f_FeedBytes(SourceData, c_nSize);
						ZLibStream.f_Close();
					}

					BackingStream.f_SetPosition(0);
					{
						TCBinaryStream_ZLib<> ZLibStream;
						ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Read);

						uint8 ResultData[c_nSize] = {};
						ZLibStream.f_ConsumeBytes(ResultData, c_nSize);

						CByteVector Expected = fMakeByteVector(SourceData, c_nSize);
						CByteVector Actual = fMakeByteVector(ResultData, c_nSize);
						DMibExpect(Actual, ==, Expected)(NMib::NTest::ETestFlag_NoValues);

						ZLibStream.f_Close();
					}
				};

				DMibTestCategory("MultipleChunkedWrites")
				{
					constexpr aint c_nChunkSize = 256;
					constexpr aint c_nChunks = 8;
					constexpr aint c_nTotal = c_nChunkSize * c_nChunks;
					uint8 SourceData[c_nTotal];
					for (aint i = 0; i < c_nTotal; ++i)
						SourceData[i] = (uint8)(i % 251);

					CBinaryStreamMemory<> BackingStream;

					{
						TCBinaryStream_ZLib<> ZLibStream;
						ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Write);

						for (aint iChunk = 0; iChunk < c_nChunks; ++iChunk)
							ZLibStream.f_FeedBytes(SourceData + iChunk * c_nChunkSize, c_nChunkSize);

						ZLibStream.f_Close();
					}

					BackingStream.f_SetPosition(0);
					{
						TCBinaryStream_ZLib<> ZLibStream;
						ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Read);

						uint8 ResultData[c_nTotal] = {};
						ZLibStream.f_ConsumeBytes(ResultData, c_nTotal);

						CByteVector Expected = fMakeByteVector(SourceData, c_nTotal);
						CByteVector Actual = fMakeByteVector(ResultData, c_nTotal);
						DMibExpect(Actual, ==, Expected)(NMib::NTest::ETestFlag_NoValues);

						ZLibStream.f_Close();
					}
				};

				DMibTestCategory("StreamPosition")
				{
					CBinaryStreamMemory<> BackingStream;

					{
						TCBinaryStream_ZLib<> ZLibStream;
						ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Write);

						DMibExpect(ZLibStream.f_GetPosition(), ==, NMib::NStream::CFilePos(0));

						uint8 Data[32];
						for (aint i = 0; i < 32; ++i)
							Data[i] = (uint8)i;

						ZLibStream.f_FeedBytes(Data, 32);
						DMibExpect(ZLibStream.f_GetPosition(), ==, NMib::NStream::CFilePos(32));

						ZLibStream.f_Close();
					}
				};

				DMibTestCategory("CompressionLevels")
				{
					uint8 Data[1024];
					for (aint i = 0; i < 1024; ++i)
						Data[i] = (uint8)(i % 7);

					for (aint nLevel = ECompressZlibLevel_Fastest; nLevel <= ECompressZlibLevel_Best; ++nLevel)
					{
						DMibTestCategory("Level {}"_f << nLevel)
						{
							CBinaryStreamMemory<> BackingStream;

							{
								TCBinaryStream_ZLib<> ZLibStream((ECompressZlibLevel)nLevel);
								ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Write);
								ZLibStream.f_FeedBytes(Data, 1024);
								ZLibStream.f_Close();
							}

							BackingStream.f_SetPosition(0);
							{
								TCBinaryStream_ZLib<> ZLibStream;
								ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Read);

								uint8 ResultData[1024] = {};
								ZLibStream.f_ConsumeBytes(ResultData, 1024);

								CByteVector Expected = fMakeByteVector(Data, 1024);
								CByteVector Actual = fMakeByteVector(ResultData, 1024);
								DMibExpect(Actual, ==, Expected)(NMib::NTest::ETestFlag_NoValues);

								ZLibStream.f_Close();
							}
						};
					}
				};

				DMibTestCategory("Errors")
				{
					DMibTestCategory("ReadFromWriteOnlyStream")
					{
						CBinaryStreamMemory<> BackingStream;
						TCBinaryStream_ZLib<> ZLibStream;
						ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Write);

						uint8 Buf[1];
						DMibExpectException
							(
								ZLibStream.f_ConsumeBytes(Buf, 1)
								, DMibImpExceptionInstance(NMib::NFile::CExceptionFile, "File was not opened for read.")
							)
						;
						ZLibStream.f_Close();
					};

					DMibTestCategory("WriteToReadOnlyStream")
					{
						// Create a valid compressed stream first
						CBinaryStreamMemory<> BackingStream;
						{
							TCBinaryStream_ZLib<> Writer;
							Writer.f_Open(&BackingStream, NMib::NFile::EFileOpen_Write);
							uint8 Data[] = {1, 2, 3};
							Writer.f_FeedBytes(Data, sizeof(Data));
							Writer.f_Close();
						}

						BackingStream.f_SetPosition(0);
						TCBinaryStream_ZLib<> ZLibStream;
						ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Read);

						uint8 Buf[] = {0};
						DMibExpectException
							(
								ZLibStream.f_FeedBytes(Buf, 1)
								, DMibImpExceptionInstance(NMib::NFile::CExceptionFile, "File was not opened for write.")
							)
						;
						ZLibStream.f_Close();
					};

					DMibTestCategory("ReadPastEndOfFile")
					{
						CBinaryStreamMemory<> BackingStream;
						{
							TCBinaryStream_ZLib<> Writer;
							Writer.f_Open(&BackingStream, NMib::NFile::EFileOpen_Write);
							uint8 Data[] = {1, 2, 3};
							Writer.f_FeedBytes(Data, sizeof(Data));
							Writer.f_Close();
						}

						BackingStream.f_SetPosition(0);
						TCBinaryStream_ZLib<> ZLibStream;
						ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Read);

						uint8 Buf[16];
						DMibExpectException
							(
								ZLibStream.f_ConsumeBytes(Buf, 16)
								, DMibImpExceptionInstance(NMib::NFile::CExceptionFile, "Would read past end of file.")
							)
						;
						ZLibStream.f_Close();
					};

					DMibTestCategory("SetLengthNotSupported")
					{
						CBinaryStreamMemory<> BackingStream;
						TCBinaryStream_ZLib<> ZLibStream;
						ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Write);

						DMibExpectException
							(
								ZLibStream.f_SetLength(100)
								, DMibErrorInstance("Not supported")
							)
						;
						ZLibStream.f_Close();
					};

					DMibTestCategory("InvalidStreamVersion")
					{
						CBinaryStreamMemory<> BackingStream;

						// Write a header with an invalid version number
						uint64 FileLen = uint64(10) | 0x8000000000000000UL;
						uint32 Version = 99;
						uint64 CompressedLen = 100;

						BackingStream << FileLen;
						BackingStream << Version;
						BackingStream << CompressedLen;

						BackingStream.f_SetPosition(0);
						TCBinaryStream_ZLib<> ZLibStream;

						DMibExpectException
							(
								ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Read)
								, DMibImpExceptionInstance(NMib::NFile::CExceptionFile, "Invalid zlib stream version")
							)
						;
					};

					DMibTestCategory("OpenWithReadAndWrite")
					{
						CBinaryStreamMemory<> BackingStream;
						TCBinaryStream_ZLib<> ZLibStream;

						DMibExpectException
							(
								ZLibStream.f_Open(&BackingStream, NMib::NFile::EFileOpen_Read | NMib::NFile::EFileOpen_Write)
								, DMibImpExceptionInstance(NMib::NFile::CExceptionFile, "You must open the file either with read or write access not both at the same time")
							)
						;
					};

					DMibTestCategory("CompressAndDecompressInSameSession")
					{
						CBinaryStreamMemory<> OutStream;
						NMib::NStream::CFilePos BytesWritten = 0;

						CCompress_ZLib Compressor;
						uint8 Data[] = {1, 2, 3, 4};
						Compressor.f_FeedBytes(&OutStream, BytesWritten, Data, sizeof(Data), ECompressZlibFlush_Finish);

						CBinaryStreamMemory<> InStream;
						uint8 Buf[4];
						DMibExpectException
							(
								Compressor.f_ConsumeBytes(&InStream, 0, Buf, sizeof(Buf), ECompressZlibFlush_None)
								, DMibErrorInstance("ZLib cannot compress and decompress in same session.")
							)
						;
					};

					DMibTestCategory("DecompressAndCompressInSameSession")
					{
						// First create valid compressed data
						CBinaryStreamMemory<> CompressedStream;
						{
							NMib::NStream::CFilePos BytesWritten = 0;
							CCompress_ZLib Comp;
							uint8 Data[] = {1, 2, 3, 4};
							Comp.f_FeedBytes(&CompressedStream, BytesWritten, Data, sizeof(Data), ECompressZlibFlush_Finish);
						}

						CompressedStream.f_SetPosition(0);
						CCompress_ZLib Decompressor;
						uint8 Buf[4];
						Decompressor.f_ConsumeBytes(&CompressedStream, 0, Buf, sizeof(Buf), ECompressZlibFlush_None);

						CBinaryStreamMemory<> OutStream;
						NMib::NStream::CFilePos BytesWritten = 0;
						DMibExpectException
							(
								Decompressor.f_FeedBytes(&OutStream, BytesWritten, Buf, sizeof(Buf), ECompressZlibFlush_Finish)
								, DMibErrorInstance("ZLib cannot compress and decompress in same session.")
							)
						;
					};

					DMibTestCategory("RanOutOfCompressedData")
					{
						// Create valid compressed data for a large payload
						CBinaryStreamMemory<> CompressedStream;
						{
							NMib::NStream::CFilePos BytesWritten = 0;
							CCompress_ZLib Comp;
							uint8 Data[64];
							for (aint i = 0; i < 64; ++i)
								Data[i] = (uint8)i;
							Comp.f_FeedBytes(&CompressedStream, BytesWritten, Data, sizeof(Data), ECompressZlibFlush_Finish);
						}

						// Truncate the compressed stream
						CompressedStream.f_SetLength(CompressedStream.f_GetLength() / 2);
						CompressedStream.f_SetPosition(0);

						CCompress_ZLib Decompressor;
						uint8 Buf[64];
						DMibExpectException
							(
								Decompressor.f_ConsumeBytes(&CompressedStream, 0, Buf, sizeof(Buf), ECompressZlibFlush_None)
								, DMibErrorInstance("Ran out of compressed data")
							)
						;
					};

					DMibTestCategory("GarbageDataDirectDecompress")
					{
						CBinaryStreamMemory<> GarbageStream;
						uint8 Garbage[] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8};
						GarbageStream.f_FeedBytes(Garbage, sizeof(Garbage));
						GarbageStream.f_SetPosition(0);

						CCompress_ZLib Decompressor;
						uint8 Buf[4];
						DMibExpectException
							(
								Decompressor.f_ConsumeBytes(&GarbageStream, 0, Buf, sizeof(Buf), ECompressZlibFlush_None)
								, DMibErrorInstance("Compression error: -3")
							)
						;
					};
				};
			};
		}
	};

	DMibTestRegister(CZlib_Tests, Malterlib::Compression);
}
