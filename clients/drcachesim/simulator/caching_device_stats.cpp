/* **********************************************************
 * Copyright (c) 2015-2020 Google, Inc.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of Google, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include <assert.h>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include "../common/options.h"
#include "caching_device_stats.h"

caching_device_stats_t::caching_device_stats_t(const std::string &miss_file, const std::string &addr2line_file,
                                               int block_size, bool warmup_enabled,
                                               bool is_coherent, bool record_instr_misses, bool record_working_set)
    : success_(true)
    , num_hits_(0)
    , num_misses_(0)
    , num_compulsory_misses_(0)
    , num_working_set_misses_(0)
    , num_child_hits_(0)
    , num_inclusive_invalidates_(0)
    , num_coherence_invalidates_(0)
    , num_hits_at_reset_(0)
    , num_misses_at_reset_(0)
    , num_child_hits_at_reset_(0)
    , warmup_enabled_(warmup_enabled)
    , is_coherent_(is_coherent)
    , access_count_(block_size)
    , working_set_access_count_(block_size)
    , record_instr_access_misses_(record_instr_misses)
    , record_working_set_(record_working_set)
    , addr2line_file_(addr2line_file)
    , file_(nullptr)
{
    if (miss_file.empty()) {
        dump_misses_ = false;
    } else {
#ifdef HAS_ZLIB
        file_ = gzopen(miss_file.c_str(), "w");
#else
        file_ = fopen(miss_file.c_str(), "w");
#endif
        if (file_ == nullptr) {
            dump_misses_ = false;
            success_ = false;
        } else
            dump_misses_ = true;
    }

    if (addr2line_file_.empty()) {
        map_to_line_ = false;
    } else {
        map_to_line_ = true;
    }

    stats_map_.emplace(metric_name_t::HITS, num_hits_);
    stats_map_.emplace(metric_name_t::MISSES, num_misses_);
    stats_map_.emplace(metric_name_t::HITS_AT_RESET, num_hits_at_reset_);
    stats_map_.emplace(metric_name_t::MISSES_AT_RESET, num_misses_at_reset_);
    stats_map_.emplace(metric_name_t::COMPULSORY_MISSES, num_compulsory_misses_);
    stats_map_.emplace(metric_name_t::CHILD_HITS_AT_RESET, num_child_hits_at_reset_);
    stats_map_.emplace(metric_name_t::CHILD_HITS, num_child_hits_);
    stats_map_.emplace(metric_name_t::INCLUSIVE_INVALIDATES, num_inclusive_invalidates_);
    stats_map_.emplace(metric_name_t::COHERENCE_INVALIDATES, num_coherence_invalidates_);

}

caching_device_stats_t::~caching_device_stats_t()
{
    if (file_ != nullptr) {
#ifdef HAS_ZLIB
        gzclose(file_);
#else
        fclose(file_);
#endif
    }
    for (auto &it : addr2line_map_) {
        delete it.second;
    }
}

void
caching_device_stats_t::access(const memref_t &memref, bool hit,
                               caching_device_block_t *cache_block)
{
    // We assume we're single-threaded.
    // We're only computing miss rate so we just inc counters here.
    if (hit)
        num_hits_++;
    else {
        num_misses_++;
        if (dump_misses_)
            dump_miss(memref);

        if (record_instr_access_misses_) {
            if (!type_is_instr(memref.data.type))
                ++ instr_access_hist_.access_hist[memref.data.pc];
        }

        check_compulsory_miss(memref.data.addr);
    }
    check_working_set(memref.data.addr);
}

void
caching_device_stats_t::child_access(const memref_t &memref, bool hit,
                                     caching_device_block_t *cache_block)
{
    if (hit)
        num_child_hits_++;
    // else being computed in access()
}

void
caching_device_stats_t::check_compulsory_miss(addr_t addr)
{
    auto lookup_pair = access_count_.lookup(addr);

    // If the address has never been accessed insert proper bound into access_count_
    // and count it as a compulsory miss.
    if (!lookup_pair.first) {
        num_compulsory_misses_++;
        access_count_.insert(addr, lookup_pair.second);
    }
}

void
caching_device_stats_t::check_working_set(addr_t addr)
{
    auto lookup_pair = working_set_access_count_.lookup(addr);

    if (!lookup_pair.first) {
        num_working_set_misses_++;
        working_set_access_count_.insert(addr, lookup_pair.second);
    }
}

void
caching_device_stats_t::flush_working_set(const memref_t &memref, const int_least64_t instr_count)
{
    if (working_set_hist_.count(instr_count))
        return;
    working_set_hist_[instr_count] = num_working_set_misses_;
    working_set_access_count_.clear();
    num_working_set_misses_ = 0;
}

void
caching_device_stats_t::dump_miss(const memref_t &memref)
{
    addr_t pc, addr;
    if (type_is_instr(memref.data.type))
        pc = memref.instr.addr;
    else { // data ref: others shouldn't get here
        assert(type_is_prefetch(memref.data.type) ||
               memref.data.type == TRACE_TYPE_READ ||
               memref.data.type == TRACE_TYPE_WRITE);
        pc = memref.data.pc;
    }
    addr = memref.data.addr;
#ifdef HAS_ZLIB
    gzprintf(file_, "0x%zx,0x%zx\n", pc, addr);
#else
    fprintf(file_, "0x%zx,0x%zx\n", pc, addr);
#endif
}

bool
comp(const std::pair<addr_t, uint64_t> &l, const std::pair<addr_t, uint64_t> &r)
{
    return l.second > r.second;
}

bool
caching_device_stats_t::read_csv(const std::string &file_name)
{
    std::ifstream file(file_name);
    if (!file.good()) {
        ERRMSG("Could not open file %s", file_name.c_str());
        return false;
    }
    csv_row_t row;
    int addr_index = -1;
    int symbol_index = -1;
    int path_index = -1;
    int line_index = -1;
    row.readNextRow(file);
    for (size_t i = 0; i < row.size(); i++) {
        if (row[i] == "addr")
            addr_index = i;
        if (row[i] == "symbol")
            symbol_index = i;
        if (row[i] == "path")
            path_index = i;
        if (row[i] == "line")
            line_index = i;
    }
    if (addr_index == -1 || symbol_index == -1 || path_index == -1 || line_index == -1) {
        ERRMSG("CSV file does not contain all required columns");
        file.close();
        return false;
    }
    row.readNextRow(file);
    while (!file.eof()) {
        debug_info_t* debug_info = new debug_info_t();
        debug_info->symbol = row[symbol_index];
        debug_info->path = row[path_index];
        debug_info->line = std::stoi(row[line_index]);
        addr2line_map_.emplace(reinterpret_cast<addr_t>(std::stoul(row[addr_index])), debug_info);
        row.readNextRow(file);
    };
    file.close();
    return true;
}

void
caching_device_stats_t::print_warmup(std::string prefix)
{
    std::cerr << prefix << std::setw(18) << std::left << "Warmup hits:" << std::setw(20)
              << std::right << num_hits_at_reset_ << std::endl;
    std::cerr << prefix << std::setw(18) << std::left << "Warmup misses:" << std::setw(20)
              << std::right << num_misses_at_reset_ << std::endl;
}

void
caching_device_stats_t::print_counts(std::string prefix)
{
    std::cerr << prefix << std::setw(18) << std::left << "Hits:" << std::setw(20)
              << std::right << num_hits_ << std::endl;
    std::cerr << prefix << std::setw(18) << std::left << "Misses:" << std::setw(20)
              << std::right << num_misses_ << std::endl;
    std::cerr << prefix << std::setw(18) << std::left
              << "Compulsory misses:" << std::setw(20) << std::right
              << num_compulsory_misses_ << std::endl;
    if (is_coherent_) {
        std::cerr << prefix << std::setw(21) << std::left
                  << "Parent invalidations:" << std::setw(17) << std::right
                  << num_inclusive_invalidates_ << std::endl;
        std::cerr << prefix << std::setw(20) << std::left
                  << "Write invalidations:" << std::setw(18) << std::right
                  << num_coherence_invalidates_ << std::endl;
    } else {
        std::cerr << prefix << std::setw(18) << std::left
                  << "Invalidations:" << std::setw(20) << std::right
                  << num_inclusive_invalidates_ << std::endl;
    }
}

void
caching_device_stats_t::print_rates(std::string prefix)
{
    if (num_hits_ + num_misses_ > 0) {
        std::string miss_label = "Miss rate:";
        if (num_child_hits_ != 0)
            miss_label = "Local miss rate:";
        std::cerr << prefix << std::setw(18) << std::left << miss_label << std::setw(20)
                  << std::fixed << std::setprecision(2) << std::right
                  << ((float)num_misses_ * 100 / (num_hits_ + num_misses_)) << "%"
                  << std::endl;
    }
}

void
caching_device_stats_t::print_child_stats(std::string prefix)
{
    if (num_child_hits_ != 0) {
        std::cerr << prefix << std::setw(18) << std::left
                  << "Child hits:" << std::setw(20) << std::right << num_child_hits_
                  << std::endl;
        std::cerr << prefix << std::setw(18) << std::left
                  << "Total miss rate:" << std::setw(20) << std::fixed
                  << std::setprecision(2) << std::right
                  << ((float)num_misses_ * 100 /
                      (num_hits_ + num_child_hits_ + num_misses_))
                  << "%" << std::endl;
    }
}

void
caching_device_stats_t::print_stats(std::string prefix, const int_least64_t instr_count)
{
    std::cerr.imbue(std::locale("")); // Add commas, at least for my locale
    if (warmup_enabled_) {
        print_warmup(prefix);
    }
    print_counts(prefix);
    print_rates(prefix);
    print_child_stats(prefix);
    std::cerr.imbue(std::locale("C")); // Reset to avoid affecting later prints.
    if (record_instr_access_misses_){
        print_miss_hist(prefix);
    }
    if (record_working_set_){
        print_working_set(prefix, instr_count);
    }
}

void
caching_device_stats_t::print_miss_hist(std::string prefix, int report_top)
{
    if (map_to_line_){
        if(!read_csv(addr2line_file_)){
            map_to_line_ = false;
        }
    }
    std::cerr << prefix << "Top data instr misses:" << std::endl;;
        std::vector<std::pair<addr_t, uint64_t>> top(report_top);
        std::partial_sort_copy(instr_access_hist_.access_hist.begin(), instr_access_hist_.access_hist.end(),
                              top.begin(), top.end(), comp);
        for (std::vector<std::pair<addr_t, uint64_t>>::iterator it = top.begin();
            it != top.end(); ++it) {
            std::cerr << prefix << "  " << std::setw(16) << std::hex << std::showbase << std::left << (it->first)
                    << std::setw(18) << std::dec << std::right << it->second << std::endl;
            if (map_to_line_){
                auto it2 = addr2line_map_.find(it->first);
                if (it2 != addr2line_map_.end()){
                    std::cerr << prefix << "    " << it2->second->path << ":" << it2->second->line << " " << it2->second->symbol << std::endl;
                }
            }
        }
        // Reset the i/o format for subsequent tool invocations.
        std::cerr << std::dec;
}

void
caching_device_stats_t::print_working_set(std::string prefix, const int_least64_t instr_count)
{
    if (!working_set_hist_.count(instr_count)){
        working_set_hist_[instr_count] = num_working_set_misses_;
    }
    std::cerr << prefix << "Working set:" << std::endl;
    for (auto it = working_set_hist_.begin(); it != working_set_hist_.end(); ++it) {
        std::cerr << prefix << "  " << std::setw(16) << std::left << (it->first)
                  << std::setw(18)<< std::right << it->second << std::endl;
    }
}

void
caching_device_stats_t::reset()
{
    num_hits_at_reset_ = num_hits_;
    num_misses_at_reset_ = num_misses_;
    num_child_hits_at_reset_ = num_child_hits_;
    num_hits_ = 0;
    num_misses_ = 0;
    num_compulsory_misses_ = 0;
    num_child_hits_ = 0;
    num_inclusive_invalidates_ = 0;
    num_coherence_invalidates_ = 0;
}

void
caching_device_stats_t::invalidate(invalidation_type_t invalidation_type)
{
    if (invalidation_type == INVALIDATION_INCLUSIVE) {
        num_inclusive_invalidates_++;
    } else if (invalidation_type == INVALIDATION_COHERENCE) {
        num_coherence_invalidates_++;
    }
}
