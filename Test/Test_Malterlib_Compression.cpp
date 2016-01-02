// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Compression/Huffman>

namespace
{
	class CCompression_Tests : public NMib::NTest::CTest
	{
	public:

		void f_DoTests()
		{
			DMibTestSuite(CTestCategory("Compression") << CTestGroup("Unfinished"))
			{
				// This causes memory overwrites
			NMib::NDataProcessing::TCCompress_Huffman<> compression;

			NMib::NMisc::CRandom Random;
			uint8 pTestBuffer[2048];
			for(aint i = 0; i < 2048; ++i)
			{
				pTestBuffer[i] = Random.f_Get()%256;
			}

			aint nDest;
			void *pDest;
			compression.f_CompressHuffman(pTestBuffer,2048,pDest,nDest);
			
			aint nDestUncomp;
			void *pDestUncomp;
			compression.f_DecompressHuffman(pDest,nDest,pDestUncomp,nDestUncomp);

			uint8 * pTestFinalBuffer = (uint8*)pDestUncomp;
			bint bDecompressDiffer = false;
			for (aint i = 0; i < 2048; ++i)
			{
				if (pTestFinalBuffer[i] != pTestBuffer[i])
					bDecompressDiffer = true;
			}

			DMibTest(DMibExpr(!bDecompressDiffer));

			NMib::NMem::fg_Free(pDest);
			NMib::NMem::fg_Free(pDestUncomp);
			};
		}


	#if 0
		class CChunk;

		class CDataEntry
		{
		public:
			class CSort0
			{
			public:
				inline_small void *operator () (CDataEntry const &_Node) const
				{
					return _Node.m_pData;
				}
			};
			class CSort1
			{
			public:
				inline_small void *operator () (CDataEntry const &_Node) const
				{
					aint Length = _Node.m_pChunk->m_Length;
					return (void *)((mint)_Node.m_pData + Length);
				}
			};

			void *m_pData;
			DMibIntrusiveLink(CDataEntry, NMib::NIntrusive::TCAVLLink<>, m_Link0);
			DMibIntrusiveLink(CDataEntry, NMib::NIntrusive::TCAVLLink<>, m_Link1);
			DMibListLinkDS_Link(CDataEntry, m_ListLink0);
			DMibListLinkDS_Link(CDataEntry, m_ListLink1);
			CChunk *m_pChunk;
		};

		class CDataEntryKey
		{
		public:
			void *m_pData;
			int m_Length;
		};

		class CChunk
		{
		public:
			CChunk()
			{
				m_nEntries = 0;
				m_Index = 0;
			}

			int m_Length;
			int m_Index;

			NMib::NIntrusive::TCAVLTree<CDataEntry::CLinkTraits_m_Link0, CDataEntry::CSort0> m_Entries0;
			NMib::NIntrusive::TCAVLTree<CDataEntry::CLinkTraits_m_Link1, CDataEntry::CSort1> m_Entries1;
			class CSort
			{
			public:
				inline_small bint operator () (CChunk const &_Left, CChunk const &_Right) const
				{
					if (_Left.m_Length < _Right.m_Length)
						return true;
					else if (_Left.m_Length > _Right.m_Length)
						return false;

					const CDataEntry *pFirst = _Left.m_Entries0.f_GetRoot();
					const CDataEntry *pSecond = _Right.m_Entries0.f_GetRoot();

					return NMib::NMem::fg_MemCmp((uint8 *)pFirst->m_pData, (uint8 *)pSecond->m_pData, _Left.m_Length) < 0;
				}
				inline_small bint operator () (CChunk const &_Left, const CDataEntryKey &_Right) const
				{
					if (_Left.m_Length < _Right.m_Length)
						return true;
					else if (_Left.m_Length > _Right.m_Length)
						return false;

					const CDataEntry *pFirst = _Left.m_Entries0.f_GetRoot();

					return NMib::NMem::fg_MemCmp((uint8 *)pFirst->m_pData, (uint8 *)_Right.m_pData, _Left.m_Length) < 0;
				}
				inline_small bint operator () (const CDataEntryKey &_Left, CChunk const &_Right) const
				{
					if (_Left.m_Length < _Right.m_Length)
						return true;
					else if (_Left.m_Length > _Right.m_Length)
						return false;

					const CDataEntry *pSecond = _Right.m_Entries0.f_GetRoot();

					return NMib::NMem::fg_MemCmp((uint8 *)_Left.m_pData, (uint8 *)pSecond->m_pData, _Left.m_Length) < 0;
				}
			};
			DMibIntrusiveLink(CChunk, NMib::NIntrusive::TCAVLLink<>, m_Link);
			int m_nEntries;
			DMibListLinkD_Link(CChunk, m_ListLink);
		};

