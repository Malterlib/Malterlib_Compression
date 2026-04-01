// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>

struct CInitAllowInvalid
{
	CInitAllowInvalid()
	{
		NMib::NSys::fg_Process_AllowInvalidExit(true);
	}
};

static CInitAllowInvalid g_InitAllowInvalid;
