// Copyright © 2025 Unbroken AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Compression/Zstandard>
#include <Mib/Compression/ZstandardAsync>

namespace
{
	using namespace NMib;
	using namespace NMib::NCompression;
	using namespace NMib::NContainer;
	using namespace NMib::NConcurrency;
	using namespace NMib::NStream;

	constexpr mint gc_TestSize = NFile::gc_IdealIoSize * 16 + 125;

	constexpr uint32 gc_CompressionLevel = 1;

	class CZstandard_Tests : public NMib::NTest::CTest
	{
	public:
		CByteVector f_GetSemiRandomTestBuffer()
		{
			NMib::NMisc::CRandomShiftRNG Random;

			CByteVector TestBuffer;
			TestBuffer.f_SetLen(gc_TestSize);
			{
				auto pArray = TestBuffer.f_GetArray();
				for(aint i = 0; i < gc_TestSize; ++i)
				{
					if ((i & 7) == 0)
						pArray[i] = Random.f_GetValue<uint8>();
					else
						pArray[i] = 0;
				}
			}

			return TestBuffer;
		}

		void f_DoTests()
		{
			DMibTestSuite("Synchronous")
			{
				auto TestBuffer = f_GetSemiRandomTestBuffer();

				CByteVector CompressedData = fg_CompressZstandard(TestBuffer, gc_CompressionLevel);
				CByteVector DecompressedData = fg_DecompressZstandard(CompressedData);

				DMibExpect(DecompressedData.f_GetLen(), ==, TestBuffer.f_GetLen());
				DMibExpect(DecompressedData, ==, TestBuffer)(ETestFlag_NoValues);
			};
			DMibTestCategory("Streaming")
			{
				for (mint iKnownSize = 0; iKnownSize < 2; ++iKnownSize)
				{
					DMibTestCategory(iKnownSize == 0 ? "Unknown size" : "Known size")
					{
						for (mint iMultiThreaded = 0; iMultiThreaded < 2; ++iMultiThreaded)
						{
							DMibTestSuite(iMultiThreaded == 0 ? "Serial" : "Parallel") -> TCFuture<void>
							{
								auto TestBuffer = f_GetSemiRandomTestBuffer();

								auto fStreamData = [InTestBuffer = TestBuffer]() -> TCAsyncGenerator<CIOByteVector>
									{
										co_await NConcurrency::fg_ContinueRunningOnActor(NConcurrency::fg_ConcurrentActorHighCPU());

										mint Len = InTestBuffer.f_GetLen();
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

								CZStandardCompressionOptions Options{.m_CompressionLevel = gc_CompressionLevel};
								if (iKnownSize == 1)
									Options.m_KnownSize = gc_TestSize;

								if (iMultiThreaded == 0)
									Options.m_CompressionThreads = 1;

								auto DecompressedGenerator = fg_DecompressZstandardAsync(fg_CompressZstandardAsync(fg_CallSafe(fg_Move(fStreamData)), Options));

								CIOByteVector DecompressedData;
								for (auto iData = co_await fg_Move(DecompressedGenerator).f_GetPipelinedIterator(); iData; co_await ++iData)
									DecompressedData.f_Insert(fg_Move(*iData));

								DMibExpect(DecompressedData.f_GetLen(), ==, TestBuffer.f_GetLen());
								DMibExpect(DecompressedData, ==, TestBuffer)(ETestFlag_NoValues);

								co_return {};
							};
						}
					};
				}
			};
		}
	};

	DMibTestRegister(CZstandard_Tests, Malterlib::Compression);
}