		NIntrusive::TCAVLTree<CChunk::CLinkTraits_m_Link, CChunk::CSort> m_ChunkTree;
		int m_nChunks;
		int m_nEntries;

		DMibListLinkD_List(CChunk, m_ListLink) m_Chunks;
		DMibListLinkD_List(CChunk, m_ListLink) m_ChoosenChunks;

		class CSortBySavedBytes
		{
		public:
			typedef aint CRet;
			static inline_small CRet fs_Compare(void *_pContext, const CChunk *_pFirst, const CChunk *_pSecond)
			{
				aint nSavedBytesFirst = ((_pFirst->m_Length- 2) * (_pFirst->m_nEntries - 1))-2;
				aint nSavedBytesSecond = ((_pSecond->m_Length- 2) * (_pSecond->m_nEntries - 1))-2;
				
				aint Ret = nSavedBytesSecond - nSavedBytesFirst;

				if (!Ret)
				{
					return _pSecond->m_Length - _pFirst->m_Length;
				}

				return Ret;
			}

		};

		class CSortByAddress
		{
		public:
			typedef aint CRet;
			static inline_small CRet fs_Compare(void *_pContext, const CDataEntry *_pFirst, const CDataEntry *_pSecond)
			{
				return (aint)_pFirst->m_pData - (aint)_pSecond->m_pData;
			}

		};

		NMib::NMem::TCPool<CChunk, 16384> m_ChunkPool;
		NMib::NMem::TCPool<CDataEntry, 16384> m_DataEntryPool;

		NMib::NContainer::TCVector< DMibListLinkDS_List(CDataEntry, m_ListLink0) > m_lDataEntriesStart;
		NMib::NContainer::TCVector< DMibListLinkDS_List(CDataEntry, m_ListLink1) > m_lDataEntriesEnd;

