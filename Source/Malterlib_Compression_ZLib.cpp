// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include "Malterlib_Compression_ZLib.h"

#include "zlib.h"
#include "zutil.h"

namespace NMib::NCompression
{
	using namespace NStr;
	struct CCompress_ZLib::CInternal
	{
		enum
		{
			ETempBuffer = 4*1024
		};

		CInternal(ECompressZlibLevel _Level, ECompressZlibType _Type)
			: m_CompressionLevel(_Level)
			, m_OpenMode(0)
			, m_Type(_Type)
		{
			NMemory::fg_MemClear(m_Stream);
		}

		~CInternal()
		{
			if (m_OpenMode == 1)
				deflateEnd(&m_Stream);
			else if (m_OpenMode == 2)
				inflateEnd(&m_Stream);
		}

		void f_FeedBytes(NStream::CBinaryStream *_pOutStream, NStream::CFilePos &_OutBytesWritten, void const *_pMem, umint _nBytes, ECompressZlibFlush _Flush)
		{
			if (m_OpenMode != 1)
			{
				if (m_OpenMode != 0)
					DMibError("ZLib cannot compress and decompress in same session.");

				int Error;
				if (m_Type == ECompressZlibType_GZip)
					Error = deflateInit2(&m_Stream, m_CompressionLevel, Z_DEFLATED, MAX_WBITS + 16, DEF_MEM_LEVEL, 0);
				else
					Error = deflateInit(&m_Stream, m_CompressionLevel);

				if (Error != Z_OK)
					DMibError("inflateInit failed. ErrorCode: {}"_f << Error);

				m_OpenMode = 1;
				m_Stream.next_out = m_TempBuffer;
				m_Stream.avail_out = ETempBuffer;
			}

			m_Stream.next_in = (Bytef *)_pMem;
			m_Stream.avail_in = _nBytes;

			while (m_Stream.avail_in)
			{
				if (!m_Stream.avail_out)
				{
					_pOutStream->f_FeedBytes(m_TempBuffer, ETempBuffer);
					_OutBytesWritten += ETempBuffer;
					m_Stream.next_out = m_TempBuffer;
					m_Stream.avail_out = ETempBuffer;
				}

				int Error = deflate(&m_Stream, Z_NO_FLUSH);

				if (Error != Z_OK && Error != Z_BUF_ERROR)
					DMibError("Compression error: {}"_f << Error);
			}

			if (_Flush == ECompressZlibFlush_Sync)
			{
				umint Len = ETempBuffer - m_Stream.avail_out;
				_pOutStream->f_FeedBytes(m_TempBuffer, Len);

				do
				{
					m_Stream.next_out = m_TempBuffer;
					m_Stream.avail_out = ETempBuffer;

					int Error = deflate(&m_Stream, Z_SYNC_FLUSH);
					umint Len = ETempBuffer - m_Stream.avail_out;

					_pOutStream->f_FeedBytes(m_TempBuffer, Len);
					_OutBytesWritten += Len;

					if (Error != Z_OK && Error != Z_BUF_ERROR)
						DMibError("Compression error: {}"_f << Error);
				}
				while (m_Stream.avail_out == 0)
					;

				m_Stream.next_out = m_TempBuffer;
				m_Stream.avail_out = ETempBuffer;
			}
			else if (_Flush == ECompressZlibFlush_Finish)
			{
				int Error = Z_OK;
				while (Error != Z_STREAM_END)
				{
					Error = deflate(&m_Stream, Z_FINISH);
					umint Len = ETempBuffer - m_Stream.avail_out;
					_pOutStream->f_FeedBytes(m_TempBuffer, Len);
					_OutBytesWritten += Len;

					if (Error != Z_OK && Error != Z_BUF_ERROR && Error != Z_STREAM_END)
						DMibError("Compression error: {}"_f << Error);

					m_Stream.next_out = m_TempBuffer;
					m_Stream.avail_out = ETempBuffer;
				}
			}
		}

		void f_ConsumeBytes(NStream::CBinaryStream *_pInStream, NStream::CFilePos _InStreamLength, void const *_pMem, umint _nBytes, ECompressZlibFlush _Flush)
		{
			if (f_TryConsumeBytes(_pInStream, _InStreamLength, _pMem, _nBytes, _Flush) != _nBytes)
				DMibError("Ran out of compressed data");
		}

