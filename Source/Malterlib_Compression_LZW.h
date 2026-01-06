// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>

namespace NMib::NCompression
{
	template <typename t_CAllocator = NMib::NMemory::CDefaultAllocator>
	class TCCompress_LZW
	{

	private:

		class CBuffer
		{
		public:
			CBuffer()
			{
			}

			CBuffer(uint8* _pBuffer, aint _nBytes)
			{
				m_pBuffer = pBuffer;
				m_nBytes = _nBytes;
			}

			CBuffer(const CBuffer &_Buffer)
			{
				*this = _Buffer
			}

		public:
			uint8* m_pBuffer;
			aint m_nBytes;

			inline aint f_Compare(const CBuffer* _pBuffer)
			{
				int nResult = NMemory::fg_MemCmp((const uint8*)m_pBuffer, (const uint8*)_pBuffer->m_pBuffer, fg_Min(m_nBytes, _pBuffer->m_nBytes));
				if (nResult != 0 || m_nBytes == _pBuffer->m_nBytes)
					return nResult;
				return m_nBytes > _pBuffer->m_nBytes ? 1 : -1;
			}

			inline void operator = (const CBuffer* _pBuffer)
			{
				m_pBuffer = _pBuffer->m_pBuffer;
				m_nBytes = _pBuffer->m_nBytes;
			}
		};

		void *fp_ReAllocPtr(mint _Size)
		{
			void *pBlock = t_CAllocator::f_Alloc(_Size);
			return NMemory::fg_MemClear(pBlock, _Size);
		}

		void *fp_ReAllocPtr(void *_pBlock, aint _Size)
		{
			aint OldSize = t_CAllocator::f_Size(_pBlock);
			_pBlock = t_CAllocator::f_Realloc(_pBlock, _Size, OldSize);
			if(_Size > OldSize)
			{
				_Size = t_CAllocator::f_Size(_pBlock);
				NMemory::fg_MemClear((uint8*)_pBlock+OldSize, _Size-OldSize);
			}

			return _pBlock;
		}

	public:

		bool f_CompressLZW(const void *_pSrc, aint _nSrcBytes, void *&_pDst, aint &_nDstBytes, aint _nBitsPerSample)
		{
			_nDstBytes = (sizeof(uint32)+1)<<3;
			// allocate buffer for destination buffer
			aint nAllocLength = _nSrcBytes*2;
			_pDst = (uint8*)fp_ReAllocPtr(nAllocLength);

			uint8 *pDst = (uint8*)_pDst;
			uint8 *pSrc = (uint8*)_pSrc;

			// save source buffer length at the first uint32
			*(uint32*)pDst = _nSrcBytes;
			*(pDst+sizeof(uint32)) = _nBitsPerSample;
			aint nSample = *pSrc;
			aint nMaxSamples = 1 << _nBitsPerSample;
			// Dictionary hash table
			TCLZWBinaryTree<CBuffer, CBuffer*, aint, aint> Dictionary;
			Dictionary.NoRepeat = true;
			// keep first 256 IDs for ascii Samples
			Dictionary.Serial = 256;
			// tree node to keep last success search to start with
			TCLZWBinaryTreeNode<CBuffer, aint>* pNode = Dictionary.Root;
			// left Dictionary Samples points to the source buffer
			CBuffer node(pSrc, 2);
			// scan the input buffer
			while(_nSrcBytes-- > 0)
			{
				if(Dictionary.Serial == nMaxSamples)
				{
					Dictionary.f_RemoveAll();
					Dictionary.Serial = 256;
				}
				pNode = Dictionary.f_Insert(&node, -1, pNode);
				if(pNode->Count > 1)
					// (repeated Sample), save success Sample to be used next fail
					nSample = pNode->ID, node.m_nBytes++;
				else
				{	// write last success Sample
					if((_nDstBytes>>3)+(aint)sizeof(uint32) >= nAllocLength)
						pDst = (uint8*)fp_ReAllocPtr(pDst, nAllocLength += 100);
					*(uint32*)(pDst+(_nDstBytes>>3)) |= nSample << (_nDstBytes&7);
					_nDstBytes += _nBitsPerSample;
					// initialize node to next Sample
					node.m_pBuffer += node.m_nBytes-1;
					node.m_nBytes = 2;
					// copy first byte of the node as a new Sample
					if(_nSrcBytes > 0)
						nSample = *node.m_pBuffer;
					// initialize search root
					pNode = Dictionary.Root;
				}
			}
			_nDstBytes = (_nDstBytes+7)/8;

			pDst = (uint8*)fp_ReAllocPtr(pDst, _nDstBytes);

			_pDst = pDst;

			return true;
		}

