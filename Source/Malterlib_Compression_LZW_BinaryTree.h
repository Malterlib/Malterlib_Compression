// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>

namespace NMib
{
	namespace NDataProcessing
	{
		template <class t_CKey, class t_CData> 
		class TCLZWBinaryTreeNode
		{
		public:
			typedef TCLZWBinaryTreeNode<t_CKey, t_CData> CTreeNode;
			TCLZWBinaryTreeNode()
			{
				m_pParent = nullptr;
				m_pChildren[0] = nullptr;
				m_pChildren[1] = nullptr;
				m_Count = 0;
				m_Identifier = 0;
				m_pEqualIDs = nullptr;
			}
			~TCLZWBinaryTreeNode()
			{
				if(m_pEqualIDs)
					delete m_pEqualIDs;
			}
		public:
			CTreeNode *m_pParent;
			CTreeNode *m_pChildren[2];
			// node key
			t_CKey m_Key;
			// node data
			t_CData m_Data;
			// node repetition count
			aint m_Count;
			// node ID
			aint m_Identifier;
			// node repeated keys' IDs
			NContainer::TCVector<aint> *m_pEqualIDs;

			const CTreeNode &operator = (const CTreeNode &_Right)
			{
				m_Key = _Right.m_Key;
				m_Data = _Right.m_Data;
				m_Count = _Right.m_Count;
				m_Identifier = _Right.m_Identifier;
				if(_Right.m_pEqualIDs)
				{
					if(m_pEqualIDs == nullptr)
						m_pEqualIDs = new vector<int>;
					*m_pEqualIDs = *_Right.m_pEqualIDs;
				}
				return *this;
			}
		};

		template <class t_CKey, class t_CArgKey, class t_CData, class t_CArgData> 
		class TCLZWBinaryTree
		{
		public:
			TCLZWBinaryTree()
			{
				m_pRoot = nullptr;
				m_pNil = nullptr;
				m_Count = 0;
				m_Serial = 0;
				m_bModified = false;
				m_bModified = false;
			}
			~TCLZWBinaryTree()
			{
				f_RemoveAll();
			}
		public:
			// tree root node
			CTreeNode *m_pRoot;
			CTreeNode *m_pNil;
			// tree nodes count
			aint m_Count;
			aint m_Serial;
			// flag to indicate if the tree is modified or not
			bint m_bModified;
			// ignore repeated keys in the Add function
			bint m_bModified;

			// return tree nodes count
			inline aint f_GetCount() const	
			{	
				return m_Count;	
			}

			// check if the tree is empty or not
			inline bool f_IsEmpty() const	
			{	
				return m_Count == 0;	
			}

