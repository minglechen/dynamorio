
#ifndef _INSTR_COUNT_H_
#define _INSTR_COUNT_H_ 1

#include <mutex>
#include <string>
#include <unordered_map>

#include "analysis_tool.h"
#include "memref.h"

class csv_row_t {
public:
    std::string
    operator[](std::size_t index) const
    {
        return std::string(&m_line[m_data[index] + 1],
                           m_data[index + 1] - (m_data[index] + 1));
    }
    std::size_t
    size() const
    {
        return m_data.size() - 1;
    }
    void
    readNextRow(std::istream &str)
    {
        std::getline(str, m_line);

        m_data.clear();
        m_data.emplace_back(-1);
        std::string::size_type pos = 0;
        while ((pos = m_line.find(',', pos)) != std::string::npos) {
            m_data.emplace_back(pos);
            // check for double quotes
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
        pos = m_line.size();
        m_data.emplace_back(pos);
    }

private:
    std::string m_line;
    std::vector<int> m_data;
};

class instr_count_t : public analysis_tool_t {
public:
    instr_count_t(const std::string &addr2line_path, const std::string &output_file_path,
                  unsigned int report_top, unsigned int verbose);
    virtual ~instr_count_t();
    bool
    process_memref(const memref_t &memref) override;
    bool
    print_results() override;
    bool
    parallel_shard_supported() override;
    void *
    parallel_worker_init(int worker_index) override;
    std::string
    parallel_worker_exit(void *worker_data) override;
    void *
    parallel_shard_init(int shard_index, void *worker_data) override;
    bool
    parallel_shard_exit(void *shard_data) override;
    bool
    parallel_shard_memref(void *shard_data, const memref_t &memref) override;
    std::string
    parallel_shard_error(void *shard_data) override;

    virtual bool
    reduce_results();

protected:
    struct shard_data_t {
        std::unordered_map<addr_t, uint64_t> instr_map;
        std::string error;
    };

    static bool
    cmp(const std::pair<addr_t, uint64_t> &l, const std::pair<addr_t, uint64_t> &r);

    bool
    read_csv(const std::string &path);

    struct debug_info_t {
        std::string symbol;
        std::string path;
        int line;
    };

    bool
    write_instr_info_file();

    const std::string addr2line_path_;
    const std::string output_dir_;
    std::unordered_map<addr_t, debug_info_t *> addr2line_map_;
    unsigned int knob_report_top_; /* most accessed instrs */
    static const std::string TOOL_NAME;
    std::unordered_map<memref_tid_t, shard_data_t *> shard_map_;
    // This mutex is only needed in parallel_shard_init.  In all other accesses to
    // shard_map (process_memref, print_results) we are single-threaded.
    std::mutex shard_map_mutex_;
    shard_data_t serial_shard_;
    // The combined data from all the shards.
    shard_data_t reduced_;
};

#endif /* _INSTR_COUNT_H_ */