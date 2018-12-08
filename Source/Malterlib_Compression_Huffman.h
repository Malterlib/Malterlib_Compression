// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>

namespace NMib::NCompression
{
	template <typename t_CAllocator = NMib::NMemory::CDefaultAllocator>
	class TCCompress_Huffman
	{

		class CHuffmanNode
		{
		public:
			CHuffmanNode()
			{
				NMemory::fg_MemClear(this, sizeof(CHuffmanNode));
			}

			int32 m_Frequency;	// must be first for uint8 align
			uint8 m_ByAscii;
			uint32 m_Code;
			int32 m_CodeLength;
			CHuffmanNode *m_pParent;
			CHuffmanNode *m_pLeft;
			CHuffmanNode *m_pRight;
		};

		CHuffmanNode* fp_PopNode(CHuffmanNode *_pNodes[], aint _iIndex, bint _bRight)
		{
			CHuffmanNode *pNode = _pNodes[_iIndex];
			pNode->m_Code = _bRight;
			pNode->m_CodeLength = 1;
			return pNode;
		}

		void fp_SetNodeCode(CHuffmanNode* _pNode)
		{
			CHuffmanNode* pParent = _pNode->m_pParent;
			while (pParent && pParent->m_CodeLength)
			{
				_pNode->m_Code <<= 1;
				_pNode->m_Code |= pParent->m_Code;
				_pNode->m_CodeLength++;
				pParent = pParent->m_pParent;
			}
		}

		aint fp_GetHuffmanTree(CHuffmanNode _Nodes[], bint _bSetCodes = true)
		{
			CHuffmanNode *pNodes[256];
			CHuffmanNode *pNode;

			// add used ascii to Huffman queue
			aint nNodes = 0;
			for (aint iCount = 0; iCount < 256 && _Nodes[iCount].m_Frequency; iCount++)
				pNodes[nNodes++] = &_Nodes[iCount];
			aint iParentNode = nNodes;
			aint iBackNode = nNodes-1;

			while (iBackNode > 0)
			{
				// parent node
				pNode = &_Nodes[iParentNode++];
				// pop first child
				pNode->m_pLeft = fp_PopNode(pNodes, iBackNode--, false);
				// pop second child
				pNode->m_pRight = fp_PopNode(pNodes, iBackNode--, true);
				// adjust parent of the two poped nodes
				pNode->m_pLeft->m_pParent = pNode->m_pRight->m_pParent = pNode;
				// adjust parent frequency
				pNode->m_Frequency = pNode->m_pLeft->m_Frequency + pNode->m_pRight->m_Frequency;
				// insert parent node depending on its frequency
				aint i = iBackNode;
				for (i = iBackNode; i >= 0; i--)
					if(pNodes[i]->m_Frequency >= pNode->m_Frequency)
						break;
				NMemory::fg_MemMove(pNodes+i+2, pNodes+i+1, (iBackNode-i)*sizeof(aint));
				pNodes[i+1] = pNode;
				iBackNode++;
			}
			// set tree leaves nodes code
			if (_bSetCodes)
				for (aint iCount = 0; iCount < nNodes; iCount++)
					fp_SetNodeCode(&_Nodes[iCount]);

			return nNodes;
		}

	public:

		class CSort_FrequencyCompare
		{
		public:
			bint operator()(CHuffmanNode &_Elem1, CHuffmanNode &_Elem2) const
			{
				return _Elem1.m_Frequency > _Elem2.m_Frequency;
			}
		};

		class CSort_ASCIICompare
		{
		public:
			bint operator()(CHuffmanNode &_Elem1, CHuffmanNode &_Elem2) const
			{
				return _Elem1.m_ByAscii < _Elem2.m_ByAscii;
			}
		};

