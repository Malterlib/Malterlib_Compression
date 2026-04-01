// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Compression/MSCompress>
#include <Mib/Compression/MSCompressAsync>
#include <Mib/Test/Exception>
#include <Mib/File/File>

#ifdef DPlatformFamily_Windows
	#include <Mib/Process/ProcessLaunchActor>
#endif

namespace
{
	using namespace NMib;
	using namespace NMib::NStr;
	using namespace NMib::NCompression;
	using namespace NMib::NContainer;
	using namespace NMib::NConcurrency;
	using namespace NMib::NStream;

	class CMSCompress_Tests : public NMib::NTest::CTest
	{
	public:
		CByteVector f_MakeByteVector(uint8 const *_pData, aint _nLen)
		{
			CByteVector Vec;
			Vec.f_SetLen(_nLen);
			NMib::NMemory::fg_MemCopy(Vec.f_GetArray(), _pData, _nLen);
			return Vec;
		}

		void f_TestSyncRoundTrip(uint8 const *_pData, aint _nLen)
		{
			CByteVector Source = f_MakeByteVector(_pData, _nLen);
			CByteVector Compressed = fg_CompressMSCompress(Source);
			CByteVector Decompressed = fg_DecompressMSCompress(Compressed);

			DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
			DMibExpect(Decompressed, ==, Source)(ETestFlag_NoValues);
		}

		TCFuture<void> f_TestAsyncRoundTrip(CByteVector _TestBuffer)
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

			CMSCompressAsyncOptions Options;
			Options.m_KnownSize = uint32(_TestBuffer.f_GetLen());

			auto DecompressedGenerator = fg_DecompressMSCompressAsync(fg_CompressMSCompressAsync(fg_CallSafe(fg_Move(fStreamData)), Options));

			CIOByteVector DecompressedData;
			for (auto iData = co_await fg_Move(DecompressedGenerator).f_GetPipelinedIterator(); iData; co_await ++iData)
				DecompressedData.f_Insert(fg_Move(*iData));

			DMibExpect(DecompressedData.f_GetLen(), ==, _TestBuffer.f_GetLen());
			DMibExpect(DecompressedData, ==, _TestBuffer)(ETestFlag_NoValues);

			co_return {};
		}