		void f_DoTests()
		{
			m_nChunks = 0;
			m_nEntries = 0;
			DMibTrace("\n\nCompression\n\n", 0);
	#if 0

			NMib::NFile::CFile File;

			try 
			{
				if (NMib::NFile::CFile::fs_FileExists("Test.bin"))
					File.f_Open("Test.bin", NMib::NFile::EFileOpen_Read);
				else
				{
					DMibTrace("Test.bin not found\n", 0);
					return "";
				}
			}
			catch (NMib::NFile::CExceptionFile)
			{
				DMibTrace("Failed to open testfile\n", 0);
				return "";
			}

			int Len = File.f_GetLength();

			NMib::NContainer::TCVector<uint8> Data;

			Data.f_SetLen(Len);

			File.f_Read(Data.f_GetArray(), Len);

			uint8 *pData = Data.f_GetArray();

			m_lDataEntriesStart.f_SetLen(Len);
			m_lDataEntriesEnd.f_SetLen(Len);

			int MaxDataSize = 16;
			int MinDataSize = 3;

			// Find equal chunks

			int l;
	        for (l = MinDataSize; l < MaxDataSize; ++l)
			{
				bool FoundDoublet = false;
				for (int i = 0; i < Len; ++i)
				{
					if ((i + l) > Len)
						break;

					CDataEntryKey Entry;
					Entry.m_pData = pData + i;
					Entry.m_Length = l;
					CChunk *pChunk = m_ChunkTree.f_FindEqual(Entry);

					if (!pChunk)
					{
						CDataEntry *pEntry = m_DataEntryPool.New();
						pEntry->m_pData = Entry.m_pData;
						m_lDataEntriesStart[i].f_Insert(pEntry);
						m_lDataEntriesEnd[i+l-1].f_Insert(pEntry);

						pChunk = m_ChunkPool.New();
						pChunk->m_Length = Entry.m_Length;
						++m_nChunks;
						++m_nEntries;
						++pChunk->m_nEntries;
						pChunk->m_Entries0.f_Insert(pEntry);				
						pChunk->m_Entries1.f_Insert(pEntry);				
						m_ChunkTree.f_Insert(pChunk);				
						m_Chunks.f_Insert(pChunk);
						pEntry->m_pChunk = pChunk;
					}
					else
					{
						// Search for double find in same range
						CDataEntry *pEntry = pChunk->m_Entries1.f_FindSmallestGreaterThanEqual(Entry.m_pData);

						if (!pEntry || (mint)Entry.m_pData >= (mint)pEntry->m_pData + Entry.m_Length)
						{
							pEntry = pChunk->m_Entries0.f_FindLargestLessThanEqual((uint8 *)Entry.m_pData + Entry.m_Length);
							if (!pEntry || (mint)Entry.m_pData >= (mint)pEntry->m_pData + Entry.m_Length)
							{
								pEntry = m_DataEntryPool.New();
								pEntry->m_pData = Entry.m_pData;
								m_lDataEntriesStart[i].f_Insert(pEntry);
								m_lDataEntriesEnd[i+l-1].f_Insert(pEntry);
								++m_nEntries;
								++pChunk->m_nEntries;
								pChunk->m_Entries0.f_Insert(pEntry);
								pChunk->m_Entries1.f_Insert(pEntry);
								FoundDoublet = true;
								pEntry->m_pChunk = pChunk;
							}
						}
					}
				}

				if (!FoundDoublet)
					break;
			}

			DMibTrace("Chunks: {} Entries: {} Longest Dup {}\n", m_nChunks << m_nEntries << l);

			// Choose good chunks

			m_Chunks.f_MergeSort<CSortBySavedBytes>();

			DMibListLinkDS_List(CDataEntry, m_ListLink0) FinalEntryList;

			while (m_Chunks.f_GetFirst()) 
			{
				// Ok, now we find the best chunk, lets remove all entries that coincide with it
				m_Chunks.f_MergeSort<CSortBySavedBytes>();
				CChunk *pChunk = m_Chunks.f_GetFirst();

				// Trace out chunk
				if (0)
				{
					DMibTrace("Length: {} Entries: {} Data:", pChunk->m_Length << pChunk->m_nEntries);
					aint Length = pChunk->m_Length;

					ch8 *pDataDa = (ch8 *)pChunk->m_Entries0.f_GetRoot()->m_pData;
					for (int i = 0; i < Length; ++i)
					{
						if (pDataDa[i] < 32)
						{
							DMibTrace(".", 0);
						}
						else
						{
							ch8 Str[2];
							Str[0] = pDataDa[i];
							Str[1] = 0;
							DMibTrace("{}", Str);
						}
					}
					DMibTrace("\n", 0);
				}

				m_ChunkTree.f_Remove(pChunk);
				m_ChoosenChunks.f_Insert(pChunk);

				auto Iter = pChunk->m_Entries0.f_GetIterator();

				while (Iter)
				{
					int Start = (mint)Iter->m_pData - (mint)pData;
					int End = Start + pChunk->m_Length;

					for (int i = Start; i < End; ++i)
					{
						CDataEntry *pEntry = m_lDataEntriesStart[i].f_Pop();
						if (!pEntry)
							pEntry = m_lDataEntriesEnd[i].f_Pop();

						while (pEntry)
						{
							if (pEntry->m_pChunk != pChunk)
							{
								CChunk *pChunkRemove = pEntry->m_pChunk;
								if (pChunkRemove->m_nEntries == 1)
									m_ChunkTree.f_Remove(pChunkRemove);

								pEntry->m_pChunk->m_Entries0.f_Remove(pEntry);
								pEntry->m_pChunk->m_Entries1.f_Remove(pEntry);
								m_DataEntryPool.f_Delete(pEntry);
								--pChunkRemove->m_nEntries;
								--m_nEntries;

								if (pChunkRemove->m_nEntries == 0)
								{
									DMibSafeCheck(pChunkRemove->m_Entries0.f_IsEmpty(), "Must be empty");
									m_ChunkPool.f_Delete(pChunkRemove);
									--m_nChunks;
								}
							}
							else
							{
								pEntry->m_ListLink0.f_Unlink();
								pEntry->m_ListLink1.f_Unlink();
								FinalEntryList.f_Insert(pEntry);
							}

							pEntry = m_lDataEntriesStart[i].f_Pop();

							if (!pEntry)
								pEntry = m_lDataEntriesEnd[i].f_Pop();
						}
					}

					++Iter;
				}
			}

			

			DMibTrace("Chunks: {} Entries: {}\n", m_nChunks << m_nEntries);

			{
				NMib::NFile::TCBinaryStreamFile OutFile;
				OutFile.f_Open("OutFile.bin", NMib::NFile::EFileOpen_Write);
		
				uint32 nChunks = m_nChunks;
				OutFile << nChunks;

				DMibListLinkD_Iter(CChunk, m_ListLink) Iter = m_ChoosenChunks;
				int iCurrent = 0;

				while (Iter)
				{
					Iter->m_Index = iCurrent;
					++iCurrent;

					uint16 Length = Iter->m_Length;
					OutFile << Length;
					CDataEntry *pEntry = Iter->m_Entries0.f_GetRoot();
					OutFile.m_File.f_Write(pEntry->m_pData, Iter->m_Length);

					++Iter;
				}

				FinalEntryList.f_MergeSort<CSortByAddress>();

				CDataEntry *pEntry = FinalEntryList.f_Pop();

				while (pEntry)
				{
					DMibTrace("Start: {} End: {}\n", ((aint)pEntry->m_pData - (aint)pData) << ((aint)pEntry->m_pData - (aint)pData + pEntry->m_pChunk->m_Length));
					uint16 Index = pEntry->m_pChunk->m_Index;
					OutFile << Index;

					CChunk *pChunkRemove = pEntry->m_pChunk;

					pChunkRemove->m_Entries0.f_Remove(pEntry);
					pChunkRemove->m_Entries1.f_Remove(pEntry);
					m_DataEntryPool.f_Delete(pEntry);
					--pChunkRemove->m_nEntries;
					--m_nEntries;

					if (pChunkRemove->m_nEntries == 0)
					{
						DMibSafeCheck(pChunkRemove->m_Entries0.f_IsEmpty(), "Must be empty");
						m_ChunkPool.f_Delete(pChunkRemove);
						--m_nChunks;
					}

					pEntry = FinalEntryList.f_Pop();
				}
			}


			DMibTrace("Chunks: {} Entries: {}\n", m_nChunks << m_nEntries);

			{
				NMib::NFile::TCBinaryStreamFile OutFile;
				OutFile.f_Open("OutFile.bin", NMib::NFile::EFileOpen_Read);
				NMib::NFile::TCBinaryStreamFile DeCompFile;
				DeCompFile.f_Open("DecompFile.bin", NMib::NFile::EFileOpen_Write);
		
				uint32 nChunks;
				OutFile >> nChunks;

				NMib::NContainer::TCVector< NMib::NContainer::TCVector<uint8> > lChunks;

				lChunks.f_SetLen(nChunks);

				for (uint32 i = 0; i < nChunks; ++i)
				{
					uint16 Length;
					OutFile >> Length;
					lChunks[i].f_SetLen(Length);
					OutFile.m_File.f_Read(lChunks[i].f_GetArray(), Length);
				}

				while (!OutFile.m_File.f_IsAtEndOfFile())
				{
					uint16 Index;
					OutFile >> Index;
					DeCompFile.m_File.f_Write(lChunks[Index].f_GetArray(), lChunks[Index].f_GetLen());
				}
			}

			return ("");
	#endif
		}

	#endif

	};

	DMibTestRegister(CCompression_Tests, Malterlib::Compression);
}


