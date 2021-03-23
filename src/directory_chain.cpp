#include "directory_chain.h"

namespace phylum {

int32_t directory_chain::mount() {
    logged_task lt{ "mount" };

    head(0);
    sector(0);

    auto err = load();
    if (err < 0) {
        return err;
    }

    phydebugf("mounted");
    back_to_head();

    return 0;
}

int32_t directory_chain::format() {
    logged_task lt{ "format" };

    head(0);
    sector(0);

    phydebugf("formatting");
    auto err = write_header();
    if (err < 0) {
        return err;
    }

    auto hdr = header<sector_chain_header_t>();
    hdr->pp = InvalidSector;

    appendable(true);
    dirty(true);

    return 0;
}

int32_t directory_chain::prepare(size_t required) {
    logged_task lt{ "prepare" };

    if (!appendable()) {
        auto err = seek_end_of_chain();
        if (err < 0) {
            phydebugf("seek-end failed");
            return err;
        }
    }

    auto err = grow_if_necessary(required);
    if (err < 0) {
        phydebugf("grow failed");
        return err;
    }

    assert(header<directory_chain_header_t>()->type == entry_type::DirectorySector);

    appendable(true);
    dirty(true);

    return 0;
}

int32_t directory_chain::grow_if_necessary(size_t required) {
    auto delimiter_overhead = varint_encoding_length(required);
    auto total_required = delimiter_overhead + required;
    if (db().room_for(total_required)) {
        return 0;
    }

    return grow_tail();
}

int32_t directory_chain::seek_end_of_buffer() {
    return db().seek_end();
}

int32_t directory_chain::write_header() {
    logged_task lt{ "dc-write-hdr", this->name() };

    db().emplace<directory_chain_header_t>();

    dirty(true);

    return 0;
}

int32_t directory_chain::touch(const char *name) {
    logged_task lt{ "dir-touch" };

    assert(emplace<file_entry_t>(name) >= 0);

    return 0;
}

int32_t directory_chain::file_attribute(file_id_t id, open_file_attribute attribute) {
    logged_task lt{ "dir-file-attribute" };

    file_attribute_t fa{ id, attribute.type, attribute.size };
    assert(append<file_attribute_t>(fa, (uint8_t const *)attribute.ptr, attribute.size) >= 0);

    return 0;
}

int32_t directory_chain::file_chain(file_id_t id, head_tail_t chain) {
    logged_task lt{ "dir-file-chain" };

    assert(emplace<file_data_t>(id, chain) >= 0);

    return 0;
}

int32_t directory_chain::file_data(file_id_t id, uint8_t const *buffer, size_t size) {
    logged_task lt{ "dir-file-data" };

    file_data_t fd{ id, (uint32_t)size };
    assert(append<file_data_t>(fd, buffer, size) >= 0);

    return 0;
}

int32_t directory_chain::find(const char *name, open_file_config file_cfg) {
    logged_task lt{ "dir-find" };

    file_ = found_file{};
    file_.cfg = file_cfg;

    // Zero attribute values before we scan.
    for (auto i = 0u; i < file_cfg.nattrs; ++i) {
        auto &attr = file_cfg.attributes[i];
        bzero(attr.ptr, attr.size);
    }

    auto err = walk([&](entry_t const *entry, written_record &record) {
        if (entry->type == entry_type::FileEntry) {
            auto fe = record.as<file_entry_t>();
            if (strncmp(fe->name, name, MaximumNameLength) == 0) {
                phydebugf("found(file) '%s' id=0x%x", name, fe->id);
                file_.id = fe->id;
            }
        }
        if (entry->type == entry_type::FileData) {
            auto fd = record.as<file_data_t>();
            if (fd->id == file_.id) {
                if (fd->chain.head != InvalidSector || fd->chain.tail != InvalidSector) {
                    file_.size = 0;
                    file_.chain = fd->chain;
                } else {
                    file_.size += fd->size;
                }
            }
        }
        if (entry->type == entry_type::FileAttribute) {
            auto fa = record.as<file_attribute_t>();
            if (fa->id == file_.id) {
                for (auto i = 0u; i < file_cfg.nattrs; ++i) {
                    if (entry->type == file_cfg.attributes[i].type) {
                        auto data = record.data<file_attribute_t>();
                        assert(data.size() == file_cfg.attributes[i].size);
                        memcpy(file_cfg.attributes[i].ptr, data.ptr(), data.size());
                    }
                }
            }
        }
        return 0;
    });

    if (err < 0) {
        phydebugf("find failed");
        file_ = found_file{};
        return err;
    }

    if (file_.id == UINT32_MAX) {
        phydebugf("find found no file");
        file_ = found_file{};
        return -1;
    }

    return 1;
}

found_file directory_chain::open() {
    logged_task lt{ "dir-open", this->name() };
    assert(file_.id != UINT32_MAX);
    return file_;
}

} // namespace phylum
