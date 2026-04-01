// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Compression/MiniZ>
#include "Malterlib_Compression_MiniZ.h"

/*
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 1

#ifdef DMibPLittleEndian
	#define MINIZ_LITTLE_ENDIAN 1
#else
	#define MINIZ_LITTLE_ENDIAN 0
#endif

#ifdef DArchitecture_x64
	#define MINIZ_HAS_64BIT_REGISTERS 1
#else
	#define MINIZ_HAS_64BIT_REGISTERS 0
#endif
*/
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAME
//#define MINIZ_NO_MALLOC
//*/
extern "C"
{
#ifdef DCompiler_MSVC
#	pragma warning(disable:4334)
#endif
	#include "../../../External/miniz/miniz.c"
	#include "../../../External/miniz/miniz_tdef.c"
	#include "../../../External/miniz/miniz_tinfl.c"
}

namespace NMib::NCompression
{
	using namespace NMib::NStr;

	namespace
	{
		NStr::CStr fsg_TdeflStatusStr(tdefl_status _Status)
		{
			switch (_Status)
			{
			case TDEFL_STATUS_BAD_PARAM: return "bad parameter";
			case TDEFL_STATUS_PUT_BUF_FAILED: return "output buffer write failed";
			case TDEFL_STATUS_OKAY: return "incomplete (not finished)";
			case TDEFL_STATUS_DONE: return "done";
			}
			return CStr(CStr::CFormat("unknown status ({})") << (int)_Status);
		}

		NStr::CStr fsg_TinflStatusStr(tinfl_status _Status)
		{
			switch (_Status)
			{
			case TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS: return "cannot make progress (corrupted or truncated input)";
			case TINFL_STATUS_BAD_PARAM: return "bad parameter";
			case TINFL_STATUS_ADLER32_MISMATCH: return "adler32 checksum mismatch";
			case TINFL_STATUS_FAILED: return "failed (corrupted data)";
			case TINFL_STATUS_DONE: return "done";
			case TINFL_STATUS_NEEDS_MORE_INPUT: return "truncated input";
			case TINFL_STATUS_HAS_MORE_OUTPUT: return "has more output";
			}
			return CStr(CStr::CFormat("unknown status ({})") << (int)_Status);
		}
	}

	NContainer::CByteVector fg_CompressMiniZ(NContainer::CByteVector const &_Input)
	{
		NContainer::CByteVector Output;

		auto fReceiveData = [](void const *_pBuf, int _nBytes, void *_pUser) -> mz_bool
			{
				auto *pOutput = (NContainer::CByteVector *)_pUser;
				pOutput->f_Insert((uint8 const *)_pBuf, _nBytes);
				return MZ_TRUE;
			}
		;

		tdefl_compressor *pComp = tdefl_compressor_alloc();
		if (!pComp)
			DMibError("MiniZ compression failed: unable to allocate compressor");

		auto CompCleanup = g_OnScopeExit / [&]
			{
				tdefl_compressor_free(pComp);
			}
		;

		tdefl_status InitStatus = tdefl_init(pComp, fReceiveData, &Output, TDEFL_DEFAULT_MAX_PROBES);
		if (InitStatus != TDEFL_STATUS_OKAY)
			DMibError("MiniZ compression initialization failed: {}"_f << fsg_TdeflStatusStr(InitStatus));

		tdefl_status CompressStatus = tdefl_compress_buffer(pComp, _Input.f_GetArray(), (size_t)_Input.f_GetLen(), TDEFL_FINISH);
		if (CompressStatus != TDEFL_STATUS_DONE)
			DMibError("MiniZ compression failed: {} (input size: {} bytes)"_f << fsg_TdeflStatusStr(CompressStatus) << _Input.f_GetLen());

		return fg_Move(Output);
	}

	NContainer::CByteVector fg_DecompressMiniZ(NContainer::CByteVector const &_Input)
	{
		NContainer::CByteVector Output;

		size_t nInputSize = (size_t)_Input.f_GetLen();
		size_t nInputOfs = 0;

		tinfl_decompressor Decomp;
		tinfl_init(&Decomp);

		mz_uint8 *pDict = (mz_uint8 *)MZ_MALLOC(TINFL_LZ_DICT_SIZE);
		if (!pDict)
			DMibError("MiniZ decompression failed: unable to allocate dictionary buffer");

		auto DictCleanup = g_OnScopeExit / [&]
			{
				MZ_FREE(pDict);
			}
		;

		NMemory::fg_MemClear(pDict, TINFL_LZ_DICT_SIZE);
		size_t nDictOfs = 0;

		for (;;)
		{
			size_t nInBufSize = nInputSize - nInputOfs;
			size_t nDstBufSize = TINFL_LZ_DICT_SIZE - nDictOfs;

			tinfl_status Status = tinfl_decompress
				(
					&Decomp
					, (mz_uint8 const *)_Input.f_GetArray() + nInputOfs
					, &nInBufSize
					, pDict
					, pDict + nDictOfs
					, &nDstBufSize
					, 0
				)
			;

			nInputOfs += nInBufSize;

			if (nDstBufSize)
				Output.f_Insert(pDict + nDictOfs, nDstBufSize);

			if (Status == TINFL_STATUS_DONE)
				break;

			if (Status != TINFL_STATUS_HAS_MORE_OUTPUT)
				DMibError("MiniZ decompression failed: {} (input size: {} bytes, consumed: {} bytes, output so far: {} bytes)"_f << fsg_TinflStatusStr(Status) << nInputSize << nInputOfs << Output.f_GetLen());

			nDictOfs = (nDictOfs + nDstBufSize) & (TINFL_LZ_DICT_SIZE - 1);
		}

		return fg_Move(Output);
	}
}
