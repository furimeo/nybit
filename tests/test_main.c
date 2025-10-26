// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nybit/support.h>

int g_tests_run = 0;
int g_tests_failed = 0;

void test_module_lifecycle(void);
void test_build_add_function(void);
void test_cfg_edges(void);

void test_lexer_tokens(void);
void test_parse_add_canonical(void);
void test_parse_abs_control_flow(void);
void test_ir_golden_roundtrip(void);
void test_parser_diagnostics(void);

void test_dominance_same_block_violation(void);
void test_dominance_cross_block_violation(void);
void test_phi_invariants(void);
void test_identity_and_comparison_canonicalization(void);
void test_cfg_canonicalization_cleanup(void);
void test_canonicalization_determinism(void);

void test_analysis_cfg_linear(void);
void test_analysis_cfg_diamond_and_unreachable(void);
void test_analysis_cfg_loop(void);
void test_analysis_dominance_and_frontiers(void);
void test_analysis_use_def(void);
void test_analysis_liveness(void);
void test_analysis_manager_caching_and_invalidation(void);

void test_opt_const_fold(void);
void test_opt_sccp(void);
void test_opt_dce(void);
void test_opt_copy_prop(void);
void test_opt_gvn(void);
void test_opt_cfg_simplify(void);
void test_opt_pipeline_e2e(void);

void test_machine_ir_construction(void);
void test_machine_ir_lowering_arithmetic(void);
void test_machine_ir_lowering_control_flow(void);
void test_machine_ir_lowering_memory_and_stack(void);
void test_machine_ir_e2e_pipeline(void);

void test_x86_register_and_abi_properties(void);
void test_x86_stack_frame_and_addressing(void);
void test_x86_instruction_selection(void);
void test_x86_e2e_pipeline(void);

void test_regalloc_arithmetic_no_spill(void);
void test_regalloc_high_pressure_and_spill(void);
void test_regalloc_loops(void);
void test_regalloc_calls_and_callee_saved(void);
void test_regalloc_abi_sysv_vs_win64(void);
void test_regalloc_validation_pass(void);
void test_regalloc_e2e_pure_asm(void);

void test_encode_mov_instructions(void);
void test_encode_arithmetic_instructions(void);
void test_encode_branch_fixup_resolution(void);
void test_encode_module_and_relocations(void);
void test_encode_validation(void);
void test_encode_e2e_full_pipeline(void);

void test_object_buffer_growth_and_alignment(void);
void test_object_elf64_basic_structure(void);
void test_object_elf64_relocations(void);
void test_object_coff_basic_structure(void);
void test_object_coff_relocations_and_long_names(void);
void test_object_multi_function_and_relocations(void);
void test_object_bounds_and_error_validation(void);
void test_object_readelf_and_objdump_inspection(void);
void test_object_e2e_link_executable(void);
void test_object_e2e_internal_calls(void);
void test_object_e2e_external_calls(void);
void test_object_e2e_linker_diagnostics(void);

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    RUN_TEST(test_module_lifecycle);
    RUN_TEST(test_build_add_function);
    RUN_TEST(test_cfg_edges);

    RUN_TEST(test_lexer_tokens);
    RUN_TEST(test_parse_add_canonical);
    RUN_TEST(test_parse_abs_control_flow);
    RUN_TEST(test_ir_golden_roundtrip);
    RUN_TEST(test_parser_diagnostics);

    RUN_TEST(test_dominance_same_block_violation);
    RUN_TEST(test_dominance_cross_block_violation);
    RUN_TEST(test_phi_invariants);
    RUN_TEST(test_identity_and_comparison_canonicalization);
    RUN_TEST(test_cfg_canonicalization_cleanup);
    RUN_TEST(test_canonicalization_determinism);

    RUN_TEST(test_analysis_cfg_linear);
    RUN_TEST(test_analysis_cfg_diamond_and_unreachable);
    RUN_TEST(test_analysis_cfg_loop);
    RUN_TEST(test_analysis_dominance_and_frontiers);
    RUN_TEST(test_analysis_use_def);
    RUN_TEST(test_analysis_liveness);
    RUN_TEST(test_analysis_manager_caching_and_invalidation);

    RUN_TEST(test_opt_const_fold);
    RUN_TEST(test_opt_sccp);
    RUN_TEST(test_opt_dce);
    RUN_TEST(test_opt_copy_prop);
    RUN_TEST(test_opt_gvn);
    RUN_TEST(test_opt_cfg_simplify);
    RUN_TEST(test_opt_pipeline_e2e);

    RUN_TEST(test_machine_ir_construction);
    RUN_TEST(test_machine_ir_lowering_arithmetic);
    RUN_TEST(test_machine_ir_lowering_control_flow);
    RUN_TEST(test_machine_ir_lowering_memory_and_stack);
    RUN_TEST(test_machine_ir_e2e_pipeline);

    RUN_TEST(test_x86_register_and_abi_properties);
    RUN_TEST(test_x86_stack_frame_and_addressing);
    RUN_TEST(test_x86_instruction_selection);
    RUN_TEST(test_x86_e2e_pipeline);

    RUN_TEST(test_regalloc_arithmetic_no_spill);
    RUN_TEST(test_regalloc_high_pressure_and_spill);
    RUN_TEST(test_regalloc_loops);
    RUN_TEST(test_regalloc_calls_and_callee_saved);
    RUN_TEST(test_regalloc_abi_sysv_vs_win64);
    RUN_TEST(test_regalloc_validation_pass);
    RUN_TEST(test_regalloc_e2e_pure_asm);

    RUN_TEST(test_encode_mov_instructions);
    RUN_TEST(test_encode_arithmetic_instructions);
    RUN_TEST(test_encode_branch_fixup_resolution);
    RUN_TEST(test_encode_module_and_relocations);
    RUN_TEST(test_encode_validation);
    RUN_TEST(test_encode_e2e_full_pipeline);

    RUN_TEST(test_object_buffer_growth_and_alignment);
    RUN_TEST(test_object_elf64_basic_structure);
    RUN_TEST(test_object_elf64_relocations);
    RUN_TEST(test_object_coff_basic_structure);
    RUN_TEST(test_object_coff_relocations_and_long_names);
    RUN_TEST(test_object_multi_function_and_relocations);
    RUN_TEST(test_object_bounds_and_error_validation);
    RUN_TEST(test_object_readelf_and_objdump_inspection);
    RUN_TEST(test_object_e2e_link_executable);
    RUN_TEST(test_object_e2e_internal_calls);
    RUN_TEST(test_object_e2e_external_calls);
    RUN_TEST(test_object_e2e_linker_diagnostics);

    if (g_tests_failed > 0) {
        fprintf(stderr, "%d of %d tests failed\n", g_tests_failed, g_tests_run);
        return 1;
    }

    printf("all %d tests passed (0 bytes leaked)\n", g_tests_run);
    return 0;
}
