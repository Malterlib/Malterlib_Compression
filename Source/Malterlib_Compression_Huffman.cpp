// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include "Malterlib_Compression_Huffman.h"

namespace NMib::NCompression
{
	// Serialized node size: uint32 frequency + uint8 ascii
	namespace
	{
		static constexpr umint gc_NodeSerializedSize = sizeof(uint32) + sizeof(uint8);

		struct CHuffmanNode
		{
			CHuffmanNode() = default;

			CHuffmanNode *m_pParent = nullptr;
			CHuffmanNode *m_pLeft = nullptr;
			CHuffmanNode *m_pRight = nullptr;

			uint64 m_Code = 0;
			uint32 m_Frequency = 0;
			int32 m_CodeLength = 0;
			uint8 m_ByAscii = 0;
		};

		struct CSort_FrequencyCompare
		{
			COrdering_Partial operator()(CHuffmanNode &_Elem1, CHuffmanNode &_Elem2) const;
		};

		struct CSort_ASCIICompare
		{
			COrdering_Partial operator()(CHuffmanNode &_Elem1, CHuffmanNode &_Elem2) const;
		};

		auto fg_PopNode(CHuffmanNode *_pNodes[], umint _iIndex, bool _bRight) -> CHuffmanNode *
		{
			CHuffmanNode *pNode = _pNodes[_iIndex];
			pNode->m_Code = _bRight;
			pNode->m_CodeLength = 1;
			return pNode;
		}

		void fg_SetNodeCode(CHuffmanNode *_pNode)
		{
			CHuffmanNode *pParent = _pNode->m_pParent;
			while (pParent && pParent->m_CodeLength)
			{
				_pNode->m_Code <<= 1;
				_pNode->m_Code |= pParent->m_Code;
				_pNode->m_CodeLength++;
				pParent = pParent->m_pParent;
			}
		}

		umint fg_GetHuffmanTree(CHuffmanNode _Nodes[], bool _bSetCodes)
		{
			CHuffmanNode *pNodes[256];
			CHuffmanNode *pNode;

			// add used ascii to Huffman queue
			umint nNodes = 0;
			for (umint iCount = 0; iCount < 256 && _Nodes[iCount].m_Frequency; iCount++)
				pNodes[nNodes++] = &_Nodes[iCount];
			umint iParentNode = nNodes;
			smint iBackNode = smint(nNodes) - 1;

			while (iBackNode > 0)
			{
				// parent node
				pNode = &_Nodes[iParentNode++];
				// pop first child
				pNode->m_pLeft = fg_PopNode(pNodes, iBackNode--, false);
				// pop second child
				pNode->m_pRight = fg_PopNode(pNodes, iBackNode--, true);
				// adjust parent of the two popped nodes
				pNode->m_pLeft->m_pParent = pNode->m_pRight->m_pParent = pNode;
				// adjust parent frequency
				pNode->m_Frequency = pNode->m_pLeft->m_Frequency + pNode->m_pRight->m_Frequency;
				// insert parent node depending on its frequency
				smint i;
				for (i = iBackNode; i >= 0; i--)
					if (pNodes[i]->m_Frequency >= pNode->m_Frequency)
						break;
				NMemory::fg_MemMove(pNodes + (i + 2), pNodes + (i + 1), (iBackNode - i) * sizeof(CHuffmanNode *));
				pNodes[i + 1] = pNode;
				iBackNode++;
			}
			// set tree leaves nodes code
			if (_bSetCodes)
				for (umint iCount = 0; iCount < nNodes; iCount++)
					fg_SetNodeCode(&_Nodes[iCount]);

			return nNodes;
		}

		COrdering_Partial CSort_FrequencyCompare::operator()(CHuffmanNode &_Elem1, CHuffmanNode &_Elem2) const
		{
			return _Elem2.m_Frequency <=> _Elem1.m_Frequency;
		}

		COrdering_Partial CSort_ASCIICompare::operator()(CHuffmanNode &_Elem1, CHuffmanNode &_Elem2) const
		{
			return _Elem1.m_ByAscii <=> _Elem2.m_ByAscii;
		}

		auto fg_GetCompressedAllocSize(umint _nHeaderSize, umint _nSrcLen) -> umint
		{
			uint64 nAllocSize = uint64(_nHeaderSize) + uint64(_nSrcLen) + sizeof(uint64);
			if (nAllocSize > TCLimitsInt<umint>::mc_Max)
				DMibError("Huffman compression output exceeds maximum buffer length");
			return (umint)nAllocSize;
		}

