#ifndef CAPIO_SERVER_HANDLERS_HPP
#define CAPIO_SERVER_HANDLERS_HPP

#include <filesystem>

void access_handler(const char *const str);
void clone_handler(const char *const str);
void close_handler(const char *str);
void dup_handler(const char *const str);
void exit_group_handler(const char *const str);
void getdents_handler(const char *const str);
void handshake_named_handler(const char *const str);
void handshake_anonymous_handler(const char *const str);
void mkdir_handler(const char *const str);
void open_handler(const char *const str);
void create_exclusive_handler(const char *const str);
void create_handler(const char *const str);
void read_handler(const char *const str);
void rename_handler(const char *const str);
void rmdir_handler(const char *const str);
void seek_hole_handler(const char *const str);
void seek_end_handler(const char *const str);
void seek_data_handler(const char *const str);
void lseek_handler(const char *const str);
void stat_handler(const char *const str);
void fstat_handler(const char *const str);
void unlink_handler(const char *const str);
void write_handler(const char *const str);

/// Function required by other handlers.
/// TODO: Remove this functions and move them to dedicated classes

inline void handle_close(int tid, int fd);
inline void reply_stat(int tid, const std::filesystem::path &path);

#endif // CAPIO_SERVER_HANDLERS_HPP
