
/* histogram tool creation */

#ifndef _WORKING_SET_CREATE_H_
#define _WORKING_SET_CREATE_H_ 1

#include "analysis_tool.h"

/**
 * @file drmemtrace/histogram_create.h
 * @brief DrMemtrace tool that computes the most-referenced cache lines.
 */

/**
 * Creates an analysis tool which computes the most-referenced cache lines.
 * The options are currently documented in \ref sec_drcachesim_ops.
 */
// These options are currently documented in ../common/options.cpp.
analysis_tool_t *
working_set_tool_create(unsigned int line_size = 64, unsigned long working_set_reset_interval = 0, unsigned int verbose = 0);


#endif /* _WORKING_SET_CREATE_H_ */