		void f_ValidateSZDDHeader(CByteVector const &_Compressed, uint32 _ExpectedOriginalSize)
		{
			DMibExpect(_Compressed.f_GetLen(), >=, (umint)14);

			uint8 const *pData = _Compressed.f_GetArray();

			// Magic signature "SZDD"
			DMibExpect(pData[0], ==, (uint8)0x53);
			DMibExpect(pData[1], ==, (uint8)0x5A);
			DMibExpect(pData[2], ==, (uint8)0x44);
			DMibExpect(pData[3], ==, (uint8)0x44);

			// Magic constant
			DMibExpect(pData[4], ==, (uint8)0x88);
			DMibExpect(pData[5], ==, (uint8)0xF0);
			DMibExpect(pData[6], ==, (uint8)0x27);
			DMibExpect(pData[7], ==, (uint8)0x33);

			// Compression type 'A'
			DMibExpect(pData[8], ==, (uint8)0x41);

			// Original size (little-endian)
			uint32 OrigSize;
			NMemory::fg_MemCopy(&OrigSize, pData + 10, 4);
			OrigSize = fg_ByteSwapLE(OrigSize);
			DMibExpect(OrigSize, ==, _ExpectedOriginalSize);
		}

#ifdef DPlatformFamily_Windows
		TCFuture<void> f_TestWindowsExpandCompatibility(CByteVector _Source, CStr _TestName)
		{
			using namespace NStr;

			// Compress with our implementation, decompress with Windows expand.exe
			CByteVector Compressed = fg_CompressMSCompress(_Source);

			CStr TempDir = NSys::NFile::fg_GetTemporaryDirectory();

			// Ensure temp directory exists
			if (!NFile::CFile::fs_FileExists(TempDir, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_CreateDirectory(TempDir);

			// Use test name in file paths to avoid conflicts between parallel tests
			CStr CompressedFile = "{}/mscompress_{}.dat_"_f << TempDir << _TestName;
			CStr ExpandedFile = "{}/mscompress_{}.dat"_f << TempDir << _TestName;

			auto CleanupFiles = g_OnScopeExit / [&]
				{
					if (NFile::CFile::fs_FileExists(CompressedFile))
						NFile::CFile::fs_DeleteFile(CompressedFile);
					if (NFile::CFile::fs_FileExists(ExpandedFile))
						NFile::CFile::fs_DeleteFile(ExpandedFile);
				}
			;

			NFile::CFile::fs_WriteFile(CompressedFile, Compressed);

			// Use full path to Windows expand.exe to avoid Git's expand.exe
			auto ExpandResult = co_await NProcess::CProcessLaunchActor::fs_LaunchSimple
				(
					NProcess::CProcessLaunchActor::CSimpleLaunch
					(
						"C:/Windows/System32/expand.exe"
						, {CompressedFile, ExpandedFile}
						, NStr::CStr()
						, NProcess::CProcessLaunchActor::ESimpleLaunchFlag_None
					)
				)
			;

			DMibExpect(ExpandResult.m_ExitCode, ==, (uint32)0);

			CByteVector ExpandedData = NFile::CFile::fs_ReadFile(ExpandedFile);

			DMibExpect(ExpandedData.f_GetLen(), ==, _Source.f_GetLen());
			DMibExpect(ExpandedData, ==, _Source)(ETestFlag_NoValues);

			co_return {};
		}
#endif

		void f_DoTests()
		{
			DMibTestSuite("Synchronous")
			{
				DMibTestCategory("Empty")
				{
					CByteVector Source;
					CByteVector Compressed = fg_CompressMSCompress(Source);
					f_ValidateSZDDHeader(Compressed, 0);
					CByteVector Decompressed = fg_DecompressMSCompress(Compressed);
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
					for (aint i = 0; i < 1024; ++i)
						Data[i] = 0xAA;
					f_TestSyncRoundTrip(Data, 1024);
				};

				DMibTestCategory("TwoSymbols")
				{
					uint8 Data[512];
					for (aint i = 0; i < 512; ++i)
						Data[i] = (i & 1) ? 0xFF : 0x00;
					f_TestSyncRoundTrip(Data, 512);
				};

				DMibTestCategory("AllByteValues")
				{
					uint8 Data[256];
					for (aint i = 0; i < 256; ++i)
						Data[i] = (uint8)i;
					f_TestSyncRoundTrip(Data, 256);
				};

				DMibTestCategory("SkewedDistribution")
				{
					uint8 Data[1024];
					for (aint i = 0; i < 1024; ++i)
						Data[i] = (i < 900) ? 'A' : (uint8)(i & 0x1F);
					f_TestSyncRoundTrip(Data, 1024);
				};

				DMibTestCategory("SmallSizes")
				{
					for (aint nSize = 1; nSize <= 18; ++nSize)
					{
						DMibTestCategory("{} bytes"_f << nSize)
						{
							uint8 Data[18];
							for (aint i = 0; i < nSize; ++i)
								Data[i] = (uint8)(i * 17);
							f_TestSyncRoundTrip(Data, nSize);
						};
					}
				};

				DMibTestCategory("RepeatedPattern")
				{
					uint8 Data[2048];
					for (aint i = 0; i < 2048; ++i)
						Data[i] = (uint8)(i % 5);
					f_TestSyncRoundTrip(Data, 2048);
				};

				DMibTestCategory("FullyRandom")
				{
					NMib::NMisc::CRandomShiftRNG Random;
					uint8 Data[2048];
					for (aint i = 0; i < 2048; ++i)
						Data[i] = Random.f_GetValue<uint8>();
					f_TestSyncRoundTrip(Data, 2048);
				};

				DMibTestCategory("LargeInput")
				{
					constexpr aint c_nSize = 65536;
					CByteVector Source;
					Source.f_SetLen(c_nSize);
					NMib::NMisc::CRandomShiftRNG Random;
					for (aint i = 0; i < c_nSize; ++i)
						Source[i] = Random.f_GetValue<uint8>();

					CByteVector Compressed = fg_CompressMSCompress(Source);
					CByteVector Decompressed = fg_DecompressMSCompress(Compressed);

					DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
					DMibExpect(Decompressed, ==, Source)(ETestFlag_NoValues);
				};

				DMibTestCategory("SpaceFilledData")
				{
					// Data filled with spaces (0x20) - this is the window pre-fill value,
					// so the compressor should find excellent matches
					uint8 Data[4096];
					for (aint i = 0; i < 4096; ++i)
						Data[i] = 0x20;
					CByteVector Source = f_MakeByteVector(Data, 4096);
					CByteVector Compressed = fg_CompressMSCompress(Source);
					CByteVector Decompressed = fg_DecompressMSCompress(Compressed);

					DMibExpect(Compressed.f_GetLen(), <, Source.f_GetLen());
					DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
					DMibExpect(Decompressed, ==, Source)(ETestFlag_NoValues);
				};

				DMibTestCategory("CompressionEfficiency")
				{
					uint8 Data[4096];
					for (aint i = 0; i < 4096; ++i)
						Data[i] = 'X';

					CByteVector Source = f_MakeByteVector(Data, 4096);
					CByteVector Compressed = fg_CompressMSCompress(Source);

					// Highly repetitive data should compress
					DMibExpect(Compressed.f_GetLen(), <, Source.f_GetLen());

					CByteVector Decompressed = fg_DecompressMSCompress(Compressed);
					DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
					DMibExpect(Decompressed, ==, Source)(ETestFlag_NoValues);
				};

				DMibTestCategory("HeaderFormat")
				{
					uint8 Data[] = {1, 2, 3, 4, 5};
					CByteVector Source = f_MakeByteVector(Data, 5);
					CByteVector Compressed = fg_CompressMSCompress(Source);
					f_ValidateSZDDHeader(Compressed, 5);
				};

				DMibTestCategory("FilenameHint")
				{
					uint8 Data[] = {1, 2, 3};
					CByteVector Source = f_MakeByteVector(Data, 3);

					CMSCompressOptions Options;
					Options.m_FilenameHint = fg_MSCompressFilenameHint("SETUP.EXE");
					CByteVector Compressed = fg_CompressMSCompress(Source, Options);

					DMibExpect(Compressed[9], ==, (uint8)'E');
					DMibExpect(fg_MSCompressReadFilenameHint(Compressed), ==, (uint8)'E');

					// No hint
					CByteVector CompressedNoHint = fg_CompressMSCompress(Source);
					DMibExpect(fg_MSCompressReadFilenameHint(CompressedNoHint), ==, (uint8)0x00);

					// Empty filename
					DMibExpect(fg_MSCompressFilenameHint(""), ==, (uint8)0x00);
				};

				DMibTestCategory("SemiRandom")
				{
					NMib::NMisc::CRandomShiftRNG Random;
					constexpr aint c_nSize = 8192;
					CByteVector Source;
					Source.f_SetLen(c_nSize);
					for (aint i = 0; i < c_nSize; ++i)
					{
						if ((i & 7) == 0)
							Source[i] = Random.f_GetValue<uint8>();
						else
							Source[i] = 0;
					}

					CByteVector Compressed = fg_CompressMSCompress(Source);
					CByteVector Decompressed = fg_DecompressMSCompress(Compressed);

					DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
					DMibExpect(Decompressed, ==, Source)(ETestFlag_NoValues);
				};

				DMibTestCategory("LargeSemiRandom")
				{
					NMib::NMisc::CRandomShiftRNG Random;
					CByteVector Source;
					Source.f_SetLen(NFile::gc_IdealIoSize * 2);
					for (umint i = 0; i < Source.f_GetLen(); ++i)
					{
						if ((i & 7) == 0)
							Source[i] = Random.f_GetValue<uint8>();
						else
							Source[i] = 0;
					}

					CByteVector Compressed = fg_CompressMSCompress(Source);
					CByteVector Decompressed = fg_DecompressMSCompress(Compressed);

					DMibExpect(Decompressed.f_GetLen(), ==, Source.f_GetLen());
					DMibExpect(Decompressed, ==, Source)(ETestFlag_NoValues);
				};

				DMibTestCategory("MalformedInput")
				{
					DMibTestCategory("GarbageData")
					{
						uint8 Data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E};
						DMibExpectException
							(
								fg_DecompressMSCompress(f_MakeByteVector(Data, sizeof(Data)))
								, DMibErrorInstance("Not a valid MS-compressed file: invalid SZDD signature")
							)
						;
					};

					DMibTestCategory("TruncatedHeader")
					{
						// Valid magic but truncated
						uint8 Data[] = {0x53, 0x5A, 0x44, 0x44, 0x88, 0xF0};
						DMibExpectException
							(
								fg_DecompressMSCompress(f_MakeByteVector(Data, sizeof(Data)))
								, DMibErrorInstance("Not a valid MS-compressed file: file too small")
							)
						;
					};

					DMibTestCategory("BadMagicConstant")
					{
						uint8 Data[14] = {0x53, 0x5A, 0x44, 0x44, 0x00, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00};
						DMibExpectException
							(
								fg_DecompressMSCompress(f_MakeByteVector(Data, sizeof(Data)))
								, DMibErrorInstance("Not a valid MS-compressed file: invalid magic constant")
							)
						;
					};

					DMibTestCategory("KWAJFormat")
					{
						uint8 Data[14] = {0x4B, 0x57, 0x41, 0x4A, 0x88, 0xF0, 0x27, 0xD1, 0x03, 0x00, 0x12, 0x00, 0x01, 0x00};
						DMibExpectException
							(
								fg_DecompressMSCompress(f_MakeByteVector(Data, sizeof(Data)))
								, DMibErrorInstance("MS-compressed file uses KWAJ format (version 6.22) which is not supported")
							)
						;
					};

					DMibTestCategory("UnsupportedCompressionType")
					{
						// Valid signature and magic but compression type 0x42 instead of 0x41
						uint8 Data[14] = {0x53, 0x5A, 0x44, 0x44, 0x88, 0xF0, 0x27, 0x33, 0x42, 0x00, 0x0A, 0x00, 0x00, 0x00};
						DMibExpectException
							(
								fg_DecompressMSCompress(f_MakeByteVector(Data, sizeof(Data)))
								, DMibErrorInstance("Not a valid MS-compressed file: unsupported compression type 0x42")
							)
						;
					};

					DMibTestCategory("TruncatedPayloadLiteral")
					{
						// Header claiming 10 bytes + control byte 0xFF (8 literals) but only 1 literal byte follows
						// Control byte 0xFF = all 8 bits set = 8 literals expected
						uint8 Data[] =
							{
								0x53, 0x5A, 0x44, 0x44, 0x88, 0xF0, 0x27, 0x33, 0x41, 0x00, 0x0A, 0x00, 0x00, 0x00  // header, size=10
								, 0xFF  // control: 8 literals
								, 0x41  // only 1 literal, truncated
							}
						;

						DMibExpectException
							(
								fg_DecompressMSCompress(f_MakeByteVector(Data, sizeof(Data)))
								, DMibErrorInstance("Truncated MS-compressed data: unexpected end of stream in literal")
							)
						;
					};

					DMibTestCategory("TruncatedPayloadBackRef")
					{
						// Header claiming 10 bytes + control byte 0x00 (8 back-refs) but only 1 data byte follows
						// Control byte 0x00 = all bits clear = 8 back-references, each needing 2 bytes
						uint8 Data[] =
							{
								0x53, 0x5A, 0x44, 0x44, 0x88, 0xF0, 0x27, 0x33, 0x41, 0x00, 0x0A, 0x00, 0x00, 0x00  // header, size=10
								, 0x00  // control: 8 back-references
								, 0x00  // only 1 byte, need 2 for a back-ref
							}
						;

						DMibExpectException
							(
								fg_DecompressMSCompress(f_MakeByteVector(Data, sizeof(Data)))
								, DMibErrorInstance("Truncated MS-compressed data: unexpected end of stream in back-reference")
							)
						;
					};

					DMibTestCategory("TruncatedPayloadSizeShortfall")
					{
						// Valid header claiming 100 bytes but no payload at all
						uint8 Header[14] = {0x53, 0x5A, 0x44, 0x44, 0x88, 0xF0, 0x27, 0x33, 0x41, 0x00, 0x64, 0x00, 0x00, 0x00};

						DMibExpectException
							(
								fg_DecompressMSCompress(f_MakeByteVector(Header, sizeof(Header)))
								, DMibErrorInstance("Truncated MS-compressed data: decompressed 0 bytes but expected 100")
							)
						;
					};

					DMibTestCategory("ExceedsMaxDecompressedSize")
					{
						// Header claiming 1 GB but limit set to 1 MB — should reject before allocating
						uint8 Header[14] = {0x53, 0x5A, 0x44, 0x44, 0x88, 0xF0, 0x27, 0x33, 0x41, 0x00, 0x00, 0x00, 0x40, 0x00}; // size = 0x00400000 = 4 MB
						DMibExpectException
							(
								fg_DecompressMSCompress(f_MakeByteVector(Header, sizeof(Header)), 1024 * 1024)
								, DMibErrorInstance("MSCompress decompressed size (4194304) exceeds maximum allowed (1048576)")
							)
						;
					};
				};
			};

			DMibTestCategory("Streaming")
			{
				DMibTestSuite("Empty") -> TCFuture<void>
				{
					CByteVector EmptyBuffer;
					co_await f_TestAsyncRoundTrip(EmptyBuffer);
					co_return {};
				};

				DMibTestSuite("SingleByte") -> TCFuture<void>
				{
					uint8 Data[] = {42};
					co_await f_TestAsyncRoundTrip(f_MakeByteVector(Data, 1));
					co_return {};
				};

				DMibTestSuite("SmallData") -> TCFuture<void>
				{
					uint8 Data[64];
					for (aint i = 0; i < 64; ++i)
						Data[i] = (uint8)(i * 7);
					co_await f_TestAsyncRoundTrip(f_MakeByteVector(Data, 64));
					co_return {};
				};

				DMibTestSuite("LargerData") -> TCFuture<void>
				{
					NMib::NMisc::CRandomShiftRNG Random;
					constexpr aint c_nSize = 32768;
					CByteVector Buffer;
					Buffer.f_SetLen(c_nSize);
					for (aint i = 0; i < c_nSize; ++i)
					{
						if ((i & 3) == 0)
							Buffer[i] = Random.f_GetValue<uint8>();
						else
							Buffer[i] = 0;
					}
					co_await f_TestAsyncRoundTrip(Buffer);
					co_return {};
				};

				DMibTestSuite("HighlyCompressible") -> TCFuture<void>
				{
					CByteVector Buffer;
					Buffer.f_SetLen(4096);
					for (umint i = 0; i < Buffer.f_GetLen(); ++i)
						Buffer[i] = 0x20;
					co_await f_TestAsyncRoundTrip(Buffer);
					co_return {};
				};

				DMibTestSuite("MultipleChunks") -> TCFuture<void>
				{
					CByteVector Buffer;
					Buffer.f_SetLen(65536);
					NMib::NMisc::CRandomShiftRNG Random;
					for (umint i = 0; i < Buffer.f_GetLen(); ++i)
					{
						if ((i & 7) == 0)
							Buffer[i] = Random.f_GetValue<uint8>();
						else
							Buffer[i] = 0;
					}
					co_await f_TestAsyncRoundTrip(Buffer);
					co_return {};
				};

				DMibTestSuite("AsyncCompressSyncDecompress") -> TCFuture<void>
				{
					// Isolate: async compress, then sync decompress
					NMib::NMisc::CRandomShiftRNG Random;
					CByteVector Buffer;
					Buffer.f_SetLen(NFile::gc_IdealIoSize * 2);
					for (umint i = 0; i < Buffer.f_GetLen(); ++i)
					{
						if ((i & 7) == 0)
							Buffer[i] = Random.f_GetValue<uint8>();
						else
							Buffer[i] = 0;
					}

					auto fStreamData = [InTestBuffer = Buffer]() -> TCAsyncGenerator<CIOByteVector>
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

					CMSCompressAsyncOptions Options;
					Options.m_KnownSize = uint32(Buffer.f_GetLen());

					auto CompressedGenerator = fg_CompressMSCompressAsync(fg_CallSafe(fg_Move(fStreamData)), Options);

					CIOByteVector CompressedData;
					for (auto iData = co_await fg_Move(CompressedGenerator).f_GetPipelinedIterator(); iData; co_await ++iData)
						CompressedData.f_Insert(fg_Move(*iData));

					CByteVector Decompressed = fg_DecompressMSCompress(CompressedData);

					DMibExpect(Decompressed.f_GetLen(), ==, Buffer.f_GetLen());
					DMibExpect(Decompressed, ==, Buffer)(ETestFlag_NoValues);

					co_return {};
				};

				DMibTestSuite("SyncCompressAsyncDecompress") -> TCFuture<void>
				{
					// Isolate: sync compress, then async decompress
					NMib::NMisc::CRandomShiftRNG Random;
					CByteVector Buffer;
					Buffer.f_SetLen(NFile::gc_IdealIoSize * 2);
					for (umint i = 0; i < Buffer.f_GetLen(); ++i)
					{
						if ((i & 7) == 0)
							Buffer[i] = Random.f_GetValue<uint8>();
						else
							Buffer[i] = 0;
					}

					CByteVector Compressed = fg_CompressMSCompress(Buffer);

					auto fStreamCompressed = [InCompressed = Compressed]() -> TCAsyncGenerator<CIOByteVector>
						{
							co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());
							umint Len = InCompressed.f_GetLen();
							auto *pBuffer = InCompressed.f_GetArray();
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

					auto DecompressedGenerator = fg_DecompressMSCompressAsync(fg_CallSafe(fg_Move(fStreamCompressed)));

					CIOByteVector DecompressedData;
					for (auto iData = co_await fg_Move(DecompressedGenerator).f_GetPipelinedIterator(); iData; co_await ++iData)
						DecompressedData.f_Insert(fg_Move(*iData));

					DMibExpect(DecompressedData.f_GetLen(), ==, Buffer.f_GetLen());
					DMibExpect(DecompressedData, ==, Buffer)(ETestFlag_NoValues);

					co_return {};
				};

				DMibTestSuite("LargerThanIoSize") -> TCFuture<void>
				{
					// Data larger than gc_IdealIoSize to exercise multi-chunk streaming through output buffer flushes
					NMib::NMisc::CRandomShiftRNG Random;
					CByteVector Buffer;
					Buffer.f_SetLen(NFile::gc_IdealIoSize * 2);
					for (umint i = 0; i < Buffer.f_GetLen(); ++i)
					{
						if ((i & 7) == 0)
							Buffer[i] = Random.f_GetValue<uint8>();
						else
							Buffer[i] = 0;
					}
					co_await f_TestAsyncRoundTrip(Buffer);
					co_return {};
				};

				DMibTestSuite("MultipleIoSizes") -> TCFuture<void>
				{
					// Data spanning several full IO-sized output chunks
					NMib::NMisc::CRandomShiftRNG Random;
					CByteVector Buffer;
					Buffer.f_SetLen(NFile::gc_IdealIoSize * 3 + 500);
					for (umint i = 0; i < Buffer.f_GetLen(); ++i)
					{
						if ((i & 3) == 0)
							Buffer[i] = Random.f_GetValue<uint8>();
						else
							Buffer[i] = 0;
					}
					co_await f_TestAsyncRoundTrip(Buffer);
					co_return {};
				};

				DMibTestSuite("LargeHighlyCompressible") -> TCFuture<void>
				{
					// Highly compressible data larger than IO size - compressed output should still be small
					CByteVector Buffer;
					Buffer.f_SetLen(NFile::gc_IdealIoSize * 2);
					for (umint i = 0; i < Buffer.f_GetLen(); ++i)
						Buffer[i] = 'A';
					co_await f_TestAsyncRoundTrip(Buffer);
					co_return {};
				};

				DMibTestCategory("MalformedInput")
				{
					DMibTestSuite("GarbageData") -> TCFuture<void>
					{
						uint8 GarbageBytes[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E};
						CByteVector GarbageData = f_MakeByteVector(GarbageBytes, sizeof(GarbageBytes));

						auto Result = co_await fg_CallSafe
							(
								[InGarbageData = GarbageData]() -> TCFuture<void>
								{
									auto fStreamData = [InData = InGarbageData]() -> TCAsyncGenerator<CIOByteVector>
										{
											co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());
											co_yield CIOByteVector(InData.f_GetArray(), InData.f_GetLen());
											co_return {};
										}
									;

									auto Generator = fg_DecompressMSCompressAsync(fg_CallSafe(fg_Move(fStreamData)));
									for (auto iData = co_await fg_Move(Generator).f_GetPipelinedIterator(); iData; co_await ++iData)
									{
									}
									co_return {};
								}
							)
							.f_Wrap()
						;

						DMibExpectException
							(
								Result.f_Access()
								, DMibErrorInstance("Not a valid MS-compressed file: invalid SZDD signature")
							)
						;

						co_return {};
					};

					DMibTestSuite("TruncatedHeader") -> TCFuture<void>
					{
						uint8 HeaderBytes[] = {0x53, 0x5A, 0x44, 0x44, 0x88, 0xF0};
						CByteVector TruncatedData = f_MakeByteVector(HeaderBytes, sizeof(HeaderBytes));

						auto Result = co_await fg_CallSafe
							(
								[InData = TruncatedData]() -> TCFuture<void>
								{
									auto fStreamData = [InData]() -> TCAsyncGenerator<CIOByteVector>
										{
											co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());
											co_yield CIOByteVector(InData.f_GetArray(), InData.f_GetLen());
											co_return {};
										}
									;

									auto Generator = fg_DecompressMSCompressAsync(fg_CallSafe(fg_Move(fStreamData)));
									for (auto iData = co_await fg_Move(Generator).f_GetPipelinedIterator(); iData; co_await ++iData)
									{
									}
									co_return {};
								}
							)
							.f_Wrap()
						;

						DMibExpectException
							(
								Result.f_Access()
								, DMibErrorInstance("Not a valid MS-compressed file: file too small")
							)
						;

						co_return {};
					};

					DMibTestSuite("BadMagicConstant") -> TCFuture<void>
					{
						uint8 HeaderBytes[14] = {0x53, 0x5A, 0x44, 0x44, 0x00, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00};
						CByteVector BadData = f_MakeByteVector(HeaderBytes, sizeof(HeaderBytes));

						auto Result = co_await fg_CallSafe
							(
								[InData = BadData]() -> TCFuture<void>
								{
									auto fStreamData = [InData]() -> TCAsyncGenerator<CIOByteVector>
										{
											co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());
											co_yield CIOByteVector(InData.f_GetArray(), InData.f_GetLen());
											co_return {};
										}
									;

									auto Generator = fg_DecompressMSCompressAsync(fg_CallSafe(fg_Move(fStreamData)));
									for (auto iData = co_await fg_Move(Generator).f_GetPipelinedIterator(); iData; co_await ++iData)
									{
									}
									co_return {};
								}
							)
							.f_Wrap()
						;

						DMibExpectException
							(
								Result.f_Access()
								, DMibErrorInstance("Not a valid MS-compressed file: invalid magic constant")
							)
						;

						co_return {};
					};

					DMibTestSuite("KWAJFormat") -> TCFuture<void>
					{
						uint8 HeaderBytes[14] = {0x4B, 0x57, 0x41, 0x4A, 0x88, 0xF0, 0x27, 0xD1, 0x03, 0x00, 0x12, 0x00, 0x01, 0x00};
						CByteVector KWAJData = f_MakeByteVector(HeaderBytes, sizeof(HeaderBytes));

						auto Result = co_await fg_CallSafe
							(
								[InData = KWAJData]() -> TCFuture<void>
								{
									auto fStreamData = [InData]() -> TCAsyncGenerator<CIOByteVector>
										{
											co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());
											co_yield CIOByteVector(InData.f_GetArray(), InData.f_GetLen());
											co_return {};
										}
									;

									auto Generator = fg_DecompressMSCompressAsync(fg_CallSafe(fg_Move(fStreamData)));
									for (auto iData = co_await fg_Move(Generator).f_GetPipelinedIterator(); iData; co_await ++iData)
									{
									}
									co_return {};
								}
							)
							.f_Wrap()
						;

						DMibExpectException
							(
								Result.f_Access()
								, DMibErrorInstance("MS-compressed file uses KWAJ format (version 6.22) which is not supported")
							)
						;

						co_return {};
					};

