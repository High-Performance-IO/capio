#ifndef CAPIO_SERVER_HANDLERS_HPP
#define CAPIO_SERVER_HANDLERS_HPP

/*
 * The handlers of the requests that clients send to their server, declared in one place: each
 * family is defined in its own file of src/handlers/, and ClientRequestManager
 * (src/client_request_manager.cpp) maps each request code to its handler.
 */

#include "capiocl/engine.h"
#include "client-manager/client_manager.hpp"
#include "common/requests.hpp"
#include "remote/backend.hpp"
#include "remote/handlers/copy_file_range.hpp"
#include "remote/home_node_service.hpp"
#include "remote/requests.hpp"
#include "storage/manager.hpp"
#include "utils/capiocl_adapter.hpp"
#include "utils/common.hpp"
#include "utils/filesystem.hpp"
#include "utils/fs_root.hpp"
#include "utils/runtime_configuration.hpp"
#include "utils/server_state.hpp"
#include "utils/types.hpp"

/* ---------------------------------------------------------------- access */

void access_handler(const char *const str);

/* ---------------------------------------------------------------- clone */

void clone_handler(const char *const str);

/* ---------------------------------------------------------------- close */

void handle_close(int tid, int fd);

/**
 * @brief "tid fd [error]": error is what the close failed with, 0 if it worked (FS variant only)
 */
void close_handler(const char *str);

/* ---------------------------------------------------------------- copy_file_range */

/**
 * Copy from an input stored on this node. As handle_local_read does for a read, the copy first
 * waits on a separate thread for the commit of the input, or for its data, when they are not
 * available yet.
 *
 * @param copy
 * @param is_prod whether the client is a producer of the input, for which the input is complete
 */
void handle_local_copy_file_range(const CSCopyFileRange_t &copy, const bool is_prod);

/**
 * Copy from the input wherever it is stored, once its location is known.
 *
 * @param copy
 * @param is_prod whether the client is a producer of the input
 * @param home the input's home node; a producer copies locally and may not have one yet
 */
void handle_copy_file_range(const CSCopyFileRange_t &copy, const bool is_prod,
                            const std::optional<std::string> &home);

void copy_file_range_handler(const char *const str);

/* ---------------------------------------------------------------- dup */

void dup_handler(const char *const str);

/* ---------------------------------------------------------------- exit */

void handle_exit_group(int tid);

void exit_group_handler(const char *const str);

/* ---------------------------------------------------------------- getdents */

/// @brief Serve a getdents of a directory whose home is @p home, another server, from what this
/// server has of it, or ask @p home for the entries
void request_remote_getdents(int tid, int fd, off64_t count, const std::string &home);

void handle_getdents(int tid, int fd, long int count);

/// @brief "tid fd count offset". The offset is only used for descriptors of the FS variant: the
/// server tracks the offset of memory ones itself.
void getdents_handler(const char *const str);

/* ---------------------------------------------------------------- handshake */

/**
 * @brief Whether a client of @p variant is refused: the variant of a workflow, on this node, is
 * that of its first client, and the two variants cannot be mixed yet.
 * TODO: drop the refusal once the two variants work together in one workflow (#337)
 */
bool refuses_variant(const pid_t pid, const char *app_name, const char variant);

/**
 * @brief Whether an FS client whose CAPIO_DIR is @p capio_dir is refused. The server streams
 * through one directory, the real one behind the virtual "/": -r, or else the CAPIO_DIR of its
 * first FS client, which then starts the file system monitor there. Any other is refused.
 */
bool refuses_root(const pid_t pid, const char *app_name, const std::filesystem::path &capio_dir);

/**
 * @brief "tid pid app_name wait variant capio_dir", where capio_dir is the real CAPIO_DIR of an FS
 * client, empty for a memory one. Every handshake gets one answer: 1, CAPIO_REPLY_VARIANT_REFUSED
 * for a client of the other variant, or CAPIO_REPLY_ROOT_REFUSED for an FS client whose CAPIO_DIR
 * is not the directory the server streams through. A new thread (wait) is answered only once its
 * parent's clone request is handled (ClientManager::unlockClonedChild).
 */
void handshake_named_handler(const char *const str);

/* ---------------------------------------------------------------- mkdir */

void mkdir_handler(const char *const str);

/* ---------------------------------------------------------------- open */

void update_file_metadata(const std::filesystem::path &path, int tid, int fd, bool is_creat,
                          off64_t offset);