		umint f_TryConsumeBytes(NStream::CBinaryStream *_pInStream, NStream::CFilePos _InStreamLength, void const *_pMem, umint _nBytes, ECompressZlibFlush _Flush)
		{
			if (m_OpenMode != 2)
			{
				if (m_OpenMode != 0)
					DMibError("ZLib cannot compress and decompress in same session.");

				int Error;
				if (m_Type == ECompressZlibType_GZip)
					Error = inflateInit2(&m_Stream, MAX_WBITS + 16);
				else
					Error = inflateInit(&m_Stream);

				if (Error != Z_OK)
					DMibError("inflateInit failed. ErrorCode: {}"_f << Error);
				m_OpenMode = 2;
			}

			if (_InStreamLength == 0)
				_InStreamLength = _pInStream->f_GetLength();

			m_Stream.next_out = (Bytef *)_pMem;
			m_Stream.avail_out = _nBytes;

			while (m_Stream.avail_out)
			{
				if (!m_Stream.avail_in)
				{
					m_Stream.next_in = m_TempBuffer;
					m_Stream.avail_in = fg_Min(_InStreamLength - _pInStream->f_GetPosition(), ETempBuffer);
					if (m_Stream.avail_in == 0)
					{
						DMibLog(Error, "ZLib inflate: No available input!", 0);
						break;
					}
					_pInStream->f_ConsumeBytes(m_Stream.next_in, m_Stream.avail_in);
				}

				int Error = inflate(&m_Stream, Z_NO_FLUSH);

				if (Error == Z_STREAM_END)
					break;

				if (Error != Z_OK && Error != Z_BUF_ERROR)
					DMibError("Compression error: {}"_f << Error);
			}

			umint Return = _nBytes - m_Stream.avail_out;

			if (_Flush == ECompressZlibFlush_Sync)
			{
				Bytef Temp[1024];
				while (_InStreamLength - _pInStream->f_GetPosition())
				{
					if (!m_Stream.avail_in)
					{
						m_Stream.next_in = m_TempBuffer;
						m_Stream.avail_in = fg_Min(_InStreamLength - _pInStream->f_GetPosition(), ETempBuffer);
						if (m_Stream.avail_in == 0)
						{
							DMibLog(Error, "ZLib inflate: No available input during flush sync!", 0);
							break;
						}
						_pInStream->f_ConsumeBytes(m_Stream.next_in, m_Stream.avail_in);
					}

					m_Stream.next_out = Temp;
					m_Stream.avail_out = 1024;
					int Error = inflate(&m_Stream, Z_NO_FLUSH);

					DMibCheck(m_Stream.avail_out == 1024);

					if (Error == Z_STREAM_END)
						break;

					if (Error != Z_OK && Error != Z_BUF_ERROR)
						DMibError("Compression error");

				}
				m_Stream.next_out = nullptr;
				m_Stream.avail_out = 0;
			}

			return Return;
		}

		ECompressZlibLevel m_CompressionLevel;
		ECompressZlibType m_Type;
		uint32 m_OpenMode;
		z_stream m_Stream;
		uint8 m_TempBuffer[ETempBuffer];
	};

	CCompress_ZLib::CCompress_ZLib(ECompressZlibLevel _Level, ECompressZlibType _Type)
		: mp_pInternal(fg_Construct(_Level, _Type))
	{
	}

	CCompress_ZLib::~CCompress_ZLib()
	{
		mp_pInternal.f_Clear();
	}

	void CCompress_ZLib::f_Clear()
	{
		ECompressZlibLevel Level = mp_pInternal->m_CompressionLevel;
		ECompressZlibType Type = mp_pInternal->m_Type;

		mp_pInternal = fg_Construct(Level, Type);
	}

	void CCompress_ZLib::f_FeedBytes(NStream::CBinaryStream * _pOutStream, NStream::CFilePos &_BytesWritten, void const *_pMem, umint _nBytes, ECompressZlibFlush _Flush)
	{
		mp_pInternal->f_FeedBytes(_pOutStream, _BytesWritten, _pMem, _nBytes, _Flush);
	}

	void CCompress_ZLib::f_ConsumeBytes(NStream::CBinaryStream *_pInStream, NStream::CFilePos _StreamLength, void const *_pMem, umint _nBytes, ECompressZlibFlush _Flush)
	{
		mp_pInternal->f_ConsumeBytes(_pInStream, _StreamLength, _pMem, _nBytes, _Flush);
	}

	umint CCompress_ZLib::f_TryConsumeBytes(NStream::CBinaryStream *_pInStream, NStream::CFilePos _StreamLength, const void *_pMem, umint _nBytes, ECompressZlibFlush _Flush)
	{
		return mp_pInternal->f_TryConsumeBytes(_pInStream, _StreamLength, _pMem, _nBytes, _Flush);
	}

	NContainer::CByteVector fg_CompressZLib(NContainer::CByteVector const &_Source)
	{
		NStream::CBinaryStreamMemory<> MemoryStream;
		TCBinaryStream_ZLib<> Stream;

		Stream.f_Open(&MemoryStream, NFile::EFileOpen_Write);
		Stream << _Source;
		Stream.f_Close();

		return MemoryStream.f_MoveVector();
	}

	NContainer::CByteVector fg_DecompressZLib(NContainer::CByteVector const &_Source)
	{
		NStream::CBinaryStreamMemoryPtr<> StreamMemory;
		StreamMemory.f_OpenRead(_Source.f_GetArray(), _Source.f_GetLen());

		TCBinaryStream_ZLib<> Stream;
		Stream.f_Open(&StreamMemory, NFile::EFileOpen_Read);

		NContainer::CByteVector Destination;
		Stream >> Destination;
		return Destination;
	}

