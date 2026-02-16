#ifndef CAPIO_REQUEST_HPP
#define CAPIO_REQUEST_HPP
#include "common/requests.hpp"
#include "utils/types.hpp"

class ClientRequestManager {

    typedef void (*CSHandler_t)(const char *const);

    struct MemHandlers {
        static void access_handler(const char *const str);
        static void clone_handler(const char *const str);
        static void close_handler(const char *const str);
        static void create_handler(const char *const str);
        static void create_exclusive_handler(const char *const str);
        static void dup_handler(const char *const str);
        static void fstat_handler(const char *const str);
        static void getdents_handler(const char *const str);
        static void mkdir_handler(const char *const str);
        static void open_handler(const char *const str);
        static void read_handler(const char *const str);
        static void rename_handler(const char *const str);
        static void rmdir_handler(const char *const str);
        static void lseek_handler(const char *const str);
        static void seek_data_handler(const char *const str);
        static void seek_end_handler(const char *const str);
        static void seek_hole_handler(const char *const str);
        static void stat_handler(const char *const str);
        static void unlink_handler(const char *const str);
        static void write_handler(const char *const str);
    };

    struct Handlers {
        static void handshake_named_handler(const char *const str);
        static void exit_group_handler(const char *const str);
    };

    struct ClientUtilities {
        static void reply_stat(int tid, const std::filesystem::path &path);
        static void handle_close(int tid, int fd);
        static void handle_exit_group(int fd);
        static void handle_seek_end(int tid, int fd);
    };

    const std::array<CSHandler_t, CAPIO_NR_REQUESTS> request_handlers;

    static constexpr std::array<CSHandler_t, CAPIO_NR_REQUESTS> build_request_handlers_table();

  public:
    ClientRequestManager();
    void start() const;
};

#endif // CAPIO_REQUEST_HPP
