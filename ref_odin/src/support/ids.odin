// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package support

Value_ID    :: distinct u32
Inst_ID     :: distinct u32
Block_ID    :: distinct u32
Function_ID :: distinct u32
Symbol_ID   :: distinct u32
Type_ID     :: distinct u32
Reg_ID      :: distinct u32
Slot_ID     :: distinct u32

INVALID_VALUE    :: Value_ID(max(u32))
INVALID_INST     :: Inst_ID(max(u32))
INVALID_BLOCK    :: Block_ID(max(u32))
INVALID_FUNCTION :: Function_ID(max(u32))
INVALID_SYMBOL   :: Symbol_ID(max(u32))
INVALID_TYPE     :: Type_ID(max(u32))
INVALID_REG      :: Reg_ID(max(u32))
INVALID_SLOT     :: Slot_ID(max(u32))