		void f_ClearDictionary(NContainer::TCVector<CBuffer *>& _Dictionary)
		{
			_Dictionary.f_DeleteAll();
		}

		bool f_DecompressLZW(const void *_pSrc, aint _nSrcBytes, void *&_pDst, aint &_nDstBytes)
		{
			uint8 *pSrc = (uint8*)_pSrc;

			// first two DWORDS (final buffer length, Samples sizes bitmap buffer start)
			// copy destination final length
			_nDstBytes = *(uint32*)pSrc;

			// copy bits pre Sample
			aint _nBitsPerSample = *(pSrc+sizeof(uint32));
			// allocate buffer for decompressed buffer
			_pDst = (uint8*)t_CAllocator::f_Alloc(_nDstBytes+1);

			uint8 *pDst = (uint8*)_pDst;

			// copy first char from source to destination
			*pDst = *(pSrc+sizeof(uint32)+1);
			aint nMaxSamples = 1 << _nBitsPerSample;
			aint nSample, nSrcIndex = ((sizeof(uint32)+1)<<3) + _nBitsPerSample;
			// Dictionary array
			NContainer::TCVector<CBuffer *> Dictionary;
			// let Dictionary Samples points to the destination buffer
			CBuffer node(pDst, 2), *pNodeSample;
			aint nDesIndex = 1, nDesIndexSave = 0, nSampleLen;

			while(nDesIndex < _nDstBytes)
			{
				nSample = (*(uint32*)(pSrc+(nSrcIndex>>3)))>>(nSrcIndex&7) & (nMaxSamples-1);
				nSrcIndex += _nBitsPerSample;

				if((aint)Dictionary.f_GetLen() == nMaxSamples-256)
					f_ClearDictionary(Dictionary);

				if(nSample >= 256)
					if(nSample-256 < (aint)Dictionary.f_GetLen())
					{	// normal case, valid Dictionary Sample
						nDesIndexSave = nDesIndex;
						Dictionary.f_GetAt(nSample-256, pNodeSample);
						nSampleLen = pNodeSample->m_nBytes+1;
						// copy Dictionary node buffer to decompressed buffer
						NMemory::fg_MemCopy(pDst+nDesIndex, pNodeSample->m_pBuffer, pNodeSample->m_nBytes);
						nDesIndex += pNodeSample->m_nBytes;
					}
					else
					{	// out of range Sample
						nSampleLen = nDesIndex-nDesIndexSave+2;
						// copy previous decompressed Sample as a new one + ...
						NMemory::fg_MemCopy(pDst+nDesIndex, pDst+nDesIndexSave, nDesIndex-nDesIndexSave);
						nDesIndex += nDesIndex-nDesIndexSave;
						// add first char of the previous decompressed Sample
						*(pDst+nDesIndex++) = *(pDst+nDesIndexSave);
						nDesIndexSave += nSampleLen-2;
					}
				else
					nDesIndexSave = nDesIndex, *(pDst+nDesIndex++) = (uint8)nSample, nSampleLen = 2;

				// add current segment to the Dictionary
				Dictionary.f_Insert(new CBuffer(node));
				// increment next node pointer to the last char of the added Sample
				node.m_pBuffer += node.m_nBytes-1;
				node.m_nBytes = nSampleLen;
			}
			// free Dictionary Samples
			f_ClearDictionary(Dictionary);

			return true;
		}
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NCompression;
#endif