		auto fg_GetEncodedBitCount(CHuffmanNode const _Nodes[], umint _nNodes) -> uint64
		{
			uint64 nEncodedBits = 0;
			for (umint iCount = 0; iCount < _nNodes; iCount++)
				nEncodedBits += uint64(_Nodes[iCount].m_Frequency) * uint64(_Nodes[iCount].m_CodeLength);
			return nEncodedBits;
		}
	}

	NContainer::CByteVector fg_CompressHuffman(NContainer::CByteVector const &_Source)
	{
		uint8 const *pSrc = _Source.f_GetArray();
		umint nSrcLen = _Source.f_GetLen();

		if (nSrcLen > TCLimitsInt<uint32>::mc_Max)
			DMibError("Huffman compression input exceeds uint32 maximum length");

		if (nSrcLen == 0)
		{
			NContainer::CByteVector Destination;
			Destination.f_SetLen(sizeof(uint32));
			uint32 Zero = 0;
			NMemory::fg_MemCopy(Destination.f_GetArray(), &Zero, sizeof(Zero));
			return Destination;
		}

		CHuffmanNode Nodes[511];
		// initialize nodes ascii
		for (umint iCount = 0; iCount < 256; iCount++)
			Nodes[iCount].m_ByAscii = iCount;

		// get ascii frequencies
		for (umint iCount = 0; iCount < nSrcLen; iCount++)
			Nodes[pSrc[iCount]].m_Frequency++;
		// sort ascii chars depending on frequency

		NMemory::fg_QSort(Nodes, 256, CSort_FrequencyCompare());

		// construct Huffman tree
		umint nNodes = fg_GetHuffmanTree(Nodes, true);
		// construct compressed buffer
		umint nHeaderSize = sizeof(uint32) + sizeof(uint8) + nNodes * gc_NodeSerializedSize;
		// Allocate header + worst-case bitstream (nSrcLen bytes) + sizeof(uint64) padding
		// for the 8-byte read-modify-write used when encoding bits
		umint nAllocSize = fg_GetCompressedAllocSize(nHeaderSize, nSrcLen);

		NContainer::CByteVector Destination;
		Destination.f_SetLen(nAllocSize);
		uint8 *pDes = Destination.f_GetArray();
		NMemory::fg_MemClear(pDes, nAllocSize);

		uint8 *pDesPtr = pDes;
		// save source buffer length as little-endian uint32
		{
			uint32 Temp = fg_ByteSwapLE((uint32)nSrcLen);
			NMemory::fg_MemCopy(pDesPtr, &Temp, sizeof(Temp));
		}
		pDesPtr += sizeof(uint32);
		// save Huffman tree leaves count-1 (as it may be 256)
		*pDesPtr = (uint8)(nNodes - 1);
		pDesPtr += sizeof(uint8);
		// save Huffman tree used leaves nodes
		for (umint iCount = 0; iCount < nNodes; iCount++)
		{
			uint32 Freq = fg_ByteSwapLE(Nodes[iCount].m_Frequency);
			NMemory::fg_MemCopy(pDesPtr, &Freq, sizeof(Freq));
			pDesPtr += sizeof(uint32);
			*pDesPtr = Nodes[iCount].m_ByAscii;
			pDesPtr += sizeof(uint8);
		}
		// sort nodes depending on ascii to can index nodes with its ascii value
		NMemory::fg_QSort(Nodes, 256, CSort_ASCIICompare());

		uint64 iDes = 0;
		// loop to write codes
		for (umint iCount = 0; iCount < nSrcLen; iCount++)
		{
			uint64 Temp;
			auto *pDestination = pDesPtr + umint(iDes >> 3);

			NMemory::fg_MemCopy(&Temp, pDestination, sizeof(Temp));
			Temp = fg_ByteSwapLE(Temp);
			Temp |= Nodes[pSrc[iCount]].m_Code << (iDes & 7);
			Temp = fg_ByteSwapLE(Temp);
			NMemory::fg_MemCopy(pDestination, &Temp, sizeof(Temp));

			iDes += Nodes[pSrc[iCount]].m_CodeLength;
		}
		// trim to actual compressed size
		umint nFinalSize = umint(uint64(pDesPtr - pDes) + ((iDes + 7) >> 3));
		Destination.f_SetLen(nFinalSize);

		return Destination;
	}

