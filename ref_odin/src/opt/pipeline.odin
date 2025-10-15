// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package opt

import "../analysis"
import "../ir"

Optimization_Level :: enum u8 {
	O0 = 0,
	O1,
	O2,
}

opt_run_pipeline :: proc(mod: ^ir.Module, fn: ^ir.Function, level := Optimization_Level.O1) -> bool {
	if len(fn.blocks) == 0 do return false

	if level == .O0 {
		return canonicalize_function(mod, fn)
	}

	am: analysis.Analysis_Manager
	analysis.analysis_manager_init(&am, fn, context.temp_allocator)
	defer analysis.analysis_manager_destroy(&am)

	changed_any := false
	max_iters := 1
	if level == .O2 {
		max_iters = 4
	}

	for iter := 0; iter < max_iters; iter += 1 {
		changed_iter := false

		// 1. Canonicalize
		if canonicalize_function(mod, fn) {
			changed_iter = true
			analysis.analysis_invalidate_cfg(&am)
		}

		// 2. CFG Simplify
		if opt_pass_cfg_simplify(mod, fn, &am) {
			changed_iter = true
		}

		// 3. SCCP
		if opt_pass_sccp(mod, fn, &am) {
			changed_iter = true
		}

		// 4. DCE
		if opt_pass_dce(mod, fn, &am) {
			changed_iter = true
		}

		// 5. Copy Propagation
		if opt_pass_copy_prop(mod, fn, &am) {
			changed_iter = true
		}

		// 6. Constant Folding
		if opt_pass_const_fold(mod, fn, &am) {
			changed_iter = true
		}

		// 7. GVN
		if opt_pass_gvn(mod, fn, &am) {
			changed_iter = true
		}

		// 8. CFG Simplify (final cleanup)
		if opt_pass_cfg_simplify(mod, fn, &am) {
			changed_iter = true
		}

		if !changed_iter do break
		changed_any = true
	}

	return changed_any
}

opt_run_module_pipeline :: proc(mod: ^ir.Module, level := Optimization_Level.O1) -> bool {
	changed := false
	for &fn in mod.functions {
		if opt_run_pipeline(mod, &fn, level) {
			changed = true
		}
	}
	return changed
}