					DMibTestSuite("MissingKnownSize") -> TCFuture<void>
					{
						auto Result = co_await fg_CallSafe
							(
								[]() -> TCFuture<void>
								{
									auto fStreamData = []() -> TCAsyncGenerator<CIOByteVector>
										{
											co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());
											uint8 Byte = 42;
											co_yield CIOByteVector(&Byte, 1);
											co_return {};
										}
									;

									CMSCompressAsyncOptions Options;
									// m_KnownSize left at default (unset)

									auto Generator = fg_CompressMSCompressAsync(fg_CallSafe(fg_Move(fStreamData)), Options);
									for (auto iData = co_await fg_Move(Generator).f_GetPipelinedIterator(); iData; co_await ++iData)
									{
									}
									co_return {};
								}
							)
							.f_Wrap()
						;

						DMibExpectException
							(
								Result.f_Access()
								, DMibErrorInstance("MSCompress async compression requires m_KnownSize to be set")
							)
						;

						co_return {};
					};

					DMibTestSuite("UnsupportedCompressionType") -> TCFuture<void>
					{
						// Valid signature and magic but compression type 0x42 instead of 0x41
						uint8 HeaderBytes[14] = {0x53, 0x5A, 0x44, 0x44, 0x88, 0xF0, 0x27, 0x33, 0x42, 0x00, 0x0A, 0x00, 0x00, 0x00};
						CByteVector BadType = f_MakeByteVector(HeaderBytes, sizeof(HeaderBytes));

						auto Result = co_await fg_CallSafe
							(
								[InData = BadType]() -> TCFuture<void>
								{
									auto fStreamData = [InData]() -> TCAsyncGenerator<CIOByteVector>
										{
											co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());
											co_yield CIOByteVector(InData.f_GetArray(), InData.f_GetLen());
											co_return {};
										}
									;

									auto Generator = fg_DecompressMSCompressAsync(fg_CallSafe(fg_Move(fStreamData)));
									for (auto iData = co_await fg_Move(Generator).f_GetPipelinedIterator(); iData; co_await ++iData)
									{
									}
									co_return {};
								}
							)
							.f_Wrap()
						;

						DMibExpectException
							(
								Result.f_Access()
								, DMibErrorInstance("Not a valid MS-compressed file: unsupported compression type 0x42")
							)
						;

						co_return {};
					};

					DMibTestSuite("SizeMismatch") -> TCFuture<void>
					{
						// Stream 10 bytes but declare m_KnownSize = 5
						auto Result = co_await fg_CallSafe
							(
								[]() -> TCFuture<void>
								{
									auto fStreamData = []() -> TCAsyncGenerator<CIOByteVector>
										{
											co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());
											uint8 Data[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
											co_yield CIOByteVector(Data, 10);
											co_return {};
										}
									;

									CMSCompressAsyncOptions Options;
									Options.m_KnownSize = 5;

									auto Generator = fg_CompressMSCompressAsync(fg_CallSafe(fg_Move(fStreamData)), Options);
									for (auto iData = co_await fg_Move(Generator).f_GetPipelinedIterator(); iData; co_await ++iData)
									{
									}
									co_return {};
								}
							)
							.f_Wrap()
						;

						DMibExpectException
							(
								Result.f_Access()
								, DMibErrorInstance("MSCompress: actual input size (10) does not match m_KnownSize (5)")
							)
						;

						co_return {};
					};

					DMibTestSuite("TruncatedPayloadLiteral") -> TCFuture<void>
					{
						// Control byte 0xFF (8 literals) but only 1 literal byte — carry handler
						// processes 1 literal then exits loop (no more data), hitting the size check
						uint8 Data[] =
							{
								0x53, 0x5A, 0x44, 0x44, 0x88, 0xF0, 0x27, 0x33, 0x41, 0x00, 0x0A, 0x00, 0x00, 0x00
								, 0xFF, 0x41
							}
						;
						CByteVector TruncData = f_MakeByteVector(Data, sizeof(Data));

						auto Result = co_await fg_CallSafe
							(
								[InData = TruncData]() -> TCFuture<void>
								{
									auto fStreamData = [InData]() -> TCAsyncGenerator<CIOByteVector>
										{
											co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());
											co_yield CIOByteVector(InData.f_GetArray(), InData.f_GetLen());
											co_return {};
										}
									;

									auto Generator = fg_DecompressMSCompressAsync(fg_CallSafe(fg_Move(fStreamData)));
									for (auto iData = co_await fg_Move(Generator).f_GetPipelinedIterator(); iData; co_await ++iData)
									{
									}
									co_return {};
								}
							)
							.f_Wrap()
						;

						DMibExpectException
							(
								Result.f_Access()
								, DMibErrorInstance("Truncated MS-compressed data: decompressed 1 bytes but expected 10")
							)
						;

						co_return {};
					};

					DMibTestSuite("TruncatedPayloadBackRef") -> TCFuture<void>
					{
						// Control byte 0x00 (8 back-refs) but only 1 data byte — truncated in carry handler
						uint8 Data[] =
							{
								0x53, 0x5A, 0x44, 0x44, 0x88, 0xF0, 0x27, 0x33, 0x41, 0x00, 0x0A, 0x00, 0x00, 0x00
								, 0x00, 0x00
							}
						;
						CByteVector TruncData = f_MakeByteVector(Data, sizeof(Data));

						auto Result = co_await fg_CallSafe
							(
								[InData = TruncData]() -> TCFuture<void>
								{
									auto fStreamData = [InData]() -> TCAsyncGenerator<CIOByteVector>
										{
											co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());
											co_yield CIOByteVector(InData.f_GetArray(), InData.f_GetLen());
											co_return {};
										}
									;

									auto Generator = fg_DecompressMSCompressAsync(fg_CallSafe(fg_Move(fStreamData)));
									for (auto iData = co_await fg_Move(Generator).f_GetPipelinedIterator(); iData; co_await ++iData)
									{
									}
									co_return {};
								}
							)
							.f_Wrap()
						;

						DMibExpectException
							(
								Result.f_Access()
								, DMibErrorInstance("Truncated MS-compressed data: unexpected end of stream in back-reference")
							)
						;

						co_return {};
					};

					DMibTestSuite("TruncatedPayloadSizeShortfall") -> TCFuture<void>
					{
						// Valid header claiming 100 bytes but no payload
						uint8 HeaderBytes[14] = {0x53, 0x5A, 0x44, 0x44, 0x88, 0xF0, 0x27, 0x33, 0x41, 0x00, 0x64, 0x00, 0x00, 0x00};
						CByteVector HeaderOnly = f_MakeByteVector(HeaderBytes, sizeof(HeaderBytes));

						auto Result = co_await fg_CallSafe
							(
								[InData = HeaderOnly]() -> TCFuture<void>
								{
									auto fStreamData = [InData]() -> TCAsyncGenerator<CIOByteVector>
										{
											co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());
											co_yield CIOByteVector(InData.f_GetArray(), InData.f_GetLen());
											co_return {};
										}
									;

									auto Generator = fg_DecompressMSCompressAsync(fg_CallSafe(fg_Move(fStreamData)));
									for (auto iData = co_await fg_Move(Generator).f_GetPipelinedIterator(); iData; co_await ++iData)
									{
									}
									co_return {};
								}
							)
							.f_Wrap()
						;

						DMibExpectException
							(
								Result.f_Access()
								, DMibErrorInstance("Truncated MS-compressed data: decompressed 0 bytes but expected 100")
							)
						;

						co_return {};
					};
				};
			};

