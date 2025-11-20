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
void test_aggregate_type_layout(void);
void test_aggregate_abi_classification(void);

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
void test_object_globals_rodata_data_bss(void);
void test_object_e2e_globals_execution(void);
void test_object_e2e_abi_stack_arguments(void);
void test_object_e2e_abi_scalar_widths(void);
void test_object_e2e_abi_fp_and_mixed(void);
void test_object_e2e_aggregate_values(void);
void test_object_e2e_aggregate_abi(void);
void test_target_unsupported_types(void);

void test_nygen_compile_raw_ir(void);
void test_nygen_compile_opt_ir(void);
void test_nygen_compile_machine_ir(void);
void test_nygen_compile_asm(void);
void test_nygen_compile_bytes(void);
void test_nygen_compile_object_elf_and_coff(void);
void test_nygen_diagnostics_on_error(void);
void test_nygen_compile_executable(void);
void test_nygen_cli_integration_e2e(void);
void test_benchmark_medium_repeat(void);

void test_jit_simple_arithmetic(void);
void test_jit_multi_function_internal_calls(void);
void test_jit_scalar_arguments_and_return(void);
void test_jit_globals_read_write(void);
void test_jit_external_host_function(void);
void test_jit_lifecycle_and_zero_leak(void);
void test_jit_unresolved_symbol_diagnostic(void);
void test_jit_nygen_standalone_encoded(void);
void test_jit_nygen_to_nyjit_execute(void);
void test_jit_nygen_compile_error_diagnostic(void);

void test_nyir_roundtrip_simple(void);
void test_nyir_roundtrip_multi_fn(void);
void test_nyir_roundtrip_globals(void);
void test_nyir_roundtrip_branch(void);
void test_nyir_deterministic_output(void);
void test_nyir_backend_matches_direct(void);
void test_nyir_corrupt_truncated(void);
void test_nyir_corrupt_bad_magic(void);
void test_nyir_corrupt_bad_version(void);
void test_nyir_corrupt_bad_opcode(void);
void test_nyir_corrupt_trailing_data(void);
void test_nyir_nygen_standalone(void);

void test_debug_source_loc_propagation(void);
void test_debug_dwarf_elf_emission(void);
void test_debug_coff_codeview_emission(void);
void test_unwind_disabled_flag(void);

