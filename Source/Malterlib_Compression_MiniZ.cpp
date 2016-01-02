// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

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
	#include "../../SDK/miniz/miniz.c"
}

namespace NMib
{
	namespace NDataProcessing
	{

		NContainer::TCVector<uint8> CCompress_MiniZ::f_Compress(NContainer::TCVector<uint8> const& _Input)
		{
			NContainer::TCVector<uint8> Output;

			auto fl_ReceiveData = 
				[](const void* _pBuf, int _nBytes, void *_pUser) -> mz_bool
				{
					NContainer::TCVector<uint8>* pOutput = (NContainer::TCVector<uint8>*)_pUser;

					pOutput->f_Insert( (uint8 const*)_pBuf, _nBytes );

					return MZ_TRUE;
				};

			auto bOK = tdefl_compress_mem_to_output
							(
									_Input.f_GetArray()
								,	(size_t)_Input.f_GetLen()
								,	fl_ReceiveData
								,	&Output
								,	128					// Fixed num probes
							);

			if (!bOK)
			{
				DMibError("Compression failed");
			}

			return fg_Move(Output);
		}

		NContainer::TCVector<uint8> CCompress_MiniZ::f_Decompress(NContainer::TCVector<uint8> const& _Input)
		{
			NContainer::TCVector<uint8> Output;
			
			auto fl_ReceiveData = 
				[](const void* _pBuf, int _nBytes, void *_pUser) -> int
				{
					NContainer::TCVector<uint8>* pOutput = (NContainer::TCVector<uint8>*)_pUser;

					pOutput->f_Insert( (uint8 const*)_pBuf, _nBytes );

					return 1;
				};			

			size_t nInputBytes = (size_t)_Input.f_GetLen();

			auto bOK = tinfl_decompress_mem_to_callback
							(
									_Input.f_GetArray()
								,	&nInputBytes
								,	fl_ReceiveData
								,	&Output
								,	0
							);

			if (bOK != 1)
			{
				DMibError("Decompression failed");
			}

			return fg_Move(Output);
		}

	} // Namespace NDataProcessing

} // Namspace NMib