#ifdef DPlatformFamily_Windows
			DMibTestCategory("WindowsCompatibility")
			{
				DMibTestSuite("SmallFile") -> TCFuture<void>
				{
					uint8 Data[] = "Hello, World! This is a test of MS-COMPRESS compatibility.";
					CByteVector Source = f_MakeByteVector(Data, sizeof(Data) - 1);
					co_await f_TestWindowsExpandCompatibility(Source, "small");
					co_return {};
				};

				DMibTestSuite("AllByteValues") -> TCFuture<void>
				{
					uint8 Data[256];
					for (aint i = 0; i < 256; ++i)
						Data[i] = (uint8)i;
					co_await f_TestWindowsExpandCompatibility(f_MakeByteVector(Data, 256), "allbytes");
					co_return {};
				};

				DMibTestSuite("RepeatedData") -> TCFuture<void>
				{
					CByteVector Source;
					Source.f_SetLen(4096);
					for (umint i = 0; i < 4096; ++i)
						Source[i] = 'A';
					co_await f_TestWindowsExpandCompatibility(Source, "repeated");
					co_return {};
				};

				DMibTestSuite("SemiRandom") -> TCFuture<void>
				{
					NMib::NMisc::CRandomShiftRNG Random;
					constexpr aint c_nSize = 8192;
					CByteVector Source;
					Source.f_SetLen(c_nSize);
					for (aint i = 0; i < c_nSize; ++i)
					{
						if ((i & 3) == 0)
							Source[i] = Random.f_GetValue<uint8>();
						else
							Source[i] = 0;
					}
					co_await f_TestWindowsExpandCompatibility(Source, "semirandom");
					co_return {};
				};

				DMibTestSuite("LargeFile") -> TCFuture<void>
				{
					NMib::NMisc::CRandomShiftRNG Random;
					constexpr aint c_nSize = 65536;
					CByteVector Source;
					Source.f_SetLen(c_nSize);
					for (aint i = 0; i < c_nSize; ++i)
						Source[i] = Random.f_GetValue<uint8>();
					co_await f_TestWindowsExpandCompatibility(Source, "large");
					co_return {};
				};

				DMibTestSuite("SpaceData") -> TCFuture<void>
				{
					CByteVector Source;
					Source.f_SetLen(2048);
					for (umint i = 0; i < 2048; ++i)
						Source[i] = 0x20;
					co_await f_TestWindowsExpandCompatibility(Source, "spaces");
					co_return {};
				};

				DMibTestSuite("LargeSemiRandom") -> TCFuture<void>
				{
					NMib::NMisc::CRandomShiftRNG Random;
					CByteVector Source;
					Source.f_SetLen(NFile::gc_IdealIoSize * 2);
					for (umint i = 0; i < Source.f_GetLen(); ++i)
					{
						if ((i & 7) == 0)
							Source[i] = Random.f_GetValue<uint8>();
						else
							Source[i] = 0;
					}
					co_await f_TestWindowsExpandCompatibility(Source, "largesemirandom");
					co_return {};
				};
			};
#endif
		}
	};

	DMibTestRegister(CMSCompress_Tests, Malterlib::Compression);
}