void wait_for_file_creation(const std::filesystem::path &path, pid_t tid, int fd);

void handle_create(int tid, int fd, const std::filesystem::path &path);

void handle_create_exclusive(int tid, int fd, const std::filesystem::path &path);

void handle_open(int tid, int fd, const std::filesystem::path &path);

void handle_create_fs(const int tid, const std::filesystem::path &path, const bool inherited);

void handle_open_fs(const int tid, const std::filesystem::path &path);

void create_handler(const char *const str);

void create_exclusive_handler(const char *const str);

void open_handler(const char *const str);

/**
 * @brief A descriptor or a file of the FS variant: "tid fd produced path". tid got the descriptor
 * fd of path, -1 if none (mkdir). produced is a CAPIO_FS_* value: with
 * CAPIO_FS_PRODUCED_IF_NO_PRODUCER, tid got a writable descriptor at exec, and it produces the
 * file only if nobody else does, so that "prog > out" does not take it from its producer.
 */
void create_fs_handler(const char *const str);

/// @brief open of the FS variant: "tid path"
void open_fs_handler(const char *const str);

/* ---------------------------------------------------------------- read */

void handle_pending_read(int tid, int fd, long int process_offset, long int count);

void handle_local_read(int tid, int fd, off64_t count, bool is_prod);

/// @brief Serve a read of a file whose home is @p home, another server, from what this server has
/// of it, or ask @p home for the data
void request_remote_read(int tid, int fd, off64_t count, const std::string &home);

void wait_for_file(const std::filesystem::path &path, int tid, int fd, off64_t count);

void handle_read(int tid, int fd, off64_t count);

void read_handler(const char *const str);

/// @brief "tid fd end_of_read". The client sends only the descriptor: its path is looked up in what
/// the server recorded when the file was opened.
void read_fs_handler(const char *const str);

/* ---------------------------------------------------------------- rename */

void handle_rename(int tid, const std::filesystem::path &oldpath,
                   const std::filesystem::path &newpath);

/**
 * @brief A file renamed out of CAPIO_DIR: its name leaves the server, but the file stays, under a
 * key that no virtual path can name (they all start with "/"), for the descriptors still open on
 * it, as a file unlinked while open.
 * TODO: write the file to its new name on the file system (#343)
 */
void handle_leave(int tid, const std::filesystem::path &oldpath);

/**
 * @brief Rename of the memory variant: "tid old_size old_pathnew_path".
 *
 * The two names are written one after the other with no separator, since a path can contain any
 * character: the first old_size bytes are the old name, the rest is the new one. As in
 * rename_fs_handler. No new name means the file left CAPIO_DIR.
 */
void rename_handler(const char *const str);

/**
 * @brief A rename done by an FS client, sent after the kernel has done it. Format:
 * "tid old_in new_in old_size old_pathnew_path", where old_in and new_in are 1 if that name is
 * inside CAPIO_DIR, and the two names are written back to back, the old one being old_size bytes.
 *
 * Everything the server knows about the file moves to the new name, and the new name's commit
 * rule is applied. This is what makes "write f.tmp, close it, rename it to f" commit f. No reply.
 */
void rename_fs_handler(const char *const str);

/* ---------------------------------------------------------------- rmdir */

void rmdir_handler(const char *const str);

/* ---------------------------------------------------------------- seek */

void handle_lseek(int tid, int fd, off64_t offset);

void handle_seek_data(int tid, int fd, off64_t offset);

void handle_seek_end(int tid, int fd);

void handle_seek_hole(int tid, int fd, off64_t offset);

void lseek_handler(const char *const str);

void seek_data_handler(const char *const str);

void seek_end_handler(const char *const str);

void seek_hole_handler(const char *const str);

/* ---------------------------------------------------------------- stat */

void wait_for_file_completion(int tid, const std::filesystem::path &path);

void reply_stat(int tid, const std::filesystem::path &path);

void fstat_handler(const char *const str);

void stat_handler(const char *const str);

/*
 * stat, access, chdir: wait for the path to exist, as open does, and then, for a declared path in
 * update mode, for its commit. The reply is 1 if it holds from now on, so that the client can cache
 * it, and 0 if it only holds now (the path is missing, or is not declared).
 */
void consent_handler(const char *const str);

/* ---------------------------------------------------------------- unlink */

void unlink_handler(const char *const str);

/* ---------------------------------------------------------------- write */

void write_handler(const char *const str);

#endif // CAPIO_SERVER_HANDLERS_HPP
