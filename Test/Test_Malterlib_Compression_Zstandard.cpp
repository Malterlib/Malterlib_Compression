// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Compression/Zstandard>
#include <Mib/Compression/ZstandardAsync>
#include <Mib/Test/Exception>

namespace
{
	using namespace NMib;
	using namespace NMib::NStr;
	using namespace NMib::NCompression;
	using namespace NMib::NContainer;
	using namespace NMib::NConcurrency;
	using namespace NMib::NStream;

	constexpr umint gc_TestSize = NFile::gc_IdealIoSize * 16 + 125;

	constexpr uint32 gc_CompressionLevel = 1;

	struct CZstandard_Tests : public NMib::NTest::CTest
	{
		CByteVector f_MakeByteVector(uint8 const *_pData, aint _nLen);
		CByteVector f_GetSemiRandomTestBuffer();
		void f_TestSyncRoundTrip(uint8 const *_pData, aint _nLen, int32 _CompressionLevel = gc_CompressionLevel);
		TCFuture<void> f_TestAsyncRoundTrip(CByteVector _TestBuffer, CZStandardCompressionOptions _Options);
		void f_DoTests();
	};

	CByteVector CZstandard_Tests::f_MakeByteVector(uint8 const *_pData, aint _nLen)
	{
		CByteVector Vec;
		Vec.f_SetLen(_nLen);
		NMib::NMemory::fg_MemCopy(Vec.f_GetArray(), _pData, _nLen);
		return Vec;
	}

	CByteVector CZstandard_Tests::f_GetSemiRandomTestBuffer()
	{
		NMib::NMisc::CRandomShiftRNG Random;

		CByteVector TestBuffer;
		TestBuffer.f_SetLen(gc_TestSize);
		{
			auto pArray = TestBuffer.f_GetArray();
			for (aint iByte = 0; iByte < gc_TestSize; ++iByte)
			{
				if ((iByte & 7) == 0)
					pArray[iByte] = Random.f_GetValue<uint8>();
				else
					pArray[iByte] = 0;
			}
		}

		return TestBuffer;
	}

	void CZstandard_Tests::f_TestSyncRoundTrip(uint8 const *_pData, aint _nLen, int32 _CompressionLevel)
	{
		CByteVector Source = f_MakeByteVector(_pData, _nLen);
		CByteVector Compressed = fg_CompressZstandard(Source, _CompressionLevel);
		CByteVector Decompressed = fg_DecompressZstandard(Compressed);

		DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
		DMibExpect(Decompressed, ==, Source)(ETestFlag_NoValues);
	}

	TCFuture<void> CZstandard_Tests::f_TestAsyncRoundTrip(CByteVector _TestBuffer, CZStandardCompressionOptions _Options)
	{
		auto fStreamData = [InTestBuffer = _TestBuffer]() -> TCAsyncGenerator<CIOByteVector>
			{
				co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());

				umint Len = InTestBuffer.f_GetLen();
				auto *pBuffer = InTestBuffer.f_GetArray();
				while (Len)
				{
					auto ThisTime = fg_Min(Len, NFile::gc_IdealIoSize);

					co_yield CIOByteVector(pBuffer, ThisTime);

					Len -= ThisTime;
					pBuffer += ThisTime;
				}

				co_return {};
			}
		;

		auto DecompressedGenerator = fg_DecompressZstandardAsync(fg_CompressZstandardAsync(fg_CallSafe(fg_Move(fStreamData)), _Options));

		CIOByteVector DecompressedData;
		for (auto iData = co_await fg_Move(DecompressedGenerator).f_GetPipelinedIterator(); iData; co_await ++iData)
			DecompressedData.f_Insert(fg_Move(*iData));

		DMibExpect(DecompressedData.f_GetLen(), ==, _TestBuffer.f_GetLen());
		DMibExpect(DecompressedData, ==, _TestBuffer)(ETestFlag_NoValues);

