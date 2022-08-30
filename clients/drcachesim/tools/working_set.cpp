#include <algorithm>
#include <iomanip>
#include <iostream>
#include <vector>
#include <mutex>
#include "working_set.h"
#include "../common/utils.h"

const std::string working_set_t::TOOL_NAME = "Working set analysis tool";

analysis_tool_t *
working_set_tool_create(unsigned int line_size = 64, uint64_t working_set_reset_interval = 0,
                      unsigned int verbose = 0)
{
    if (working_set_reset_interval == 0) {
        working_set_reset_interval = working_set_t::default_working_set_reset_interval;
    }
    return new working_set_t(line_size, working_set_reset_interval, verbose);
}

working_set_t::working_set_t(unsigned int line_size, uint64_t working_set_reset_interval,
                         unsigned int verbose)
    : knob_line_size_(line_size)
    , knob_working_set_reset_interval_(working_set_reset_interval)
    , instruction_count_(0)
    , num_working_set_count_(0)
{
    line_size_bits_ = compute_log2((int)line_size);
    icache_map = new std::unordered_map<addr_t, uint64_t>();
    dcache_map = new std::unordered_map<addr_t, uint64_t>();
}

working_set_t::~working_set_t()
{
    delete icache_map;
    delete dcache_map;
}


static inline addr_t
back_align(addr_t addr, addr_t align)
{
    return addr & ~(align - 1);
}


bool
working_set_t::process_memref(const memref_t &memref)
{
    // similar to how histogram is handled, except we need to be single threaded for instruction count
    if (type_is_instr(memref.instr.type)) {
        instruction_count_++;
        num_working_set_count_++;
    }
    std::unordered_map<addr_t, uint64_t> *cache_map = nullptr;
    addr_t start_addr;
    size_t size;
    if (type_is_instr(memref.instr.type) ||
        memref.instr.type == TRACE_TYPE_PREFETCH_INSTR) {
        cache_map = icache_map;
        start_addr = memref.instr.addr;
        size = memref.instr.size;
    } else if (memref.data.type == TRACE_TYPE_READ ||
               memref.data.type == TRACE_TYPE_WRITE ||
               type_is_prefetch(memref.data.type)) {
        cache_map = dcache_map;
        start_addr = memref.instr.addr;
        size = memref.instr.size;
    } else {
        if (num_working_set_count_ >= knob_working_set_reset_interval_) {
            flush_working_set(instruction_count_);
        }
        return true;
    }
    for (addr_t addr = back_align(start_addr, knob_line_size_);
         addr < start_addr + size && addr < addr + knob_line_size_ /* overflow */;
         addr += knob_line_size_) {
        ++(*cache_map)[addr >> line_size_bits_];
    }
    if (num_working_set_count_ >= knob_working_set_reset_interval_) {
        flush_working_set(instruction_count_);
    }
    return true;
}


void
working_set_t::flush_working_set(const uint64_t instr_count)
{
    working_set_icache_hist_[instr_count] = icache_map->size();
    working_set_dcache_hist_[instr_count] = dcache_map->size();
    icache_map->clear();
    dcache_map->clear();
    num_working_set_count_ = 0;
}


bool
working_set_t::print_results()
{
    if (!working_set_icache_hist_.count(instruction_count_)){
        working_set_icache_hist_[instruction_count_] = icache_map->size();
    }
    if (!working_set_dcache_hist_.count(instruction_count_)){
        working_set_dcache_hist_[instruction_count_] = dcache_map->size();
    }
    std::cerr << "Working set:" << std::endl;
    std::cerr << "  Instructions:" << std::endl;
    for (auto it = working_set_icache_hist_.begin(); it != working_set_icache_hist_.end(); ++it) {
        std::cerr << "  " << std::setw(16) << std::left << (it->first)
                  << std::setw(18)<< std::right << it->second << std::endl;
    }
    std::cerr << "  Data:" << std::endl;
    for (auto it = working_set_dcache_hist_.begin(); it != working_set_dcache_hist_.end(); ++it) {
        std::cerr << "  " << std::setw(16) << std::left << (it->first)
                  << std::setw(18)<< std::right << it->second << std::endl;
    }
    return true;
}
