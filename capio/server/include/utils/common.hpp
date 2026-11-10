#ifndef CAPIO_SERVER_UTILS_COMMON_HPP
#define CAPIO_SERVER_UTILS_COMMON_HPP

#include <iomanip>
#include <string>
#include <thread>

#include "calf/StlLogger.h"
#include "capiocl_adapter.hpp"
#include "client-manager/client_manager.hpp"
#include "common/dirent.hpp"
#include "remote/home_node_service.hpp"
#include "storage/capio_file.hpp"
#include "storage/manager.hpp"
#include "utils/types.hpp"

extern ClientManager *client_manager;
extern StorageManager *storage_manager;

void wait_for_dirent_data(off64_t wait_size, int wait_tid, int wait_fd, int wait_count,
                          CapioFile &wait_c_file);

inline void send_dirent_to_client(int tid, int fd, CapioFile &c_file, off64_t offset,
                                  off64_t count) {
    START_LOG(gettid(), "call(tid=%d, fd=%d, offset=%ld, count=%ld)", tid, fd, offset, count);

    struct linux_dirent64 *dir_entity;

    char *incoming      = c_file.getBuffer();
    int first_entry     = static_cast<int>(offset / sizeof(linux_dirent64));
    off64_t end_of_read = std::min(offset + count, c_file.getStoredSize());
    int last_entry      = static_cast<int>(end_of_read / sizeof(linux_dirent64));
    off64_t actual_size = (last_entry - first_entry) * static_cast<off64_t>(sizeof(linux_dirent64));

    if (actual_size > 0) {
        auto dirents = std::unique_ptr<linux_dirent64[]>(new linux_dirent64[actual_size]);

        for (int i = first_entry; i < last_entry; i++) {
            dir_entity = (struct linux_dirent64 *) (incoming + i * sizeof(linux_dirent64));
            auto &current_dirent = dirents[i - first_entry];

            current_dirent.d_reclen = sizeof(linux_dirent64);
            current_dirent.d_ino    = dir_entity->d_ino;
            current_dirent.d_off    = (i + 1) * current_dirent.d_reclen;
            current_dirent.d_type   = dir_entity->d_type;
            strcpy(current_dirent.d_name, dir_entity->d_name);

            LOG("DIRENT NAME: %s - TARGET NAME: %s", dir_entity->d_name, current_dirent.d_name);
        }

        client_manager->replyToClient(tid, offset, reinterpret_cast<char *>(dirents.get()) - offset,
                                      actual_size);
        storage_manager->setFileOffset(tid, fd, offset + actual_size);
        return;
    }

    // A consumer of a directory that is not committed waits for more entries. Its producer has
    // already added every entry it will add before reading it, so it reads the end of the
    // directory instead, as std::filesystem::remove_all does after removing the entries.
    const auto &path_to_check = storage_manager->getPath(tid, fd);
    if (!c_file.isCommitted() && CapioCLEngine::get().isFirable(path_to_check) &&
        !CapioCLEngine::get().isProducer(path_to_check, client_manager->getAppName(tid))) {
        LOG("File %s has mode no_update and not enough data is available", path_to_check.c_str());
        std::thread t(wait_for_dirent_data, (last_entry + 1) * sizeof(linux_dirent64), tid, fd,
                      count, std::ref(c_file));
        t.detach();
        return;
    }

    client_manager->replyToClient(tid, offset);
}

inline void wait_for_dirent_data(const off64_t wait_size, const int wait_tid, const int wait_fd,
                                 const int wait_count, CapioFile &wait_c_file) {
    const auto current_size = storage_manager->getFileOffset(wait_tid, wait_fd);
    START_LOG(gettid(), "call(wait_size=%d, current_size = %ld, wait_fd=%d, wait_count=%d)",
              wait_size, current_size, wait_fd, wait_count);
    wait_c_file.waitForData(wait_size);
    LOG("New capio file size = %ld", wait_c_file.getStoredSize());
    send_dirent_to_client(wait_tid, wait_fd, wait_c_file, current_size, wait_count);
}

inline bool is_int(const std::string &s) {
    START_LOG(gettid(), "call(%s)", s.c_str());
    bool res = false;
    if (!s.empty()) {
        char *p;
        strtol(s.c_str(), &p, 10);
        res = *p == 0;
    }
    return res;
}

/**
 * Record that @p tid wrote the bytes [@p offset, @p end_of_write) of @p path, once they are in the
 * buffer of @p c_file: the bytes become readable and wake the readers waiting for them, @p tid
 * becomes a producer of the file, and the first write publishes the location of the file and adds
 * it to its directory. Every handler that puts data into a file calls it, as write_handler and
 * copy_file_range_handler do. It does not move the file offset of the writer.
 *
 * @param tid thread that wrote the data
 * @param path path of the file written
 * @param c_file the file written, whose buffer already holds the data
 * @param offset start of the data written
 * @param end_of_write end of the data written, excluded
 */
inline void register_written_data(const int tid, const std::filesystem::path &path,
                                  CapioFile &c_file, const off64_t offset,
                                  const off64_t end_of_write) {
    START_LOG(gettid(), "call(tid=%d, path=%s, offset=%ld, end_of_write=%ld)", tid, path.c_str(),
              offset, end_of_write);

    client_manager->registerProducedFile(tid, path);
    c_file.insertSector(offset, end_of_write);
    if (c_file.isFirstWrite()) {
        c_file.registerFirstWrite();
        home_node_service->setHome(path);
        // TODO: it works only if there is one prod per file
        storage_manager->updateDirectory(tid, path);
    }
}

/**
 * Copy the data the input holds at the position of @p copy, up to its count, and reply to the
 * client with the number of bytes copied, 0 at the end of the input. The caller has already waited
 * for the data, as a read would.
 *
 * @param copy
 */
inline void copy_file_range_data(const CSCopyFileRange_t &copy) {
    START_LOG(gettid(), "call(tid=%d, path_in=%s, path_out=%s, offset_in=%ld, count=%ld)", copy.tid,
              copy.path_in.c_str(), copy.path_out.c_str(), copy.offset_in, copy.count);

    const CapioFile &c_file_in  = storage_manager->get(copy.path_in);
    const off64_t end_of_sector = c_file_in.getSectorEnd(copy.offset_in);
    const off64_t bytes_copied  = end_of_sector == CAPIO_SECTOR_NOT_FOUND
                                      ? 0
                                      : std::min(copy.count, end_of_sector - copy.offset_in);

    if (bytes_copied > 0) {
        CapioFile &c_file_out      = storage_manager->get(copy.path_out);
        const off64_t end_of_write = copy.offset_out + bytes_copied;
        c_file_out.createBuffer(copy.path_out, true);
        if (end_of_write > c_file_out.getBufSize()) {
            c_file_out.expandBuffer(end_of_write);
        }
        c_file_out.copyFromFile(c_file_in, copy.offset_in, copy.offset_out, bytes_copied);
        register_written_data(copy.tid, copy.path_out, c_file_out, copy.offset_out, end_of_write);
    }
    LOG("copied %ld bytes", bytes_copied);
    client_manager->replyToClient(copy.tid, bytes_copied);
}

#endif // CAPIO_SERVER_UTILS_COMMON_HPP
