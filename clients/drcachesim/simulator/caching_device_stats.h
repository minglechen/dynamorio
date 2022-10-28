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

/* caching_device_stats: represents a hardware caching device.
 */

#ifndef _CACHING_DEVICE_STATS_H_
#define _CACHING_DEVICE_STATS_H_ 1

#include "caching_device_block.h"
#include <string>
#include <iterator>
#include <fstream>
#include <map>
#include <unordered_map>
#include <vector>
#include <stdint.h>
#include <limits>
#ifdef HAS_ZLIB
#    include <zlib.h>
#endif
#include "memref.h"

enum invalidation_type_t {
    INVALIDATION_INCLUSIVE,
    INVALIDATION_COHERENCE,
};

enum class metric_name_t {
    HITS,
    MISSES,
    HITS_AT_RESET,
    MISSES_AT_RESET,
    COMPULSORY_MISSES,
    CHILD_HITS,
    CHILD_HITS_AT_RESET,
    INCLUSIVE_INVALIDATES,
    COHERENCE_INVALIDATES,
    PREFETCH_HITS,
    PREFETCH_MISSES,
    FLUSHES
};

struct bound {
    addr_t beg;
    addr_t end;
};

class access_count_t {
public:
    access_count_t(int block_size)
        : block_size_(block_size)
    {
        if (!IS_POWER_OF_2(block_size)) {
            ERRMSG("Block size should be a power of 2.");
            return;
        }
        int block_size_bits = compute_log2(block_size);
        block_size_mask_ = ~((1 << block_size_bits) - 1);
    }

    // Takes non-aligned address and inserts bound consisting of the nearest multiples
    // of the block_size.
    void
    insert(addr_t addr_beg, std::map<addr_t, addr_t>::iterator next_it)
    {
        // Round the address down to the nearest multiple of the block_size.
        addr_beg &= block_size_mask_;
        addr_t addr_end = addr_beg + block_size_;

        // Detect the overflow and assign maximum possible value to the addr_end.
        if (addr_beg > addr_end) {
            addr_end = std::numeric_limits<addr_t>::max();
        }

        std::map<addr_t, addr_t>::reverse_iterator prev_it(next_it);

        // Current bound -> (addr_beg...addr_end) connects previous and
        // next bound
        if (prev_it != bounds.rend() && prev_it->second == addr_beg &&
            next_it != bounds.end() && next_it->first == addr_end) {
            prev_it->second = next_it->second;
            bounds.erase(next_it);
            // Current bound extends previous bound
        } else if (prev_it != bounds.rend() && prev_it->second == addr_beg) {
            prev_it->second = addr_end;
            // Current bound extends next bound
        } else if (next_it != bounds.end() && next_it->first == addr_end) {
            addr_t bound_end = next_it->second;
            // We need to reinsert the element when changing key value.
            // Iterator hint should provide costant complexity of this operation.
            bounds.erase(next_it++);
            bounds.emplace_hint(next_it, addr_beg, bound_end);
        } else {
            bounds.emplace_hint(next_it, addr_beg, addr_end);
        }
    }

    // Takes non-aligned address. Returns:
    // - boolean value indicating whether the address has ever been accessed
    // - iterator to the bound where the address is located or the element which should
    //   be provided as a hint when inserting new bound with given address.
    std::pair<bool, std::map<addr_t, addr_t>::iterator>
    lookup(addr_t addr)
    {
        // Function upper_bound returns bound which beginning is larger
        // then the addr.
        auto next_it = bounds.upper_bound(addr);
        std::map<addr_t, addr_t>::reverse_iterator prev_it(next_it);

        if (prev_it != bounds.rend() && addr >= prev_it->first &&
            addr < prev_it->second) {
            return std::make_pair(true, prev_it.base());
        } else {
            return std::make_pair(false, next_it);
        }
    }

private:
    // Bounds are members of the std::map. The beginning of the bound is stored
    // as a key and the end as a value.
    std::map<addr_t, addr_t> bounds;
    int block_size_mask_ = 0;
    int block_size_;
};

// https://stackoverflow.com/questions/1120140/how-can-i-read-and-parse-csv-files-in-c

