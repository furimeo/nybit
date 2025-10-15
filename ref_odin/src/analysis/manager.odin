// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package analysis

import "../ir"
import "core:mem"

Analysis_Manager :: struct {
	fn:             ^ir.Function,
	cfg:            CFG_Info,
	dom:            Dominator_Tree,
	use_def:        Use_Def,
	liveness:       Liveness_Info,

	valid_cfg:      bool,
	valid_dom:      bool,
	valid_use_def:  bool,
	valid_liveness: bool,

	allocator:      mem.Allocator,
}

analysis_manager_init :: proc(am: ^Analysis_Manager, fn: ^ir.Function, allocator := context.allocator) {
	am^ = {
		fn        = fn,
		allocator = allocator,
	}
}

analysis_manager_destroy :: proc(am: ^Analysis_Manager) {
	analysis_invalidate_all(am)
	am^ = {}
}

analysis_manager_set_function :: proc(am: ^Analysis_Manager, fn: ^ir.Function) {
	analysis_invalidate_all(am)
	am.fn = fn
}

analysis_invalidate_all :: proc(am: ^Analysis_Manager) {
	if am.valid_cfg {
		cfg_info_destroy(&am.cfg)
		am.valid_cfg = false
	}
	if am.valid_dom {
		dominator_tree_destroy(&am.dom)
		am.valid_dom = false
	}
	if am.valid_use_def {
		use_def_destroy(&am.use_def)
		am.valid_use_def = false
	}
	if am.valid_liveness {
		liveness_destroy(&am.liveness)
		am.valid_liveness = false
	}
}

analysis_invalidate_cfg :: proc(am: ^Analysis_Manager) {
	if am.valid_cfg {
		cfg_info_destroy(&am.cfg)
		am.valid_cfg = false
	}
	if am.valid_dom {
		dominator_tree_destroy(&am.dom)
		am.valid_dom = false
	}
	if am.valid_liveness {
		liveness_destroy(&am.liveness)
		am.valid_liveness = false
	}
}

analysis_invalidate_dom :: proc(am: ^Analysis_Manager) {
	if am.valid_dom {
		dominator_tree_destroy(&am.dom)
		am.valid_dom = false
	}
}

analysis_invalidate_use_def :: proc(am: ^Analysis_Manager) {
	if am.valid_use_def {
		use_def_destroy(&am.use_def)
		am.valid_use_def = false
	}
	if am.valid_liveness {
		liveness_destroy(&am.liveness)
		am.valid_liveness = false
	}
}

analysis_invalidate_liveness :: proc(am: ^Analysis_Manager) {
	if am.valid_liveness {
		liveness_destroy(&am.liveness)
		am.valid_liveness = false
	}
}

analysis_get_cfg :: proc(am: ^Analysis_Manager) -> ^CFG_Info {
	if !am.valid_cfg {
		cfg_info_init(&am.cfg, am.fn, am.allocator)
		am.valid_cfg = true
	}
	return &am.cfg
}

analysis_get_dom :: proc(am: ^Analysis_Manager) -> ^Dominator_Tree {
	if !am.valid_dom {
		dominator_tree_init(&am.dom, am.fn, am.allocator)
		am.valid_dom = true
	}
	return &am.dom
}

analysis_get_use_def :: proc(am: ^Analysis_Manager) -> ^Use_Def {
	if !am.valid_use_def {
		use_def_init(&am.use_def, am.fn, am.allocator)
		am.valid_use_def = true
	}
	return &am.use_def
}

analysis_get_liveness :: proc(am: ^Analysis_Manager) -> ^Liveness_Info {
	if !am.valid_liveness {
		liveness_init(&am.liveness, am.fn, am.allocator)
		am.valid_liveness = true
	}
	return &am.liveness
}
