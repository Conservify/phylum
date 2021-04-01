#pragma once

#include "data_chain.h"
#include "simple_buffer.h"
#include "directory.h"

namespace phylum {

class file_appender {
private:
    directory *directory_{ nullptr };
    found_file file_;
    simple_buffer buffer_;
    data_chain data_chain_;

public:
    file_appender(sector_chain &other, directory *directory, found_file file);

    virtual ~file_appender();

private:
    file_appender(working_buffers &buffers, sector_map &sectors, sector_allocator &allocator, directory *directory, found_file file);

public:
    int32_t write(char const *str) {
        return write((uint8_t *)str, strlen(str));
    }

    int32_t write(char const *str, size_t size) {
        return write((uint8_t *)str, size);
    }

    int32_t write(uint8_t const *data, size_t size);

    int32_t flush();

    int32_t close();

    uint32_t u32(uint8_t type);

    void u32(uint8_t type, uint32_t value);

    size_t length_sectors() {
        if (has_chain()) {
            return data_chain_.length_sectors();
        }
        return 0;
    }

    data_chain_cursor cursor() {
        if (has_chain()) {
            return data_chain_.cursor();
        }
        return data_chain_cursor{ };
    }

private:
    int32_t make_data_chain();

    bool has_chain() {
        return data_chain_.valid();
    }
};

} // namespace phylum
