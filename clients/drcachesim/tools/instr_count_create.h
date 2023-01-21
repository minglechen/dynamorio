
#ifndef _INSTR_COUNT_CREATE_H_
#define _INSTR_COUNT_CREATE_H_ 1

#include "analysis_tool.h"

/**
 * @file drmemtrace/instr_count_create.h
 * @brief DrMemtrace tool that computes the most-referenced instructions.
 */

/**
 * Creates an analysis tool which computes the most-referenced cache lines.
 * The options are currently documented in \ref sec_drcachesim_ops.
 */
// These options are currently documented in ../common/options.cpp.
analysis_tool_t *
instr_count_tool_create(const std::string &addr2line_path, const std::string &output_dir,
                        unsigned int report_top = 10, unsigned int verbose = 0);

#endif /* _INSTR_COUNT_CREATE_H_ */