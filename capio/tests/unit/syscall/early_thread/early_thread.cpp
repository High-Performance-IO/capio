#include "early_thread.hpp"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

/*
 * A thread started by the constructor of a library, as UCX starts its fuse thread. The test binary
 * links this library, and the dynamic loader runs the constructors of the libraries a program
 * links before the one of a preloaded library: so the thread exists before libcapio is set up, and
 * never goes through its clone hook. It waits for a job from the test, and does it with its first
 * system calls after the wait, which libcapio sees from a thread it does not know.
 */
namespace {

struct Job {
    const char *path = nullptr;
    char *buffer     = nullptr;
    size_t size      = 0;
    long result      = 0;
};

sem_t job_ready;
sem_t job_done;
Job job;
bool started = false;
bool used    = false;

void wait_for(sem_t *semaphore) {
    while (sem_wait(semaphore) == -1 && errno == EINTR) {
    }
}

void *early_thread(void *) {
    wait_for(&job_ready);
    const int fd = open(job.path, O_RDONLY);
    if (fd < 0) {
        job.result = -errno;
    } else {
        const ssize_t count = read(fd, job.buffer, job.size);
        job.result          = count < 0 ? -errno : count;
        close(fd);
    }
    sem_post(&job_done);
    return nullptr;
}

__attribute__((constructor)) void start_early_thread() {
    if (sem_init(&job_ready, 0, 0) != 0 || sem_init(&job_done, 0, 0) != 0) {
        return;
    }
    pthread_t thread;
    if (pthread_create(&thread, nullptr, early_thread, nullptr) == 0) {
        pthread_detach(thread);
        started = true;
    }
}

} // namespace

long capio_test_early_thread_read(const char *path, char *buffer, const size_t size) {
    if (!started) {
        return -ESRCH;
    }
    if (used) {
        return -EALREADY;
    }
    used = true;
    job  = {path, buffer, size, 0};
    sem_post(&job_ready);
    wait_for(&job_done);
    return job.result;
}
