#include "delimited_buffer.h"

namespace phylum {

void *delimited_buffer::reserve(size_t length) {
    assert(buffer_.valid());

    // If this is our first record, write the offset value first.
    if (buffer_.position() == 0) {
        if (offset_ > 0) {
            debugf("writing offset: %zu\n", offset_);
        }
        auto needed = varint_encoding_length(offset_);
        auto p = buffer_.take(needed);
        if (*p != 0xff) {
            fk_dump_memory("overwrite ", p, needed);
            assert(*p == 0xff);
        }
        varint_encode(offset_, p, needed);
    }

    // Verify enough room for the record and its prefix.
    auto delimiter_overhead = varint_encoding_length(length);

    // Encode the length before the record.
    auto position_before = buffer_.position();
    auto p = buffer_.take(delimiter_overhead + length);
    assert(p != nullptr);

    varint_encode(length, p, delimiter_overhead);

    auto position_after = buffer_.position();

    debugf("reserve: %zu (+%d) = %zu before=%zu after=%zu\n", length, delimiter_overhead, length + delimiter_overhead,
           position_before, position_after);

    // This is where we'll ask the caller to construct their record.
    auto allocated = p + delimiter_overhead;

    // TODO Check the whole region?
    if (*allocated != 0xff) {
        fk_dump_memory("overwrite ", allocated, length);
        assert(*allocated == 0xff);
    }

    return allocated;
}

} // namespace phylum
