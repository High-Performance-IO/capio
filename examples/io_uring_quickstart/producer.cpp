// Producer step: writes a few messages to a file with io_uring, one 4 KiB record each.
// It is ordinary liburing code; CAPIO is added only at launch time (see run.sh).
#include <liburing.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

static constexpr size_t kRecord = 4096; // one message per record, see run.sh

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <file> [messages]\n", argv[0]);
        return 2;
    }
    const int messages = argc > 2 ? std::atoi(argv[2]) : 5;

    int fd = open(argv[1], O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        std::perror("producer: open");
        return 1;
    }
    io_uring ring{};
    if (int rc = io_uring_queue_init(8, &ring, 0); rc < 0) {
        std::fprintf(stderr, "producer: io_uring_queue_init: %s\n", std::strerror(-rc));
        return 1;
    }

    off_t offset = 0;
    for (int i = 1; i <= messages; ++i) {
        char record[kRecord] = {};
        std::snprintf(record, sizeof(record), "message %d of %d", i, messages);

        io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_write(sqe, fd, record, kRecord, offset);
        io_uring_submit(&ring);

        io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);
        if (cqe->res != static_cast<int>(kRecord)) {
            std::fprintf(stderr, "producer: write failed: %s\n",
                         cqe->res < 0 ? std::strerror(-cqe->res) : "short write");
            return 1;
        }
        io_uring_cqe_seen(&ring, cqe);
        offset += kRecord;

        std::printf("producer: wrote   %s\n", record);
        std::fflush(stdout);
        sleep(1); // give the consumer time to show that it reads while we write
    }

    io_uring_queue_exit(&ring);
    close(fd);
    std::printf("producer: closed the file\n");
    return 0;
}