	void fg_CompressGZip(NStream::CBinaryStream &_SourceStream, NStream::CBinaryStream &_DestinationStream, ECompressZlibLevel _Level)
	{
		CCompress_ZLib Compress(_Level, ECompressZlibType_GZip);

		uint8 Buffer[8192];

		NStream::CFilePos SourceBytes = _SourceStream.f_GetLength();
		while (SourceBytes)
		{
			umint ToWrite = (umint)fg_Min(SourceBytes, NStream::CFilePos(8192));
			_SourceStream.f_ConsumeBytes(Buffer, ToWrite);
			SourceBytes -= ToWrite;
			NStream::CFilePos OutBytesWritten = 0;
			Compress.f_FeedBytes(&_DestinationStream, OutBytesWritten, Buffer, ToWrite, SourceBytes == 0 ? ECompressZlibFlush_Finish : ECompressZlibFlush_None);
		}
	}

	NContainer::CByteVector fg_CompressGZip(NContainer::CByteVector const &_SourceData, ECompressZlibLevel _Level)
	{
		NStream::CBinaryStreamMemoryConstRef<> SourceStream(_SourceData);
		NStream::CBinaryStreamMemory<> DestStream;
		fg_CompressGZip(SourceStream, DestStream, _Level);

		return DestStream.f_MoveVector();
	}

	void fg_CompressGZip(NStr::CStr const &_SourceFile, NStream::CBinaryStream &_DestinationStream, ECompressZlibLevel _Level)
	{
		NFile::TCBinaryStreamFile<> SourceStream;
		SourceStream.f_Open(_SourceFile, NFile::EFileOpen_Read | NFile::EFileOpen_ShareAll);
		fg_CompressGZip(SourceStream, _DestinationStream, _Level);
	}

	void fg_CompressGZip(NStream::CBinaryStream &_SourceStream, NStr::CStr const &_DestinationFile, ECompressZlibLevel _Level)
	{
		NFile::TCBinaryStreamFile<> DestinationStream;
		DestinationStream.f_Open(_DestinationFile, NFile::EFileOpen_Write | NFile::EFileOpen_ShareAll);
		fg_CompressGZip(_SourceStream, DestinationStream, _Level);
	}

	void fg_CompressGZip(NStr::CStr const &_SourceFile, NStr::CStr const &_DestinationFile, ECompressZlibLevel _Level)
	{
		NFile::TCBinaryStreamFile<> SourceStream;
		SourceStream.f_Open(_SourceFile, NFile::EFileOpen_Read | NFile::EFileOpen_ShareAll);
		fg_CompressGZip(SourceStream, _DestinationFile, _Level);
	}

	void fg_DecompressGZip(NStream::CBinaryStream &_SourceStream, NStream::CBinaryStream &_DestinationStream)
	{
		CCompress_ZLib Compress(ECompressZlibLevel_Best, ECompressZlibType_GZip);

		uint8 Buffer[8192];

		NStream::CFilePos SourceBytes = _SourceStream.f_GetLength();
		while (true)
		{
			umint nRead = Compress.f_TryConsumeBytes(&_SourceStream, SourceBytes, Buffer, 8192, ECompressZlibFlush_None);
			_DestinationStream.f_FeedBytes(Buffer, nRead);
			if (nRead < 8192)
				break;
		}
	}

	void fg_DecompressGZip(NStr::CStr const &_SourceFile, NStream::CBinaryStream &_DestinationStream)
	{
		NFile::TCBinaryStreamFile<> SourceStream;
		SourceStream.f_Open(_SourceFile, NFile::EFileOpen_Read | NFile::EFileOpen_ShareAll);
		fg_DecompressGZip(SourceStream, _DestinationStream);
	}

	void fg_DecompressGZip(NStream::CBinaryStream &_SourceStream, NStr::CStr const &_DestinationFile)
	{
		NFile::TCBinaryStreamFile<> DestinationStream;
		DestinationStream.f_Open(_DestinationFile, NFile::EFileOpen_Write | NFile::EFileOpen_ShareAll);
		fg_DecompressGZip(_SourceStream, DestinationStream);
	}

	void fg_DecompressGZip(NStr::CStr const &_SourceFile, NStr::CStr const &_DestinationFile)
	{
		NFile::TCBinaryStreamFile<> SourceStream;
		SourceStream.f_Open(_SourceFile, NFile::EFileOpen_Read | NFile::EFileOpen_ShareAll);
		fg_DecompressGZip(SourceStream, _DestinationFile);
	}

	NContainer::CByteVector fg_DecompressGZip(NContainer::CByteVector const &_SourceData)
	{
		NStream::CBinaryStreamMemoryConstRef<> SourceStream(_SourceData);
		NStream::CBinaryStreamMemory<> DestStream;
		fg_DecompressGZip(SourceStream, DestStream);

		return DestStream.f_MoveVector();
	}
}