	NContainer::CByteVector fg_DecompressHuffman(NContainer::CByteVector const &_Source, umint _nMaxDestinationLen)
	{
		uint8 const *pSrc = _Source.f_GetArray();
		umint nSrcLen = _Source.f_GetLen();
		auto fThrowMalformed = []
			{
				DMibError("Malformed Huffman compressed data");
			}
		;
		auto fThrowTooLarge = []
			{
				DMibError("Huffman decompression output exceeds maximum buffer length");
			}
		;

		if (nSrcLen < sizeof(uint32))
			fThrowMalformed();

		// read destination final length as little-endian uint32
		uint32 nDesLenRaw;
		NMemory::fg_MemCopy(&nDesLenRaw, pSrc, sizeof(nDesLenRaw));
		umint nDesLen = fg_ByteSwapLE(nDesLenRaw);

		if (nDesLen == 0)
		{
			if (nSrcLen != sizeof(uint32))
				fThrowMalformed();
			return {};
		}

		if (nDesLen > _nMaxDestinationLen)
			fThrowTooLarge();

		if (nSrcLen < sizeof(uint32) + sizeof(uint8))
			fThrowMalformed();

		umint nNodes = *(pSrc + sizeof(uint32)) + 1;
		umint nHeaderSize = sizeof(uint32) + sizeof(uint8) + nNodes * gc_NodeSerializedSize;
		if (nSrcLen < nHeaderSize)
			fThrowMalformed();

		// initialize Huffman nodes with frequency and ascii
		CHuffmanNode Nodes[511];
		bool bSeenAscii[256] = {};
		umint iSrc = sizeof(uint32) + sizeof(uint8);
		uint64 nTotalFrequency = 0;
		for (umint iCount = 0; iCount < nNodes; iCount++)
		{
			uint32 Freq;
			NMemory::fg_MemCopy(&Freq, pSrc + iSrc, sizeof(Freq));
			auto &Node = Nodes[iCount];
			Node.m_Frequency = fg_ByteSwapLE(Freq);
			if (Node.m_Frequency == 0)
				fThrowMalformed();
			nTotalFrequency += Node.m_Frequency;
			if (nTotalFrequency > nDesLen)
				fThrowMalformed();
			iSrc += sizeof(uint32);

			Node.m_ByAscii = *(pSrc + iSrc);
			if (bSeenAscii[Node.m_ByAscii])
				fThrowMalformed();
			bSeenAscii[Node.m_ByAscii] = true;
			iSrc += sizeof(uint8);
		}
		if (nTotalFrequency != nDesLen)
			fThrowMalformed();
		// construct Huffman tree
		if (fg_GetHuffmanTree(Nodes, true) != nNodes)
			fThrowMalformed();
		uint64 nEncodedBits = fg_GetEncodedBitCount(Nodes, nNodes);
		uint64 nExpectedSrcBits = (uint64(iSrc) << 3) + nEncodedBits;
		uint64 nExpectedSrcLen = uint64(iSrc) + ((nEncodedBits + 7) >> 3);
		if (nExpectedSrcBits > (uint64(nSrcLen) << 3) || nExpectedSrcLen != nSrcLen)
			fThrowMalformed();
		if ((nEncodedBits & 7) != 0 && (pSrc[nSrcLen - 1] & uint8(TCLimitsInt<uint8>::mc_Max << (nEncodedBits & 7))) != 0)
			fThrowMalformed();
		// get Huffman tree root
		CHuffmanNode *pRoot = &Nodes[0];
		while (pRoot->m_pParent)
			pRoot = pRoot->m_pParent;

		NContainer::CByteVector Destination;
		Destination.f_SetLen(nDesLen);
		uint8 *pDes = Destination.f_GetArray();

		umint iDes = 0;
		CHuffmanNode *pNode;
		uint64 Code;
		uint64 nSrcBits = uint64(nSrcLen) << 3;
		uint64 iSrcBits = uint64(iSrc) << 3;
		while (iDes < nDesLen)
		{
			uint64 Temp = 0;
			umint iSrcByte = umint(iSrcBits >> 3);
			if (iSrcByte < nSrcLen)
				NMemory::fg_MemCopy(&Temp, pSrc + iSrcByte, fg_Min(sizeof(Temp), nSrcLen - iSrcByte));
			Temp = fg_ByteSwapLE(Temp);
			Code = Temp >> (iSrcBits & 7);
			pNode = pRoot;
			while (pNode->m_pLeft)	// if node has pLeft then it must has pRight
			{	// node not leaf
				if (iSrcBits >= nSrcBits)
					fThrowMalformed();
				pNode = (Code & 1) ? pNode->m_pRight : pNode->m_pLeft;
				Code >>= 1;
				iSrcBits++;
			}
			pDes[iDes++] = pNode->m_ByAscii;
		}
		if (iSrcBits != nExpectedSrcBits)
			fThrowMalformed();

		return Destination;
	}
}
