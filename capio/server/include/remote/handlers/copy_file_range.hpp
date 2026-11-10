#ifndef CAPIO_SERVER_REMOTE_HANDLERS_COPY_FILE_RANGE_HPP
#define CAPIO_SERVER_REMOTE_HANDLERS_COPY_FILE_RANGE_HPP

/**
 * copy_file_range from an input stored on another node: the node of the client asks the node that
 * stores the input for its data, which goes from server to server, and then copies it to the
 * output as for an input stored on the node of the client. The request carries the descriptors
 * and the positions of the copy, and the reply sends them back with the data, so that no node
 * keeps any state about a copy in progress.
 */

#include <thread>

#include "calf/StlLogger.h"
#include "remote/backend.hpp"
#include "remote/requests.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"
#include "utils/types.hpp"

extern StorageManager *storage_manager;

/**
 * On the node of the client: copy from an input stored on another node. The copy of the input
 * kept on this node serves the copy when it already holds the data, as for a read. Otherwise the
 * node that stores the input is asked for its data, and handle_remote_copy_file_range_reply()
 * copies it when it arrives.
 *
 * @param copy
 * @param home the input's home node, which the data is asked from
 */
inline void handle_remote_copy_file_range(const CSCopyFileRange_t &copy, const std::string &home) {
    START_LOG(gettid(), "call(tid=%d, path_in=%s, offset_in=%ld, count=%ld)", copy.tid,
              copy.path_in.c_str(), copy.offset_in, copy.count);

    const CapioFile &c_file_in  = storage_manager->get(copy.path_in);
    const off64_t end_of_sector = c_file_in.getSectorEnd(copy.offset_in);
    const bool sector_found     = end_of_sector != CAPIO_SECTOR_NOT_FOUND;
    const bool range_here       = sector_found && copy.offset_in + copy.count <= end_of_sector;
    const bool all_data_here    = c_file_in.isCommitted() &&
                                  (sector_found ? end_of_sector : 0) == c_file_in.getRealFileSize();

    if (range_here || all_data_here) {
        LOG("The data of the input is already on this node");
        copy_file_range_data(copy);
        return;
    }
    handle_remote_copy_file_range_request(copy.tid, copy.fd_in, copy.fd_out, copy.path_in,
                                          copy.offset_in, copy.offset_out, copy.count, home);
}

/**
 * On the node that stores the input: send to @p dest the data of the input from @p offset_in,
 * once a read would be served. As handle_remote_read does for a read, a separate thread waits for
 * the data of the input when it is not committed and the requested range is not all there yet.
 *
 * @param path_in
 * @param dest node of the client
 * @param tid
 * @param fd_in
 * @param fd_out
 * @param offset_in
 * @param offset_out
 * @param count
 */
inline void handle_remote_copy_file_range_data(const std::filesystem::path &path_in,
                                               const std::string &dest, const int tid,
                                               const int fd_in, const int fd_out,
                                               const off64_t offset_in, const off64_t offset_out,
                                               const off64_t count) {
    START_LOG(gettid(), "call(path_in=%s, dest=%s, tid=%d, offset_in=%ld, count=%ld)",
              path_in.c_str(), dest.c_str(), tid, offset_in, count);

    const auto serve = [path_in, dest, tid, fd_in, fd_out, offset_in, offset_out, count] {
        CapioFile &c_file    = storage_manager->get(path_in);
        const off64_t nbytes = remote_reply_size(c_file, offset_in);
        serve_remote_copy_file_range_request(
            tid, fd_in, fd_out, offset_in, offset_out, count, nbytes, c_file.getStoredSize(),
            c_file.isCommitted(), dest, nbytes == 0 ? nullptr : c_file.getBuffer() + offset_in);
    };

    const CapioFile &c_file   = storage_manager->get(path_in);
    const bool data_available = offset_in + count <= c_file.getStoredSize();
    if (c_file.isCommitted() || (CapioCLEngine::get().isFirable(path_in) && data_available)) {
        serve();
        return;
    }
    LOG("Waiting on a separate thread for the data of the input");
    std::thread t([&c_file, offset_in, count, serve] {
        c_file.waitForData(offset_in + count);
        serve();
    });
    t.detach();
}

/**
 * On the node of the client: store the data of the input sent by @p source in the copy of the
 * input kept on this node, as a read reply does, and copy it to the output.
 *
 * @param source node that stores the input
 * @param tid
 * @param fd_in
 * @param fd_out
 * @param offset_in
 * @param offset_out
 * @param count
 * @param nbytes number of bytes of the input received from @p source
 * @param file_size size of the input on @p source
 * @param complete whether the input is committed
 */
inline void handle_remote_copy_file_range_reply(const std::string &source, const int tid,
                                                const int fd_in, const int fd_out,
                                                const off64_t offset_in, const off64_t offset_out,
                                                const off64_t count, const off64_t nbytes,
                                                const off64_t file_size, const bool complete) {
    START_LOG(gettid(), "call(source=%s, tid=%d, offset_in=%ld, nbytes=%ld, complete=%s)",
              source.c_str(), tid, offset_in, nbytes, complete ? "true" : "false");

    const CSCopyFileRange_t copy{tid,
                                 fd_in,
                                 fd_out,
                                 offset_in,
                                 offset_out,
                                 count,
                                 storage_manager->getPath(tid, fd_in),
                                 storage_manager->getPath(tid, fd_out)};
    CapioFile &c_file_in = storage_manager->get(copy.path_in);

    c_file_in.createBuffer(copy.path_in, false);
    if (nbytes != 0) {
        if (offset_in + nbytes > c_file_in.getBufSize()) {
            c_file_in.expandBuffer(offset_in + nbytes);
        }
        c_file_in.readFromNode(source, offset_in, nbytes);
    }
    c_file_in.setRealFileSize(file_size);
    c_file_in.insertSector(offset_in, offset_in + nbytes);
    c_file_in.setCommitted(complete);

    copy_file_range_data(copy);
}

inline void remote_copy_file_range_handler(const RemoteRequest &request) {
    char path_in[PATH_MAX];
    int tid, fd_in, fd_out;
    off64_t offset_in, offset_out, count;
    sscanf(request.get_content(), "%s %d %d %d %ld %ld %ld", path_in, &tid, &fd_in, &fd_out,
           &offset_in, &offset_out, &count);
    handle_remote_copy_file_range_data(path_in, request.get_source(), tid, fd_in, fd_out, offset_in,
                                       offset_out, count);
}

inline void remote_copy_file_range_reply_handler(const RemoteRequest &request) {
    int tid, fd_in, fd_out, complete;
    off64_t offset_in, offset_out, count, nbytes, file_size;
    sscanf(request.get_content(), "%d %d %d %ld %ld %ld %ld %ld %d", &tid, &fd_in, &fd_out,
           &offset_in, &offset_out, &count, &nbytes, &file_size, &complete);
    handle_remote_copy_file_range_reply(request.get_source(), tid, fd_in, fd_out, offset_in,
                                        offset_out, count, nbytes, file_size, complete);
}

#endif // CAPIO_SERVER_REMOTE_HANDLERS_COPY_FILE_RANGE_HPP