void test_nylink_context_lifecycle(void);
void test_nylink_single_elf_loading(void);
void test_nylink_single_coff_loading(void);
void test_nylink_multi_object_relocations(void);
void test_nylink_duplicate_symbol_error(void);
void test_nylink_undefined_symbol_error(void);
void test_nylink_malformed_objects(void);
void test_nylink_section_layout_and_symbol_vas(void);
void test_nylink_relocation_application(void);
void test_nylink_relocation_pc32_overflow(void);
void test_nylink_elf64_executable_emission(void);
void test_nylink_pe_executable_emission(void);
void test_nylink_deterministic_emission(void);
void test_nylink_negative_validation_cases(void);
void test_nylink_e2e_multi_object_execution(void);
void test_nylink_archive_single_member_extraction(void);
void test_nylink_archive_unused_members(void);
void test_nylink_archive_chained_dependencies(void);
void test_nylink_archive_cyclic_dependencies(void);
void test_nylink_archive_malformed_and_bounds(void);
void test_nylink_archive_e2e_execution(void);
void test_cli_link_basic_objects(void);
void test_cli_link_with_archive_lazy_extraction(void);
void test_cli_link_search_path_and_library(void);
void test_cli_link_options_entry_base_target(void);
void test_cli_link_error_handling_and_cleanup(void);
void test_cli_link_determinism(void);
void test_cli_link_e2e_execution(void);
void test_nylink_elf64_shared_emission(void);
void test_cli_link_shared_options(void);
void test_nylink_pie_executable_emission(void);
void test_nylink_copy_reloc_rejection(void);
void test_cli_link_pie_options(void);
void test_nylink_gotpcrel_relocation(void);

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
    RUN_TEST(test_aggregate_type_layout);
    RUN_TEST(test_aggregate_abi_classification);

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
    RUN_TEST(test_object_globals_rodata_data_bss);
    RUN_TEST(test_object_e2e_globals_execution);
    RUN_TEST(test_object_e2e_abi_stack_arguments);
    RUN_TEST(test_object_e2e_abi_scalar_widths);
    RUN_TEST(test_object_e2e_abi_fp_and_mixed);
    RUN_TEST(test_object_e2e_aggregate_values);
    RUN_TEST(test_object_e2e_aggregate_abi);
    RUN_TEST(test_target_unsupported_types);

    RUN_TEST(test_nygen_compile_raw_ir);
    RUN_TEST(test_nygen_compile_opt_ir);
    RUN_TEST(test_nygen_compile_machine_ir);
    RUN_TEST(test_nygen_compile_asm);
    RUN_TEST(test_nygen_compile_bytes);
    RUN_TEST(test_nygen_compile_object_elf_and_coff);
    RUN_TEST(test_nygen_diagnostics_on_error);
    RUN_TEST(test_nygen_compile_executable);
    RUN_TEST(test_nygen_cli_integration_e2e);
    RUN_TEST(test_benchmark_medium_repeat);

    RUN_TEST(test_jit_simple_arithmetic);
    RUN_TEST(test_jit_multi_function_internal_calls);
    RUN_TEST(test_jit_scalar_arguments_and_return);
    RUN_TEST(test_jit_globals_read_write);
    RUN_TEST(test_jit_external_host_function);
    RUN_TEST(test_jit_lifecycle_and_zero_leak);
    RUN_TEST(test_jit_unresolved_symbol_diagnostic);
    RUN_TEST(test_jit_nygen_standalone_encoded);
    RUN_TEST(test_jit_nygen_to_nyjit_execute);
    RUN_TEST(test_jit_nygen_compile_error_diagnostic);

    RUN_TEST(test_nyir_roundtrip_simple);
    RUN_TEST(test_nyir_roundtrip_multi_fn);
    RUN_TEST(test_nyir_roundtrip_globals);
    RUN_TEST(test_nyir_roundtrip_branch);
    RUN_TEST(test_nyir_deterministic_output);
    RUN_TEST(test_nyir_backend_matches_direct);
    RUN_TEST(test_nyir_corrupt_truncated);
    RUN_TEST(test_nyir_corrupt_bad_magic);
    RUN_TEST(test_nyir_corrupt_bad_version);
    RUN_TEST(test_nyir_corrupt_bad_opcode);
    RUN_TEST(test_nyir_corrupt_trailing_data);
    RUN_TEST(test_nyir_nygen_standalone);

    RUN_TEST(test_debug_source_loc_propagation);
    RUN_TEST(test_debug_dwarf_elf_emission);
    RUN_TEST(test_debug_coff_codeview_emission);
    RUN_TEST(test_unwind_disabled_flag);

    RUN_TEST(test_nylink_context_lifecycle);
    RUN_TEST(test_nylink_single_elf_loading);
    RUN_TEST(test_nylink_single_coff_loading);
    RUN_TEST(test_nylink_multi_object_relocations);
    RUN_TEST(test_nylink_duplicate_symbol_error);
    RUN_TEST(test_nylink_undefined_symbol_error);
    RUN_TEST(test_nylink_malformed_objects);
    RUN_TEST(test_nylink_section_layout_and_symbol_vas);
    RUN_TEST(test_nylink_relocation_application);
    RUN_TEST(test_nylink_relocation_pc32_overflow);
    RUN_TEST(test_nylink_elf64_executable_emission);
    RUN_TEST(test_nylink_pe_executable_emission);
    RUN_TEST(test_nylink_deterministic_emission);
    RUN_TEST(test_nylink_negative_validation_cases);
    RUN_TEST(test_nylink_e2e_multi_object_execution);
    RUN_TEST(test_nylink_archive_single_member_extraction);
    RUN_TEST(test_nylink_archive_unused_members);
    RUN_TEST(test_nylink_archive_chained_dependencies);
    RUN_TEST(test_nylink_archive_cyclic_dependencies);
    RUN_TEST(test_nylink_archive_malformed_and_bounds);
    RUN_TEST(test_nylink_archive_e2e_execution);
    RUN_TEST(test_cli_link_basic_objects);
    RUN_TEST(test_cli_link_with_archive_lazy_extraction);
    RUN_TEST(test_cli_link_search_path_and_library);
    RUN_TEST(test_cli_link_options_entry_base_target);
    RUN_TEST(test_cli_link_error_handling_and_cleanup);
    RUN_TEST(test_cli_link_determinism);
    RUN_TEST(test_cli_link_e2e_execution);
    RUN_TEST(test_nylink_elf64_shared_emission);
    RUN_TEST(test_cli_link_shared_options);
    RUN_TEST(test_nylink_pie_executable_emission);
    RUN_TEST(test_nylink_copy_reloc_rejection);
    RUN_TEST(test_cli_link_pie_options);
    RUN_TEST(test_nylink_gotpcrel_relocation);

    if (g_tests_failed > 0) {
        fprintf(stderr, "%d of %d tests failed\n", g_tests_failed, g_tests_run);
        return 1;
    }

    printf("all %d tests passed (0 bytes leaked)\n", g_tests_run);
    return 0;
}
