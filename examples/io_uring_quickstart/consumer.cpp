// Consumer step: reads the producer's 4 KiB records with io_uring until end of file.
// Under CAPIO it can start before the producer: each read waits for data, and
// end of file comes only when the producer closes the file (see capio_config.json).
#include <liburing.h>

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

static constexpr size_t kRecord = 4096; // same record size as the producer

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return 2;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        std::perror("consumer: open");
        return 1;
    }
    io_uring ring{};
    if (int rc = io_uring_queue_init(8, &ring, 0); rc < 0) {
        std::fprintf(stderr, "consumer: io_uring_queue_init: %s\n", std::strerror(-rc));
        return 1;
    }

    char record[kRecord + 1] = {};
    size_t have  = 0; // bytes of the current record read so far
    off_t offset = 0;
    while (true) {
        io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_read(sqe, fd, record + have, kRecord - have, offset);
        io_uring_submit(&ring);

        io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);
        int res = cqe->res;
        io_uring_cqe_seen(&ring, cqe);
        if (res < 0) {
            std::fprintf(stderr, "consumer: read failed: %s\n", std::strerror(-res));
            return 1;
        }
        if (res == 0) {
            break; // end of file: the producer closed the file
        }
        offset += res;
        have += res;

        if (have == kRecord) {
            std::printf("consumer: read    %s\n", record);
            std::fflush(stdout);
            have = 0;
        }
    }

    io_uring_queue_exit(&ring);
    close(fd);
    std::printf("consumer: end of file after %ld bytes\n", static_cast<long>(offset));
    return 0;
}
