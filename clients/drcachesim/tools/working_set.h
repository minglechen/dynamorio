
#ifndef _WORKING_SET_H_
#define _WORKING_SET_H_ 1
#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <stdint.h>
#include <limits>
#include "analysis_tool.h"
#include "memref.h"


class working_set_t : public analysis_tool_t {
public:
    working_set_t(unsigned int line_size, uint64_t working_set_reset_interval, unsigned int verbose);
    virtual ~working_set_t();
    bool
    process_memref(const memref_t &memref) override;
    bool
    print_results() override;

    static const uint64_t default_working_set_reset_interval = 100000000;
protected:
    void
    flush_working_set(uint64_t instruction_count);
    std::unordered_map<addr_t, uint64_t>* icache_map;
    std::unordered_map<addr_t, uint64_t>* dcache_map;
    std::string error;

    unsigned int knob_line_size_;
    uint64_t knob_working_set_reset_interval_;
    uint64_t instruction_count_;
    uint64_t num_working_set_count_;
    size_t line_size_bits_;
    static const std::string TOOL_NAME;
    std::map<uint64_t, uint64_t> working_set_icache_hist_;
    std::map<uint64_t, uint64_t> working_set_dcache_hist_;
};


#endif // _WORKING_SET_H_