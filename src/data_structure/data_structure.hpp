#ifndef CAPIO_DATA_STRUCTURE_HPP
#define CAPIO_DATA_STRUCTURE_HPP


struct linux_dirent {
    unsigned long  d_ino;
    off_t          d_off;
    unsigned short d_reclen;
    char           d_name[DNAME_LENGTH + 2];
};

struct linux_dirent64 {
    ino64_t  	   d_ino;
    off64_t        d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char           d_name[DNAME_LENGTH + 1];
};

struct spinlock {
    std::atomic<bool> lock_ = {0};

    void lock() noexcept {
        for (;;) {
            // Optimistically assume the lock is free on the first try
            if (!lock_.exchange(true, std::memory_order_acquire)) {
                return;
            }
            // Wait for lock to be released without generating cache misses
            while (lock_.load(std::memory_order_relaxed)) {
                // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
                // hyper-threads
                __builtin_ia32_pause();
            }
        }
    }

    bool try_lock() noexcept {
        // First do a relaxed load to check if lock is free in order to prevent
        // unnecessary cache misses if someone does while(!try_lock())
        return !lock_.load(std::memory_order_relaxed) &&
               !lock_.exchange(true, std::memory_order_acquire);
    }

    void unlock() noexcept {
        lock_.store(false, std::memory_order_release);
    }
};


struct remote_n_files {
    char* prefix;
    std::size_t n_files;
    int dest;
    std::vector<std::string>* files_path;
    sem_t* sem;

};

struct handle_write_metadata{
    char str[64];
    long int rank;
};

struct wait_for_file_metadata{
    int tid;
    int fd;
    bool dir;
    bool is_getdents;
    off64_t count;
};

struct wait_for_stat_metadata{
    int tid;
    char path[PATH_MAX];
};

struct remote_read_metadata {
    char path[PATH_MAX];
    long int offset;
    int dest;
    long int nbytes;
    bool* complete;
    sem_t* sem;
};

struct remote_stat_metadata {
    char* path;
    Capio_file* c_file;
    int dest;
    sem_t* sem;
};


#endif //CAPIO_DATA_STRUCTURE_HPP
