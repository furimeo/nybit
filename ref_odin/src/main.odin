// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package main

import "analysis"
import "core:fmt"
import "core:mem"
import "ir"
import "opt"
import "parser"

main :: proc() {
	track: mem.Tracking_Allocator
	mem.tracking_allocator_init(&track, context.allocator)
	defer mem.tracking_allocator_destroy(&track)
	context.allocator = mem.tracking_allocator(&track)

	{
		ctx: ir.Context
		ir.context_init(&ctx, "main_module")
		defer ir.context_destroy(&ctx)

		source_ir := `
// Nybit IR Example: add, abs, and opt_demo
@function add(%a: i32, %b: i32) -> i32;
.entry;
    %r = add %a, %b;
    @return %r;
;;

@function abs(%x: i32) -> i32;
.entry;
    %zero = const 0;
    %cond = cmp.lt.s %x, %zero;
    @branch_if %cond, .negative, .positive;

.negative;
    %r1 = neg %x;
    @return %r1;

.positive;
    @return %x;
;;

@function opt_demo(%x: i32) -> i32;
.entry;
    %c10 = const 10;
    %c20 = const 20;
    %sum = add %c10, %c20;
    %cond = const 1;
    %dead = mul %x, 999;
    @branch_if %cond, .live_br, .dead_br;

.live_br;
    %res = add %x, %sum;
    @branch .exit;

.dead_br;
    @return %dead;

.exit;
    @return %res;
;;
`

		p: parser.Parser
		parser.parser_init(&p, &ctx.module, source_ir)

		if !parser.parse_module(&p) {
			for d in p.diagnostics {
				fmt.eprintf("parse error [%d:%d]: %s\n", d.line, d.col, d.message)
			}
			return
		}

		diags := ir.validate_module(&ctx.module, context.temp_allocator)
		if len(diags) > 0 {
			for err in diags {
				fmt.eprintln("validation error:", err)
			}
			return
		}

		fmt.println("// --- RAW PARSED IR ---")
		dump_raw := ir.dump_module(&ctx.module, context.temp_allocator)
		fmt.print(dump_raw)

		// Run Optimization Pipeline
		opt.opt_run_module_pipeline(&ctx.module, .O1)

		diags_opt := ir.validate_module(&ctx.module, context.temp_allocator)
		if len(diags_opt) > 0 {
			for err in diags_opt {
				fmt.eprintln("post-opt validation error:", err)
			}
			return
		}

		fmt.println("\n// --- OPTIMIZED IR (-O1) ---")
		dump_opt := ir.dump_module(&ctx.module, context.temp_allocator)
		fmt.print(dump_opt)

		// Run Analysis Manager on all optimized functions
		for &fn in ctx.module.functions {
			am: analysis.Analysis_Manager
			analysis.analysis_manager_init(&am, &fn)
			defer analysis.analysis_manager_destroy(&am)

			cfg := analysis.analysis_get_cfg(&am)
			dt  := analysis.analysis_get_dom(&am)
			ud  := analysis.analysis_get_use_def(&am)
			liv := analysis.analysis_get_liveness(&am)

			fmt.printf("// Analysis for @%s: %d blocks (RPO: %v), %d uses tracked, %d live-in words\n",
				fn.name, len(cfg.rpo), cfg.rpo, len(ud.uses), liv.words_per_block)
			_ = dt
		}
	}

	for _, leak in track.allocation_map {
		fmt.eprintf("leak: %v bytes @ %v\n", leak.size, leak.location)
	}
}