		co_return {};
	}

	void CZstandard_Tests::f_DoTests()
	{
		DMibTestSuite("Synchronous")
		{
			DMibTestCategory("SemiRandom")
			{
				auto TestBuffer = f_GetSemiRandomTestBuffer();

				CByteVector CompressedData = fg_CompressZstandard(TestBuffer, gc_CompressionLevel);
				CByteVector DecompressedData = fg_DecompressZstandard(CompressedData);

				DMibExpect(DecompressedData.f_GetLen(), ==, TestBuffer.f_GetLen());
				DMibExpect(DecompressedData, ==, TestBuffer)(ETestFlag_NoValues);
			};

			DMibTestCategory("Empty")
			{
				CByteVector Source;
				CByteVector Compressed = fg_CompressZstandard(Source, gc_CompressionLevel);
				CByteVector Decompressed = fg_DecompressZstandard(Compressed);
				DMibExpect(Decompressed.f_GetLen(), ==, (umint)0);
			};

			DMibTestCategory("SingleByte")
			{
				uint8 Data[] = {42};
				f_TestSyncRoundTrip(Data, 1);
			};

			DMibTestCategory("SingleSymbol")
			{
				uint8 Data[1024];
				for (aint iByte = 0; iByte < 1024; ++iByte)
					Data[iByte] = 0xAA;
				f_TestSyncRoundTrip(Data, 1024);
			};

			DMibTestCategory("TwoSymbols")
			{
				uint8 Data[512];
				for (aint iByte = 0; iByte < 512; ++iByte)
					Data[iByte] = (iByte & 1) ? 0xFF : 0x00;
				f_TestSyncRoundTrip(Data, 512);
			};

			DMibTestCategory("AllByteValues")
			{
				uint8 Data[256];
				for (aint iByte = 0; iByte < 256; ++iByte)
					Data[iByte] = (uint8)iByte;
				f_TestSyncRoundTrip(Data, 256);
			};

			DMibTestCategory("SkewedDistribution")
			{
				uint8 Data[1024];
				for (aint iByte = 0; iByte < 1024; ++iByte)
					Data[iByte] = (iByte < 900) ? 'A' : (uint8)(iByte & 0x1F);
				f_TestSyncRoundTrip(Data, 1024);
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
						f_TestSyncRoundTrip(Data, nSize);
					};
				}
			};

			DMibTestCategory("RepeatedPattern")
			{
				uint8 Data[2048];
				for (aint iByte = 0; iByte < 2048; ++iByte)
					Data[iByte] = (uint8)(iByte % 5);
				f_TestSyncRoundTrip(Data, 2048);
			};

			DMibTestCategory("FullyRandom")
			{
				NMib::NMisc::CRandomShiftRNG Random;
				uint8 Data[2048];
				for (aint iByte = 0; iByte < 2048; ++iByte)
					Data[iByte] = Random.f_GetValue<uint8>();
				f_TestSyncRoundTrip(Data, 2048);
			};

			DMibTestCategory("LargeInput")
			{
				constexpr aint c_nSize = 65536;
				CByteVector Source;
				Source.f_SetLen(c_nSize);
				NMib::NMisc::CRandomShiftRNG Random;
				for (aint iByte = 0; iByte < c_nSize; ++iByte)
					Source[iByte] = Random.f_GetValue<uint8>();

				CByteVector Compressed = fg_CompressZstandard(Source, gc_CompressionLevel);
				CByteVector Decompressed = fg_DecompressZstandard(Compressed);

				DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
				DMibExpect(Decompressed, ==, Source)(ETestFlag_NoValues);
			};

			DMibTestCategory("CompressionEfficiency")
			{
				uint8 Data[4096];
				for (aint iByte = 0; iByte < 4096; ++iByte)
					Data[iByte] = 'X';

				CByteVector Source = f_MakeByteVector(Data, 4096);
				CByteVector Compressed = fg_CompressZstandard(Source, gc_CompressionLevel);

				// Single-symbol data should compress significantly
				DMibExpect(Compressed.f_GetLen(), <, Source.f_GetLen());

				CByteVector Decompressed = fg_DecompressZstandard(Compressed);
				DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
				DMibExpect(Decompressed, ==, Source)(ETestFlag_NoValues);
			};

			DMibTestCategory("CompressionLevels")
			{
				NMib::NMisc::CRandomShiftRNG Random;
				uint8 Data[2048];
				for (aint iByte = 0; iByte < 2048; ++iByte)
				{
					if ((iByte & 3) == 0)
						Data[iByte] = Random.f_GetValue<uint8>();
					else
						Data[iByte] = 0;
				}

				for (int32 nLevel = 1; nLevel <= 19; nLevel += 6)
				{
					DMibTestCategory("Level {}"_f << nLevel)
					{
						f_TestSyncRoundTrip(Data, 2048, nLevel);
					};
				}
			};

			DMibTestCategory("MalformedInput")
			{
				DMibTestCategory("GarbageData")
				{
					uint8 Data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
					DMibExpectException(fg_DecompressZstandard(f_MakeByteVector(Data, sizeof(Data))), DMibErrorInstance("Failed to decompress with zstd: Src size is incorrect"));
				};

				DMibTestCategory("TruncatedFrame")
				{
					uint8 Data[256];
					for (aint iByte = 0; iByte < 256; ++iByte)
						Data[iByte] = (uint8)iByte;
					CByteVector Compressed = fg_CompressZstandard(f_MakeByteVector(Data, sizeof(Data)));

					// Truncate the compressed data
					Compressed.f_SetLen(Compressed.f_GetLen() / 2);

					DMibExpectException(fg_DecompressZstandard(Compressed), DMibErrorInstance("Failed to decompress with zstd: Src size is incorrect"));
				};

				DMibTestCategory("CorruptedData")
				{
					uint8 Data[256];
					for (aint iByte = 0; iByte < 256; ++iByte)
						Data[iByte] = (uint8)iByte;
					CByteVector Source = f_MakeByteVector(Data, sizeof(Data));
					CByteVector Compressed = fg_CompressZstandard(Source);

					// Flip bits in the middle of the compressed data
					umint nMid = Compressed.f_GetLen() / 2;
					Compressed[nMid] ^= 0xFF;
					Compressed[nMid + 1] ^= 0xFF;

					// Corruption should either throw or produce different output
					bool bDetected = false;
					try
					{
						CByteVector Decompressed = fg_DecompressZstandard(Compressed);
						bDetected = (Decompressed != Source);
					}
					catch (...)
					{
						bDetected = true;
					}
					DMibExpectTrue(bDetected);
				};
			};
		};
		DMibTestCategory("Streaming")
		{
			for (umint iKnownSize = 0; iKnownSize < 2; ++iKnownSize)
			{
				DMibTestCategory(iKnownSize == 0 ? "Unknown size" : "Known size")
				{
					for (umint iMultiThreaded = 0; iMultiThreaded < 2; ++iMultiThreaded)
					{
						DMibTestSuite(iMultiThreaded == 0 ? "Serial" : "Parallel") -> TCFuture<void>
						{
							auto TestBuffer = f_GetSemiRandomTestBuffer();

							CZStandardCompressionOptions Options{.m_CompressionLevel = gc_CompressionLevel};
							if (iKnownSize == 1)
								Options.m_KnownSize = gc_TestSize;

							if (iMultiThreaded == 0)
								Options.m_CompressionThreads = 1;

							co_await f_TestAsyncRoundTrip(TestBuffer, Options);

							co_return {};
						};
					}
				};
			}

			DMibTestSuite("Empty") -> TCFuture<void>
			{
				CByteVector EmptyBuffer;
				CZStandardCompressionOptions Options{.m_CompressionLevel = gc_CompressionLevel, .m_CompressionThreads = 1};
				co_await f_TestAsyncRoundTrip(EmptyBuffer, Options);
				co_return {};
			};

			DMibTestSuite("SingleByte") -> TCFuture<void>
			{
				uint8 Data[] = {42};
				CByteVector Buffer = f_MakeByteVector(Data, 1);
				CZStandardCompressionOptions Options{.m_CompressionLevel = gc_CompressionLevel, .m_CompressionThreads = 1};
				co_await f_TestAsyncRoundTrip(Buffer, Options);
				co_return {};
			};

			DMibTestSuite("SmallData") -> TCFuture<void>
			{
				uint8 Data[64];
				for (aint iByte = 0; iByte < 64; ++iByte)
					Data[iByte] = (uint8)(iByte * 7);
				CByteVector Buffer = f_MakeByteVector(Data, 64);
				CZStandardCompressionOptions Options{.m_CompressionLevel = gc_CompressionLevel, .m_CompressionThreads = 1};
				co_await f_TestAsyncRoundTrip(Buffer, Options);
				co_return {};
			};

			DMibTestSuite("HighlyCompressible") -> TCFuture<void>
			{
				CByteVector Buffer;
				Buffer.f_SetLen(NFile::gc_IdealIoSize * 4);
				for (umint iByte = 0; iByte < Buffer.f_GetLen(); ++iByte)
					Buffer[iByte] = 0;
				CZStandardCompressionOptions Options{.m_CompressionLevel = gc_CompressionLevel, .m_CompressionThreads = 1};
				co_await f_TestAsyncRoundTrip(Buffer, Options);
				co_return {};
			};

			DMibTestCategory("CompressionLevels")
			{
				for (int32 nLevel = 1; nLevel <= 19; nLevel += 6)
				{
					DMibTestSuite("Level {}"_f << nLevel) -> TCFuture<void>
					{
						NMib::NMisc::CRandomShiftRNG Random;
						CByteVector Buffer;
						Buffer.f_SetLen(4096);
						for (umint iByte = 0; iByte < Buffer.f_GetLen(); ++iByte)
						{
							if ((iByte & 3) == 0)
								Buffer[iByte] = Random.f_GetValue<uint8>();
							else
								Buffer[iByte] = 0;
						}

						CZStandardCompressionOptions Options{.m_CompressionLevel = nLevel, .m_CompressionThreads = 1};
						co_await f_TestAsyncRoundTrip(Buffer, Options);
						co_return {};
					};
				}
			};

			DMibTestSuite("MultipleChunks") -> TCFuture<void>
			{
				// Data larger than a single IO chunk to exercise multi-chunk streaming
				CByteVector Buffer;
				Buffer.f_SetLen(NFile::gc_IdealIoSize * 3 + 500);
				NMib::NMisc::CRandomShiftRNG Random;
				for (umint iByte = 0; iByte < Buffer.f_GetLen(); ++iByte)
				{
					if ((iByte & 7) == 0)
						Buffer[iByte] = Random.f_GetValue<uint8>();
					else
						Buffer[iByte] = 0;
				}
				CZStandardCompressionOptions Options{.m_CompressionLevel = gc_CompressionLevel, .m_CompressionThreads = 1};
				co_await f_TestAsyncRoundTrip(Buffer, Options);
				co_return {};
			};

			DMibTestSuite("KnownSizeSmallData") -> TCFuture<void>
			{
				uint8 Data[128];
				for (aint iByte = 0; iByte < 128; ++iByte)
					Data[iByte] = (uint8)(iByte ^ 0x55);
				CByteVector Buffer = f_MakeByteVector(Data, 128);
				CZStandardCompressionOptions Options{.m_KnownSize = 128, .m_CompressionLevel = gc_CompressionLevel, .m_CompressionThreads = 1};
				co_await f_TestAsyncRoundTrip(Buffer, Options);
				co_return {};
			};

			DMibTestCategory("MalformedInput")
			{
				DMibTestSuite("GarbageData") -> TCFuture<void>
				{
					uint8 GarbageBytes[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
					CByteVector GarbageData = f_MakeByteVector(GarbageBytes, sizeof(GarbageBytes));

					auto Result = co_await fg_CallSafe
						(
							[InGarbageData = GarbageData]() -> TCFuture<void>
							{
								auto fStreamGarbage = [InData = InGarbageData]() -> TCAsyncGenerator<CIOByteVector>
									{
										co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());
										co_yield CIOByteVector(InData.f_GetArray(), InData.f_GetLen());
										co_return {};
									}
								;

								auto Generator = fg_DecompressZstandardAsync(fg_CallSafe(fg_Move(fStreamGarbage)));
								for (auto iData = co_await fg_Move(Generator).f_GetPipelinedIterator(); iData; co_await ++iData)
								{
								}
								co_return {};
							}
						)
						.f_Wrap()
					;

					DMibExpectException(Result.f_Access(), DMibErrorInstance("Error decompressing data: Unknown frame descriptor"));

					co_return {};
				};

				DMibTestSuite("TruncatedFrame") -> TCFuture<void>
				{
					uint8 Data[256];
					for (aint iByte = 0; iByte < 256; ++iByte)
						Data[iByte] = (uint8)iByte;
					CByteVector Source = f_MakeByteVector(Data, sizeof(Data));
					CByteVector Compressed = fg_CompressZstandard(Source);

					// Truncate the compressed data
					Compressed.f_SetLen(Compressed.f_GetLen() / 2);

					auto Result = co_await fg_CallSafe
						(
							[InCompressed = Compressed]() -> TCFuture<void>
							{
								auto fStreamTruncated = [InData = InCompressed]() -> TCAsyncGenerator<CIOByteVector>
									{
										co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());
										co_yield CIOByteVector(InData.f_GetArray(), InData.f_GetLen());
										co_return {};
									}
								;

								auto Generator = fg_DecompressZstandardAsync(fg_CallSafe(fg_Move(fStreamTruncated)));
								for (auto iData = co_await fg_Move(Generator).f_GetPipelinedIterator(); iData; co_await ++iData)
								{
								}
								co_return {};
							}
						)
						.f_Wrap()
					;

					DMibExpectException(Result.f_Access(), DMibErrorInstance("Error decompressing data (end): No progress was made"));

					co_return {};
				};
			};
		};
	}

	DMibTestRegister(CZstandard_Tests, Malterlib::Compression);
}
