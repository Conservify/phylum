#include "directory_chain.h"

namespace phylum {

int32_t directory_chain::mount() {
    logged_task lt{ "mount" };

    sector(head());

    dhara_page_t page = 0;
    auto find = sectors()->find(0, &page);
    if (find < 0) {
        return find;
    }

    auto page_lock = db().writing(sector());

    auto err = load(page_lock);
    if (err < 0) {
        return err;
    }

    back_to_head(page_lock);

    phyinfof("mounted %d", sector());

    return 0;
}

int32_t directory_chain::format() {
    logged_task lt{ "format" };
    auto page_lock = db().writing(head());

    sector(head());

    phyinfof("formatting");
    auto err = write_header(page_lock);
    if (err < 0) {
        return err;
    }

    assert(db().write_header<directory_chain_header_t>([&](auto header) {
        header->pp = InvalidSector;
        return 0;
    }) == 0);

    page_lock.dirty();
    appendable(true);

    err = flush(page_lock);
    if (err < 0) {
        return err;
    }

    return 0;
}

int32_t directory_chain::write_header(page_lock &page_lock) {
    logged_task lt{ "dc-write-hdr", this->name() };

    db().emplace<directory_chain_header_t>();

    page_lock.dirty();

    return 0;
}

int32_t directory_chain::touch(const char *name) {
    logged_task lt{ "dir-touch" };

    auto page_lock = db().writing(sector());

    assert(emplace<file_entry_t>(page_lock, name) >= 0);

    auto err = flush(page_lock);
    if (err < 0) {
        return err;
    }

    return 0;
}

int32_t directory_chain::unlink(const char *name) {
    logged_task lt{ "dir-unlink" };

    auto page_lock = db().writing(sector());

    auto id = make_file_id(name);
    assert(emplace<file_data_t>(page_lock, id, (uint32_t)0) >= 0);

    auto err = flush(page_lock);
    if (err < 0) {
        return err;
    }

    return 0;
}

int32_t directory_chain::file_attribute(file_id_t id, open_file_attribute attribute) {
    logged_task lt{ "dir-file-attribute" };

    auto page_lock = db().writing(sector());

    file_attribute_t fa{ id, attribute.type, attribute.size };
    assert(append<file_attribute_t>(page_lock, fa, (uint8_t const *)attribute.ptr, attribute.size) >= 0);

    return 0;
}

int32_t directory_chain::file_attributes(file_id_t file_id, open_file_attribute *attributes, size_t nattrs) {
    for (auto i = 0u; i < nattrs; ++i) {
        auto &attr = attributes[i];
        if (attr.dirty) {
            if (attr.size == sizeof(uint32_t)) {
                uint32_t value = *(uint32_t *)attr.ptr;
                phydebugf("attribute[%d] write type=%d size=%d value=0x%x", i, attr.type, attr.size, value);
            } else {
                phydebugf("attribute[%d] write type=%d size=%d", i, attr.type, attr.size);
            }
            assert(file_attribute(file_id, attr) >= 0);
        }
    }

    auto err = flush();
    if (err < 0) {
        return err;
    }

    return 0;
}

int32_t directory_chain::file_chain(file_id_t id, head_tail_t chain) {
    logged_task lt{ "dir-file-chain" };
    auto page_lock = db().writing(sector());

    assert(emplace<file_data_t>(page_lock, id, chain) >= 0);

    auto err = flush(page_lock);
    if (err < 0) {
        return err;
    }

    return 0;
}

int32_t directory_chain::file_data(file_id_t id, uint8_t const *buffer, size_t size) {
    logged_task lt{ "dir-file-data" };
    auto page_lock = db().writing(sector());

    file_data_t fd{ id, (uint32_t)size };
    assert(append<file_data_t>(page_lock, fd, buffer, size) >= 0);

    auto err = flush(page_lock);
    if (err < 0) {
        return err;
    }

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

    auto err = walk([&](auto &/*page_lock*/, auto *entry, record_ptr &record) {
        // HACK Buffer is unpaged outside of this lambda.
        file_.directory_capacity = db().size() / 2;

        if (entry->type == entry_type::FileEntry) {
            auto fe = record.as<file_entry_t>();
            if (strncmp(fe->name, name, MaximumNameLength) == 0) {
                file_.id = fe->id;
            }
        }
        if (entry->type == entry_type::FileData) {
            auto fd = record.as<file_data_t>();
            if (fd->id == file_.id) {
                if (fd->chain.head != InvalidSector || fd->chain.tail != InvalidSector) {
                    file_.directory_size = 0;
                    file_.chain = fd->chain;
                } else {
                    if (fd->size == 0) {
                        file_ = found_file{ };
                    }
                    else {
                        file_.directory_size += fd->size;
                        file_.directory_capacity -= fd->size;
                    }
                }
            }
        }
        if (entry->type == entry_type::FileAttribute && file_cfg.nattrs > 0) {
            auto fa = record.as<file_attribute_t>();
            if (fa->id == file_.id) {
                for (auto i = 0u; i < file_cfg.nattrs; ++i) {
                    if (fa->type == file_cfg.attributes[i].type) {
                        auto err = record.read_data<file_attribute_t>([&](auto data_buffer) {
                            assert(data_buffer.available() == file_cfg.attributes[i].size);
                            memcpy(file_cfg.attributes[i].ptr, data_buffer.cursor(), data_buffer.available());
                            return data_buffer.available();
                        });
                        if (err < 0) {
                            return err;
                        }
                    }
                }
            }
        }
        return 0;
    });

    if (err < 0) {
        phyerrorf("find failed");
        file_ = found_file{};
        return err;
    }

    if (file_.id == UINT32_MAX) {
        phywarnf("find found no file");
        file_ = found_file{};
        return 0;
    }

    return 1;
}

found_file directory_chain::open() {
    assert(file_.id != UINT32_MAX);
    return file_;
}

int32_t directory_chain::seek_file_entry(file_id_t id) {
    return walk([&](auto &/*page_lock*/, auto const *entry, record_ptr &record) {
        if (entry->type == entry_type::FileEntry) {
            auto fe = record.as<file_entry_t>();
            if (fe->id == id) {
                appendable(false);
                return 1;
            }
        }
        return 0;
    });
}

int32_t directory_chain::read(file_id_t id, std::function<int32_t(read_buffer)> data_fn) {
    auto copied = 0u;

    auto err = walk([&](auto &/*page_lock*/, auto const *entry, record_ptr &record) {
        if (entry->type == entry_type::FileData) {
            auto fd = record.as<file_data_t>();
            if (fd->id == id) {
                phydebugf("%s (copy) id=0x%x bytes=%d size=%d", this->name(), fd->id, fd->size, file_.directory_size);

                auto err = record.read_data<file_data_t>([&](auto data_buffer) {
                    return data_fn(std::move(data_buffer));
                });
                if (err < 0) {
                    return err;
                }

                copied += err;
            }
        }
        return (int32_t)0;
    });
    if (err < 0) {
        return err;
    }
    return copied;
}

} // namespace phylum