			// remove all tree nodes
			void f_RemoveAll()
			{
				CTreeNode *pNode = m_pRoot, *pTemp;
				while (pNode != m_pNil)
				{
					// check for  child
					if (pNode->m_pChildren[0] != m_pNil)
						pNode = pNode->m_pChildren[0];
					// check for right child
					else if (pNode->m_pChildren[1] != m_pNil)
						pNode = pNode->m_pChildren[1];
					else	// pNode has no children
					{	// save pNode pointer
						pTemp = pNode;
						// set pNode pointer at its parent to nullptr
						if(pNode->m_pParent != m_pNil)
							pNode->m_pParent->m_pChildren[pNode != pNode->m_pParent->m_pChildren[0]] = m_pNil;
						// update pointer pNode to its parent
						pNode = pNode->m_pParent;
						// delete the saved pNode
						delete pTemp;
					}
				}
				m_Count = m_Serial = 0;
				m_pRoot = m_pNil;
				m_bModified = false;
			}
			// insert key in the tree
			inline CTreeNode* f_Insert(t_CArgKey _Key, aint _ID = -1, CTreeNode* _pNode = nullptr)
			{
				if(m_pRoot == m_pNil)
				{
					m_pRoot = fp_NewNode();
					_pNode = m_pRoot;
				}
				else	
				{
					if(_pNode == nullptr)
						_pNode = m_pRoot;
					aint nResult;
					while(true)
					{
						nResult = _pNode->m_Key.f_Compare(_Key);
						if(nResult == 0)
						{
							_pNode->m_Count++;
							if(m_bModified == false)
							{
								if(_pNode->m_pEqualIDs == nullptr)
									_pNode->m_pEqualIDs = new NContainer::TCVector<aint>;
								_pNode->m_pEqualIDs->f_Insert(_ID == -1 ? m_Serial : _ID);
								m_Serial++;
								m_Count++;
							}
							return _pNode;
						}
						nResult = nResult > 0 ? 0 : 1;
						if(_pNode->m_pChildren[nResult] == m_pNil)
						{
							_pNode->m_pChildren[nResult] = fp_NewNode();
							_pNode->m_pChildren[nResult]->m_pParent = _pNode;
							_pNode = _pNode->m_pChildren[nResult];
							break;
						}
						_pNode = _pNode->m_pChildren[nResult];
					}	
				}
				_pNode->m_Key = _Key;
				_pNode->m_Identifier = _ID == -1 ? m_Serial : _ID;
				m_Serial++;
				m_Count++;
				_pNode->m_Count++;
				m_bModified = true;

				return _pNode;
			}	
			// search for a _Key in the tree
			inline CTreeNode* f_Search(t_CArgKey _Key, CTreeNode* _pNode = nullptr) const
			{
				if(_pNode == nullptr)
					_pNode = m_pRoot;
				aint nResult;
				while(_pNode != m_pNil && (nResult = _pNode->m_Key.f_Compare(_Key)) != 0)
					_pNode = _pNode->m_pChildren[nResult < 0];
				return _pNode == m_pNil ? nullptr : _pNode;
			}	
			// return minimum _Key in the tree
			CTreeNode* f_Min(CTreeNode* _pNode) const
			{	
				// iterate in the left branch
				while(_pNode != m_pNil && _pNode->m_pChildren[0] != m_pNil)
					_pNode = _pNode->m_pChildren[0];
				return _pNode;
			}
			// return maximum _Key in the tree
			CTreeNode* f_Max(CTreeNode* _pNode) const
			{	
				// iterate in the right branch
				while(_pNode != m_pNil && _pNode->m_pChildren[1] != m_pNil)
					_pNode = _pNode->m_pChildren[1];
				return _pNode;
			}
			// return _pNode successor
			CTreeNode* f_Successor(CTreeNode* _pNode) const
			{
				// return the left most _pNode in the right subtree
				if(_pNode->m_pChildren[1] != m_pNil)
					return f_Min(_pNode->m_pChildren[1]);
				// go up from _pNode until we find a _pNode that is the left of its parent
				CTreeNode* m_pParent = _pNode->m_pParent;
				while(m_pParent != m_pNil && _pNode == m_pParent->m_pChildren[1])
				{
					_pNode = m_pParent;
					m_pParent = _pNode->m_pParent;
				}
				return m_pParent;
			}
			// return _pNode predecessor
			CTreeNode* f_Predecessor(CTreeNode* _pNode) const
			{	
				// return the right most _pNode in the left subtree
				if(_pNode->m_pChildren[0] != m_pNil)
					return f_Max(_pNode->m_pChildren[0]);
				// go up from _pNode until we find a _pNode that is the right of its parent
				CTreeNode* m_pParent = _pNode->m_pParent;
				while(m_pParent != m_pNil && _pNode == m_pParent->m_pChildren[0])
				{
					_pNode = m_pParent;
					m_pParent = _pNode->m_pParent;
				}
				return m_pParent;
			}

			// delete _pNode
			// 1- _pNode has no child, remove it
			// 2- _pNode has one child, splice it (connect its parent and child)
			// 3- _pNode has two children, splice its successor and put it in its place
			void f_Delete(CTreeNode* _pNode)
			{	
				CTreeNode *pSplice = (_pNode->m_pChildren[0] == m_pNil || _pNode->m_pChildren[1] == m_pNil)?_pNode:f_Successor(_pNode);
				CTreeNode *pChild = pSplice->m_pChildren[pSplice->m_pChildren[0] == m_pNil];
				// connect child to spliced _pNode parent
				if(pChild != m_pNil)
					pChild->m_pParent = pSplice->m_pParent;
				// connect spliced _pNode parent to child
				if(pSplice->m_pParent == m_pNil)
					m_pRoot = pChild;
				else
					pSplice->m_pParent->m_pChildren[pSplice != pSplice->m_pParent->m_pChildren[0]] = pChild;
				// put spliced _pNode in place of _pNode (if required)
				if(pSplice != _pNode)
				{	// copy spliced _pNode
					*_pNode = *pSplice;
					// delete the spliced _pNode
					delete pSplice;
				}
				else
					// delete the _pNode
					delete _pNode;
				m_Count--;
			}

		protected:
			virtual CTreeNode* fp_NewNode()
			{
				return new CTreeNode();
			}
		};
	}
}