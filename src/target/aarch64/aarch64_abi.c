// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/target_aarch64.h"
#include "nybit/ir.h"

/*
 * AAPCS64 aggregate classification (ARM IHI 0059, sections 5.4.2 and 6.1.2).
 *
 * HFA (Homogeneous Floating-point Aggregate): a composite whose leaf
 * members are all the same floating-point type (f32 or f64), with at most
 * 4 members.  Passed/returned in V0-V3.
 *
 * Non-HFA composites:
 *   size == 0  -> ignored (no argument, no return register)
 *   size <= 16 -> passed/returned in 1 or 2 GPRs, packed as 8-byte chunks
 *   size > 16  -> passed by pointer (X8 for return / sret; caller-allocated
 *                 copy for arguments)
 *
 * Arrays of a uniform float element are also HFAs.
 */

static bool classify_hfa_recursive(const Ny_Type_Table *tt, Ny_Type_ID ty,
                                   Ny_Type_ID *out_member, uint32_t *out_count) {
    const Ny_Type *t = ny_type_get(tt, ty);
    if (!t) return false;

    if (t->kind == NY_TYPE_KIND_PRIMITIVE || t->kind == NY_TYPE_KIND_POINTER) {
        if (!ny_type_is_float(ty)) return false;
        if (*out_member == NY_INVALID_TYPE) {
            *out_member = ty;
        } else if (*out_member != ty) {
            return false;
        }
        (*out_count)++;
        return *out_count <= 4;
    }

    if (t->kind == NY_TYPE_KIND_ARRAY) {
        for (uint32_t i = 0; i < t->array_count; i++) {
            if (!classify_hfa_recursive(tt, t->elem_type, out_member, out_count))
                return false;
        }
        return *out_count <= 4;
    }

    if (t->kind == NY_TYPE_KIND_STRUCT) {
        for (size_t i = 0; i < t->field_count; i++) {
            if (!classify_hfa_recursive(tt, t->field_types[i], out_member, out_count))
                return false;
        }
        return *out_count <= 4;
    }

    return false;
}

Ny_AAPCS64_ABI aarch64_abi_classify_aggregate(const Ny_Type_Table *tt, Ny_Type_ID ty) {
    Ny_AAPCS64_ABI r = {0};
    r.hfa_member = NY_INVALID_TYPE;

    if (!ny_type_is_aggregate(tt, ty)) {
        r.kind = NY_AAPCS64_SCALAR;
        return r;
    }

    r.size = ny_type_size(tt, ty);

    if (r.size == 0) {
        r.kind = NY_AAPCS64_SCALAR;
        return r;
    }

    Ny_Type_ID hfa_member = NY_INVALID_TYPE;
    uint32_t hfa_count = 0;
    if (classify_hfa_recursive(tt, ty, &hfa_member, &hfa_count) && hfa_count > 0) {
        r.kind = NY_AAPCS64_HFA;
        r.reg_count = (uint8_t)hfa_count;
        r.hfa_member = hfa_member;
        return r;
    }

    if (r.size <= 16) {
        r.kind = NY_AAPCS64_GPR_AGG;
        r.reg_count = (uint8_t)((r.size + 7) / 8);
        if (r.reg_count == 0) r.reg_count = 1;
        return r;
    }

    r.kind = NY_AAPCS64_INDIRECT;
    return r;
}