class csv_row_t
{
    public:
        std::string operator[](std::size_t index) const
        {
            return std::string(&m_line[m_data[index] + 1], m_data[index + 1] -  (m_data[index] + 1));
        }
        std::size_t size() const
        {
            return m_data.size() - 1;
        }
        void readNextRow(std::istream& str)
        {
            std::getline(str, m_line);

            m_data.clear();
            m_data.emplace_back(-1);
            std::string::size_type pos = 0;
            while((pos = m_line.find(',', pos)) != std::string::npos)
            {
                m_data.emplace_back(pos);
                //check for double quotes
                if (m_line.size() > pos + 1 && m_line[pos + 1] == '"') {
                    pos = m_line.find("\",", pos + 2);
                    if (pos == std::string::npos) {
                        break;
                    }
                } else {
                    pos++;
                }
            }
            // This checks for a trailing comma with no data after it.
            pos   = m_line.size();
            m_data.emplace_back(pos);
        }
    private:
        std::string         m_line;
        std::vector<int>    m_data;
};


class caching_device_stats_t {
public:
    explicit caching_device_stats_t(const std::string &miss_file, const std::string &addr2line_file,
                                    const std::string &output_file,
                                    int block_size,
                                    bool warmup_enabled = false,
                                    bool is_coherent = false,
                                    bool record_instr_misses = false
                                    );

    virtual ~caching_device_stats_t();

    // Called on each access.
    // A multi-block memory reference invokes this routine
    // separately for each block touched.
    virtual void
    access(const memref_t &memref, bool hit, caching_device_block_t *cache_block);

    // Called on each access by a child caching device.
    virtual void
    child_access(const memref_t &memref, bool hit, caching_device_block_t *cache_block);

    virtual void
    print_stats(std::string prefix);

    virtual void
    reset();

    virtual bool operator!()
    {
        return !success_;
    }

    // Process invalidations due to cache inclusions or external writes.
    virtual void
    invalidate(invalidation_type_t invalidation_type);

    int_least64_t
    get_metric(metric_name_t metric) const
    {
        if (stats_map_.find(metric) != stats_map_.end()) {
            return stats_map_.at(metric);
        } else {
            ERRMSG("Wrong metric name.\n");
            return 0;
        }
    }

protected:
    bool success_;

    // print different groups of information, beneficial for code reuse
    virtual void
    print_warmup(std::string prefix);
    virtual void
    print_counts(std::string prefix); // hit/miss numbers
    virtual void
    print_rates(std::string prefix); // hit/miss rates
    virtual void
    print_child_stats(std::string prefix); // child/total info

    virtual void
    dump_miss(const memref_t &memref);

    void
    print_miss_hist(std::string prefix, int report_top = 10);

    void
    write_instr_info_file();

    void
    check_compulsory_miss(addr_t addr);

    bool
    read_csv(const std::string &file_name);

    struct instr_access_hist_t {
        std::unordered_map<addr_t, int_least64_t> access_hist;
        std::string error;
    };

    struct debug_info_t {
        std::string symbol;
        std::string path;
        int line;
    };

    instr_access_hist_t instr_access_hist_;

    int_least64_t num_hits_;
    int_least64_t num_misses_;
    int_least64_t num_compulsory_misses_;
    int_least64_t num_working_set_misses_;
    int_least64_t num_child_hits_;

    int_least64_t num_inclusive_invalidates_;
    int_least64_t num_coherence_invalidates_;

    // Stats saved when the last reset was called. This helps us get insight
    // into what the stats were when the cache was warmed up.
    int_least64_t num_hits_at_reset_;
    int_least64_t num_misses_at_reset_;
    int_least64_t num_child_hits_at_reset_;
    // Enabled if options warmup_refs > 0 || warmup_fraction > 0
    bool warmup_enabled_;

    // Print out write invalidations if cache is coherent.
    bool is_coherent_;

    // References to the properties with statistics are held in the map with the
    // statistic name as the key. Sample map element: {HITS, num_hits_}
    std::map<metric_name_t, int_least64_t &> stats_map_;

    // We provide a feature of dumping misses to a file.
    bool dump_misses_;

    access_count_t access_count_;

    std::unordered_map<addr_t, debug_info_t*> addr2line_map_;
    
    bool record_instr_access_misses_;

    bool map_to_line_;

    bool write_instr_info_file_;

    const std::string addr2line_file_;

    const std::string output_file_;
    
#ifdef HAS_ZLIB
    gzFile file_;
#else
    FILE *file_;
#endif
};

#endif /* _CACHING_DEVICE_STATS_H_ */
