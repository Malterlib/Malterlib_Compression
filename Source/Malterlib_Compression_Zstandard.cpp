// Copyright © 2025 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include "Malterlib_Compression_Zstandard.h"

#include <zstd.h>
#include <zstd_errors.h>

namespace NMib::NCompression
{
	NContainer::CByteVector fg_CompressZstandard(NContainer::CByteVector const &_Source, int _CompressionLevel)
	{
		using namespace NMib::NStr;

		mint NeededSize = ZSTD_compressBound(_Source.f_GetLen());
		NContainer::CByteVector CompressedData;
		CompressedData.f_SetLen(NeededSize);

		mint CompressedSize = ZSTD_compress(CompressedData.f_GetArray(), NeededSize, _Source.f_GetArray(), _Source.f_GetLen(), _CompressionLevel);

		if (ZSTD_isError(CompressedSize))
			DMibError("Failed to compress with zstd: {}"_f << ZSTD_getErrorName(CompressedSize));

		CompressedData.f_SetLen(CompressedSize);

		return CompressedData;
	}

	NContainer::CByteVector fg_DecompressZstandard(NContainer::CByteVector const &_Source)
	{
		using namespace NMib::NStr;

		mint DecompressedSize = fg_Max(_Source.f_GetLen(), 16u) * 20u;

		NContainer::CByteVector DecompressedData;
		DecompressedData.f_SetLen(DecompressedSize);

		mint DecompressResult = ZSTD_decompress(DecompressedData.f_GetArray(), DecompressedSize, _Source.f_GetArray(), _Source.f_GetLen());

		for (; ZSTD_getErrorCode(DecompressResult) == ZSTD_error_dstSize_tooSmall;)
		{
			DecompressedSize *= 2;
			DecompressedData.f_SetLen(DecompressedSize);
			DecompressResult = ZSTD_decompress(DecompressedData.f_GetArray(), DecompressedSize, _Source.f_GetArray(), _Source.f_GetLen());
		}

		if (ZSTD_isError(DecompressResult))
			DMibError("Failed to decompress with zstd: {}"_f << ZSTD_getErrorName(DecompressResult));

		DecompressedData.f_SetLen(DecompressResult);

		return DecompressedData;
	}
}
