
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <vector>
#include "directory_iterator.h"
#include "instr_count.h"

const std::string instr_count_t::TOOL_NAME = "Instruction count tool";

analysis_tool_t *
instr_count_tool_create(const std::string &addr2line_path = "",
                        const std::string &output_dir = "", unsigned int report_top = 10,
                        unsigned int verbose = 0)
{
    return new instr_count_t(addr2line_path, output_dir, report_top, verbose);
}

instr_count_t::instr_count_t(const std::string &addr2line_path,
                             const std::string &output_dir, unsigned int report_top,
                             unsigned int verbose)
    : addr2line_path_(addr2line_path)
    , output_dir_(output_dir)
    , knob_report_top_(report_top)
{
}

instr_count_t::~instr_count_t()
{
    for (auto &iter : shard_map_) {
        delete iter.second;
    }
    for (auto &iter : addr2line_map_) {
        delete iter.second;
    }
}

bool
instr_count_t::parallel_shard_supported()
{
    return true;
}

void *
instr_count_t::parallel_worker_init(int worker_index)
{
    return nullptr;
}

std::string
instr_count_t::parallel_worker_exit(void *worker_data)
{
    return "";
}

void *
instr_count_t::parallel_shard_init(int shard_index, void *worker_data)
{
    auto shard = new shard_data_t;
    std::lock_guard<std::mutex> guard(shard_map_mutex_);
    shard_map_[shard_index] = shard;
    return reinterpret_cast<void *>(shard);
}

bool
instr_count_t::parallel_shard_exit(void *shard_data)
{
    // Nothing (we read the shard data in print_results).
    return true;
}

static inline addr_t
back_align(addr_t addr, addr_t align)
{
    return addr & ~(align - 1);
}

bool
instr_count_t::parallel_shard_memref(void *shard_data, const memref_t &memref)
{
    shard_data_t *shard = reinterpret_cast<shard_data_t *>(shard_data);
    std::unordered_map<addr_t, uint64_t> *instr_map = nullptr;
    addr_t start_addr;
    if (type_is_instr(memref.instr.type)) {
        instr_map = &shard->instr_map;
        start_addr = memref.instr.addr;
        ++(*instr_map)[start_addr];
    }
    return true;
}

std::string
instr_count_t::parallel_shard_error(void *shard_data)
{
    shard_data_t *shard = reinterpret_cast<shard_data_t *>(shard_data);
    return shard->error;
}

bool
instr_count_t::process_memref(const memref_t &memref)
{
    if (!parallel_shard_memref(reinterpret_cast<void *>(&serial_shard_), memref)) {
        error_string_ = serial_shard_.error;
        return false;
    }
    return true;
}

bool
instr_count_t::reduce_results()
{
    if (shard_map_.empty()) {
        reduced_ = serial_shard_;
    } else {
        for (const auto &shard : shard_map_) {
            for (const auto &keyvals : shard.second->instr_map) {
                reduced_.instr_map[keyvals.first] += keyvals.second;
            }
        }
    }
    return true;
}

bool
instr_count_t::cmp(const std::pair<addr_t, uint64_t> &l,
                   const std::pair<addr_t, uint64_t> &r)
{
    return l.second > r.second;
}

bool
instr_count_t::print_results()
{
    if (!reduce_results())
        return false;

    bool map_from_file = addr2line_path_.empty() ? false : true;
    if (map_from_file && !read_csv(addr2line_path_))
        return false;

    std::cerr << TOOL_NAME << " results:\n";
    std::cerr << "instructions: " << reduced_.instr_map.size()
              << " unique instructions\n";
    std::vector<std::pair<addr_t, uint64_t>> top(knob_report_top_);
    std::partial_sort_copy(reduced_.instr_map.begin(), reduced_.instr_map.end(),
                           top.begin(), top.end(), cmp);
    std::cerr << "instructions top " << top.size() << "\n";
    for (std::vector<std::pair<addr_t, uint64_t>>::iterator it = top.begin();
         it != top.end(); ++it) {
        std::cerr << std::setw(18) << std::hex << std::showbase << (it->first) << ": "
                  << std::dec << it->second << "\n";
        if (map_from_file) {
            auto it2 = addr2line_map_.find(it->first);
            if (it2 != addr2line_map_.end()) {
                std::cerr << "    " << it2->second->path << ":" << it2->second->line
                          << " " << it2->second->symbol << std::endl;
            }
        }
    }
    // Reset the i/o format for subsequent tool invocations.
    std::cerr << std::dec;

    if (!output_dir_.empty()) {
        write_instr_info_file();
    }
    return true;
}

bool
instr_count_t::write_instr_info_file()
{
    if (!read_csv(addr2line_path_)) {
        return false;
    }
    std::unordered_map<addr_t, u_int64_t> *top =
        reduced_.instr_map.empty() ? &serial_shard_.instr_map : &reduced_.instr_map;
    if (top->empty()) {
        return true;
    }

    std::string file_name = "instr_counts.csv";
    directory_iterator_t::create_directory(output_dir_);
    std::ofstream file(output_dir_ + DIRSEP + file_name);

    if (!file.good()) {
        ERRMSG("Could not open file %s", file_name.c_str());
        file.close();
        return false;
    }
    file << "addr,count,path,line,symbol" << std::endl;

    for (auto it = top->begin(); it != top->end(); ++it) {
        file << (it->first) << "," << (it->second) << ",";
        auto it2 = addr2line_map_.find(it->first);
        if (it2 != addr2line_map_.end()) {
            file << it2->second->path << "," << it2->second->line << ","
                 << it2->second->symbol << std::endl;
        } else {
            file << "unknown,0,unknown" << std::endl;
        }
    }
    file.close();
    return true;
}

bool
instr_count_t::read_csv(const std::string &file_name)
{
    // CSV already read
    if (addr2line_map_.size() > 0) {
        return true;
    }
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
        debug_info_t *debug_info = new debug_info_t();
        debug_info->symbol = row[symbol_index];
        debug_info->path = row[path_index];
        debug_info->line = std::stoi(row[line_index]);
        addr2line_map_.emplace(reinterpret_cast<addr_t>(std::stoul(row[addr_index])),
                               debug_info);
        row.readNextRow(file);
    };
    file.close();
    return true;
}