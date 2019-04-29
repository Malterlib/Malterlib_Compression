// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

namespace NMib::NCompression
{
	template <typename t_CStreamType>
	TCBinaryStream_ZLib<t_CStreamType>::TCBinaryStream_ZLib(ECompressZlibLevel _Level)
		: mp_Compressor(_Level)
	{
		mp_pStream = nullptr;
		mp_OpenFlags = NFile::EFileOpen_None;
		mp_LastChunk = 0;
		mp_FilePos = 0;
		mp_FileLen = 0;
		mp_CompressedLen = 0;
	}

	template <typename t_CStreamType>
	TCBinaryStream_ZLib<t_CStreamType>::~TCBinaryStream_ZLib()
	{
		try
		{
			f_Close(true);
		}
		catch (...)
		{
		}
	}

	template <typename t_CStreamType>
	void TCBinaryStream_ZLib<t_CStreamType>::f_Close(bool _bDestroy)
	{
		if (mp_pStream && (mp_OpenFlags & NFile::EFileOpen_Write))
		{
			fp_WriteChunk(true);

			NStream::CFilePos Pos = mp_pStream->f_GetPosition();
			mp_pStream->f_SetPosition(mp_StartPos);

			uint64 FileLen = mp_FileLen | 0x8000000000000000UL;
			uint32 Version = EVersion_Current;
			uint64 CompressedLen = (Pos - mp_StartPos);

			*mp_pStream << FileLen;
			*mp_pStream << Version;
			*mp_pStream << CompressedLen;

			mp_pStream->f_SetPosition(Pos);
		}

		mp_pStream = nullptr;
		mp_OpenFlags = NFile::EFileOpen_None;
		mp_LastChunk = 0;
		mp_FilePos = 0;
		mp_FileLen = 0;
		mp_CompressedLen = 0;

		if (!_bDestroy)
			mp_Compressor.f_Clear();
	}

	template <typename t_CStreamType>
	void TCBinaryStream_ZLib<t_CStreamType>::f_Open(NStream::CBinaryStream *_pStream, NFile::EFileOpen _OpenFlags)
	{
		f_Close(false);
		mp_pStream = _pStream;
		mp_OpenFlags = _OpenFlags;
		mp_StartPos = mp_pStream->f_GetPosition();
		mp_bCurrentDirty = false;
		mp_LastChunk = 0;

		if (mp_OpenFlags == NFile::EFileOpen_Write)
		{
			mp_FilePos = 0;
			uint64 FileLen = 0;
			uint32 Version = EVersion_Current;
			uint64 CompressedLen = 0;

			*mp_pStream << FileLen;
			*mp_pStream << Version;
			*mp_pStream << CompressedLen;
		}
		else if (mp_OpenFlags == NFile::EFileOpen_Read)
		{
			mp_FilePos = 0;
			uint64 FileLen;
			*mp_pStream >> FileLen;

			if (FileLen & 0x8000000000000000UL)
			{
				FileLen &= ~0x8000000000000000UL;

				uint32 Version;
				uint64 CompressedLen;

				*mp_pStream >> Version;

				switch(Version)
				{
				case EVersion_1:
					{
						*mp_pStream >> CompressedLen;
						mp_CompressedLen = CompressedLen;
						mp_CompressedLen += mp_StartPos;
					}
					break;
				default:
					DMibErrorFile("Invalid zlib stream version");
				}
			}
			else
				mp_CompressedLen = _pStream->f_GetLength();

			mp_FileLen = FileLen;
		}
		else
		{
			DMibErrorFile("You must open the file either with read or write access not both at the same time");
		}
	}

	template <typename t_CStreamType>
	void TCBinaryStream_ZLib<t_CStreamType>::f_Flush(bint _bLocalCacheOnly)
	{
		fp_WriteChunk(true);
	}

	template <typename t_CStreamType>
	void TCBinaryStream_ZLib<t_CStreamType>::f_SetCacheSize(mint _CacheSize)
	{
		mp_pStream->f_SetCacheSize(_CacheSize);
	}

	template <typename t_CStreamType>
	void TCBinaryStream_ZLib<t_CStreamType>::f_FeedBytes(void const *_pMem, mint _nBytes)
	{
		if (!(mp_OpenFlags & NFile::EFileOpen_Write))
			DMibErrorFile("File was not opened for write.");

		uint8 const *pMem = (uint8 const *)_pMem;
		while (_nBytes)
		{
			aint Pos = fp_PrepareBlock(mp_FilePos, true);
			aint ThisTime = fg_Min(_nBytes, (mint)ETempBuffer - Pos);
			NMemory::fg_MemCopy(mp_TempBuffer + Pos, pMem, ThisTime);

			mp_FilePos += ThisTime;
			pMem += ThisTime;
			_nBytes -= ThisTime;
			if (mp_FilePos > mp_FileLen)
				mp_FileLen = mp_FilePos;
		}
	}

