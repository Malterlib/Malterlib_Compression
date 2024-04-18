// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once
#include <Mib/Core/Core>
#include <Mib/Stream/Binary>

namespace NMib::NCompression
{
	enum ECompressZlibLevel
	{
		ECompressZlibLevel_Fastest = 1,
		ECompressZlibLevel_2,
		ECompressZlibLevel_3,
		ECompressZlibLevel_4,
		ECompressZlibLevel_5,
		ECompressZlibLevel_6,
		ECompressZlibLevel_7,
		ECompressZlibLevel_8,
		ECompressZlibLevel_Best = 9,
	};

	enum ECompressZlibType
	{
		ECompressZlibType_ZLib = 0
		, ECompressZlibType_GZip
	};

	enum ECompressZlibFlush
	{
		ECompressZlibFlush_None,
		ECompressZlibFlush_Sync,
		ECompressZlibFlush_Finish,
	};

	struct CCompress_ZLib
	{
		CCompress_ZLib(ECompressZlibLevel _Level = ECompressZlibLevel_Best, ECompressZlibType _Type = ECompressZlibType_ZLib);
		~CCompress_ZLib();

		void f_Clear();
		void f_FeedBytes(NStream::CBinaryStream *_pOutStream, NStream::CFilePos &o_BytesWritten, const void *_pMem, mint _nBytes, ECompressZlibFlush _Flush);
		void f_ConsumeBytes(NStream::CBinaryStream *_pInStream, NStream::CFilePos _StreamLength, const void *_pMem, mint _nBytes, ECompressZlibFlush _Flush);
		mint f_TryConsumeBytes(NStream::CBinaryStream *_pInStream, NStream::CFilePos _StreamLength, const void *_pMem, mint _nBytes, ECompressZlibFlush _Flush);

	private:
		struct CInternal;
		NStorage::TCUniquePointer<CInternal> mp_pInternal;
	};

	template <typename t_CStreamType = NStream::CBinaryStreamDefault>
	class TCBinaryStream_ZLib : public t_CStreamType
	{
	public:
		DMibStreamImplementOperators(TCBinaryStream_ZLib);

		TCBinaryStream_ZLib(ECompressZlibLevel _Level = ECompressZlibLevel_Best);
		~TCBinaryStream_ZLib();

		void f_Close(bool _bDestroy = false);
		void f_Open(NStream::CBinaryStream *_pStream, NFile::EFileOpen _OpenFlags);
		void f_Flush(bool _bLocalCacheOnly);
		void f_SetCacheSize(mint _CacheSize);
		void f_FeedBytes(const void *_pMem, mint _nBytes);
		void f_ConsumeBytes(void *_pMem, mint _nBytes);

		bool f_IsValid() const;
		bool f_IsAtEndOfStream() const;
		NStream::CFilePos f_GetPosition() const;
		void f_SetPosition(NStream::CFilePos _Pos);
		void f_SetPositionFromEnd(NStream::CFilePos _Pos);
		void f_AddPosition(NStream::CFilePos _Pos);
		bool f_IsValidReadPosition(NStream::CFilePos _Pos) const;

		NStream::CFilePos f_GetLength() const;
		void f_SetLength(NStream::CFilePos _Length);
		mint f_ContainerLengthLimit() const;
		
	protected:
		DMibStreamImplementProtected(TCBinaryStream_ZLib);

	private:
		enum
		{
			ETempBuffer = 4*1024,
		};

		enum EVersion
		{
			EVersion_0	= 0,
			EVersion_1	= 1,
			EVersion_Current = EVersion_1,
		};

		void fp_WriteChunk(bool _bFlush);
		void fp_ReadChunk();
		aint fp_PrepareBlock(NStream::CFilePos _Pos, bool _bWrite);

		CCompress_ZLib mp_Compressor;
		NStream::CBinaryStream *mp_pStream;
		NFile::EFileOpen mp_OpenFlags;

		NStream::CFilePos mp_StartPos;
		NStream::CFilePos mp_LastChunk;
		NStream::CFilePos mp_FilePos;
		NStream::CFilePos mp_FileLen;
		NStream::CFilePos mp_CompressedLen;

		uint8 mp_TempBuffer[ETempBuffer];
		bool mp_bCurrentDirty;
	};

	NContainer::CByteVector fg_CompressZLib(NContainer::CByteVector const &_Source);
	NContainer::CByteVector fg_DecompressZLib(NContainer::CByteVector const &_Source);

	void fg_CompressGZip(NStream::CBinaryStream &_SourceStream, NStream::CBinaryStream &_DestinationStream, ECompressZlibLevel _Level = ECompressZlibLevel_Best);
	void fg_CompressGZip(NStr::CStr const &_SourceFile, NStream::CBinaryStream &_DestinationStream, ECompressZlibLevel _Level = ECompressZlibLevel_Best);
	void fg_CompressGZip(NStream::CBinaryStream &_SourceStream, NStr::CStr const &_DestinationFile, ECompressZlibLevel _Level = ECompressZlibLevel_Best);
	void fg_CompressGZip(NStr::CStr const &_SourceFile, NStr::CStr const &_DestinationFile, ECompressZlibLevel _Level = ECompressZlibLevel_Best);
	NContainer::CByteVector fg_CompressGZip(NContainer::CByteVector const &_SourceData, ECompressZlibLevel _Level = ECompressZlibLevel_Best);

	void fg_DecompressGZip(NStream::CBinaryStream &_SourceStream, NStream::CBinaryStream &_DestinationStream);
	void fg_DecompressGZip(NStr::CStr const &_SourceFile, NStream::CBinaryStream &_DestinationStream);
	void fg_DecompressGZip(NStream::CBinaryStream &_SourceStream, NStr::CStr const &_DestinationFile);
	void fg_DecompressGZip(NStr::CStr const &_SourceFile, NStr::CStr const &_DestinationFile);
	NContainer::CByteVector fg_DecompressGZip(NContainer::CByteVector const &_SourceData);
}

#include "Malterlib_Compression_ZLib.hpp"

#ifndef DMibPNoShortCuts
	using namespace NMib::NCompression;
#endif