		bool f_CompressHuffman(const void *_pSrc, aint _SrcLen, void *&_pDes, aint &_DesLen)
		{
			// source
			uint8 *pSrc = (uint8*)_pSrc;

			CHuffmanNode Nodes[511];
			// initialize nodes ascii
			for(aint iCount = 0; iCount < 256; iCount++)
				Nodes[iCount].m_ByAscii = iCount;

			// get ascii frequencies
			for(aint iCount = 0; iCount < _SrcLen; iCount++)
				Nodes[pSrc[iCount]].m_Frequency++;
			// sort ascii chars depending on frequency

			NMemory::fg_QSort(Nodes, 256, CSort_FrequencyCompare());
			//qsort(nodes, 256, sizeof(CHuffmanNode), frequencyCompare);

			// construct Huffman tree
			aint nNodes = fp_GetHuffmanTree(Nodes);
			// construct compressed buffer
			aint NodeSize = sizeof(uint32)+sizeof(uint8);
			_DesLen = _SrcLen+nNodes*NodeSize;

			// dest
			_pDes = (uint8*)t_CAllocator::f_Alloc(_DesLen);
			uint8 *pDes = (uint8*)_pDes;

			uint8 *pDesPtr = pDes;
			NMemory::fg_MemClear(pDesPtr, _DesLen);
			// save source buffer length at the first uint32
			*(uint32*)pDesPtr = _SrcLen;
			pDesPtr += sizeof(uint32);
			// save Huffman tree leaves count-1 (as it may be 256)
			*pDesPtr = nNodes-1;
			pDesPtr += sizeof(uint8);
			// save Huffman tree used leaves nodes
			for(aint iCount = 0; iCount < nNodes; iCount++)
			{	// the array sorted on frequency so used nodes come first
				NMemory::fg_MemCopy(pDesPtr, &Nodes[iCount], NodeSize);
				pDesPtr += NodeSize;
			}
			// sort nodes depending on ascii to can index nodes with its ascii value
			NMemory::fg_QSort(Nodes, 256, CSort_ASCIICompare());

			aint iDes = 0;
			// loop to write codes
			for(aint iCount = 0; iCount < _SrcLen; iCount++)
			{
				*(uint32*)(pDesPtr+(iDes>>3)) |= Nodes[pSrc[iCount]].m_Code << (iDes&7);
				iDes += Nodes[pSrc[iCount]].m_CodeLength;
			}
			// update destination length
			_DesLen = (pDesPtr-pDes)+(iDes+7)/8;
			mint Size = _DesLen;
			_pDes = t_CAllocator::f_Resize(_pDes, Size, 0);

			return true;
		}

		bool f_DecompressHuffman(const void *_pSrc, aint _SrcLen, void *&_pDes, aint &_DesLen)
		{
			// source
			uint8 *pSrc = (uint8*)_pSrc;

			// copy destination final length
			_DesLen = *((uint32*)pSrc);

			// allocate buffer for decompressed buffer
			_pDes = (uint8*)t_CAllocator::f_Alloc(_DesLen+1);
			uint8 *pDes = (uint8*)_pDes;

			aint nNodes = *(pSrc+sizeof(uint32))+1;
			// initialize Huffman nodes with frequency and ascii
			CHuffmanNode Nodes[511];
			aint NodeSize = sizeof(uint32)+sizeof(uint8);
			aint iSrc = NodeSize;
			for(aint iCount = 0; iCount < nNodes; iCount++)
			{
				NMemory::fg_MemCopy(&Nodes[iCount], pSrc+iSrc, NodeSize);
				iSrc += NodeSize;
			}
			// construct Huffman tree
			fp_GetHuffmanTree(Nodes, false);
			// get Huffman tree root
			CHuffmanNode *pRoot = &Nodes[0];
			while(pRoot->m_pParent)
				pRoot = pRoot->m_pParent;

			aint iDes = 0;
			CHuffmanNode *pNode;
			uint32 Code;
			iSrc <<= 3;	// convert from bits to bytes
			while(iDes < _DesLen)
			{
				Code = (*(uint32*)(pSrc+(iSrc>>3)))>>(iSrc&7);
				pNode = pRoot;
				while(pNode->m_pLeft)	// if node has pLeft then it must has pRight
				{	// node not leaf
					pNode = (Code&1) ? pNode->m_pRight : pNode->m_pLeft;
					Code >>= 1;
					iSrc++;
				}
				pDes[iDes++] = pNode->m_ByAscii;
			}

			return true;
		}
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NCompression;
#endif