	template <typename t_CStreamType>
	void TCBinaryStream_ZLib<t_CStreamType>::f_ConsumeBytes(void *_pMem, mint _nBytes)
	{
		if (!(mp_OpenFlags & NFile::EFileOpen_Read))
			DMibErrorFile("File was not opened for read.");

		if (mp_FilePos + NStream::CFilePos(_nBytes) > mp_FileLen)
			DMibErrorFile("Would read past end of file.");

		uint8 *pMem = (uint8 *)_pMem;
		while (_nBytes)
		{
			aint Pos = fp_PrepareBlock(mp_FilePos, false);
			aint ThisTime = fg_Min(_nBytes, (mint)ETempBuffer - Pos);
			NMemory::fg_MemCopy(pMem, mp_TempBuffer + Pos, ThisTime);

			mp_FilePos += ThisTime;
			pMem += ThisTime;
			_nBytes -= ThisTime;
		}
	}

	template <typename t_CStreamType>
	bint TCBinaryStream_ZLib<t_CStreamType>::f_IsValid() const
	{
		if (!mp_pStream)
			return false;

		return mp_pStream->f_IsValid();
	}

	template <typename t_CStreamType>
	bint TCBinaryStream_ZLib<t_CStreamType>::f_IsAtEndOfStream() const
	{
		return mp_FilePos == mp_FileLen;
	}

	template <typename t_CStreamType>
	NStream::CFilePos TCBinaryStream_ZLib<t_CStreamType>::f_GetPosition() const
	{
		return mp_FilePos;
	}

	template <typename t_CStreamType>
	void TCBinaryStream_ZLib<t_CStreamType>::f_SetPosition(NStream::CFilePos _Pos)
	{
		mp_FilePos = _Pos;
	}

	template <typename t_CStreamType>
	void TCBinaryStream_ZLib<t_CStreamType>::f_SetPositionFromEnd(NStream::CFilePos _Pos)
	{
		mp_FilePos = mp_FileLen - _Pos;
	}

	template <typename t_CStreamType>
	void TCBinaryStream_ZLib<t_CStreamType>::f_AddPosition(NStream::CFilePos _Pos)
	{
		mp_FilePos += _Pos;
	}

	template <typename t_CStreamType>
	bint TCBinaryStream_ZLib<t_CStreamType>::f_IsValidReadPosition(NStream::CFilePos _Pos) const
	{
		return _Pos >= 0 && _Pos < NStream::CFilePos(mp_FileLen);
	}

	template <typename t_CStreamType>
	NStream::CFilePos TCBinaryStream_ZLib<t_CStreamType>::f_GetLength() const
	{
		return mp_FileLen;
	}

	template <typename t_CStreamType>
	void TCBinaryStream_ZLib<t_CStreamType>::f_SetLength(NStream::CFilePos _Length)
	{
		DMibError("Not supported");
	}

	template <typename t_CStreamType>
	void TCBinaryStream_ZLib<t_CStreamType>::fp_WriteChunk(bint _bFlush)
	{
		mint nBytes = fg_Min(mp_FileLen - mp_LastChunk, ETempBuffer);
		if (nBytes)
		{
			NMib::NStream::CFilePos nBytesWritten;
			mp_Compressor.f_FeedBytes(mp_pStream, nBytesWritten, mp_TempBuffer, nBytes, _bFlush ? ECompressZlibFlush_Finish : ECompressZlibFlush_None);
			if (nBytes != ETempBuffer && !_bFlush)
				DMibErrorFile("Internal error");
			mp_LastChunk += nBytes;
		}
	}

	template <typename t_CStreamType>
	void TCBinaryStream_ZLib<t_CStreamType>::fp_ReadChunk()
	{
		mint nBytes = fg_Min(mp_FileLen - mp_LastChunk, ETempBuffer);
		if (nBytes)
		{
			mp_LastChunk += nBytes;
			mp_Compressor.f_ConsumeBytes(mp_pStream, mp_CompressedLen, mp_TempBuffer, nBytes, ECompressZlibFlush_None);
		}
	}

	template <typename t_CStreamType>
	aint TCBinaryStream_ZLib<t_CStreamType>::fp_PrepareBlock(NStream::CFilePos _Pos, bint _bWrite)
	{
		if (_bWrite)
		{
			if (_Pos >= mp_LastChunk && _Pos < mp_LastChunk + ETempBuffer)
				return _Pos % ETempBuffer;

			if (_Pos < mp_LastChunk)
				DMibErrorFile("Cannot seek backwards in stream");

			NStream::CFilePos TargetPos = NMib::fg_AlignDown(_Pos, ETempBuffer);
			while (mp_LastChunk < TargetPos)
				fp_WriteChunk(false);

			return _Pos % ETempBuffer;
		}
		else
		{
			if (_Pos >= (mp_LastChunk - ETempBuffer) && _Pos < mp_LastChunk)
				return _Pos % ETempBuffer;

			if (_Pos < (mp_LastChunk - ETempBuffer))
				DMibErrorFile("Cannot seek backwards in stream");


			NStream::CFilePos TargetPos = fg_Min(NMib::fg_AlignDown(_Pos, ETempBuffer) + ETempBuffer, mp_FileLen);
			while (mp_LastChunk < TargetPos)
				fp_ReadChunk();

			return _Pos % ETempBuffer;
		}
	}
}
