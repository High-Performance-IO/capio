#ifndef SRC_CAPIO_POSIX_GLOBALS_H
#define SRC_CAPIO_POSIX_GLOBALS_H

#include <filesystem>
#include <set>
#include <string>

#include <libsyscall_intercept_hook_point.h>
#include <semaphore.h>
#include <syscall.h>

#include "capio/constants.hpp"
#include "capio/filesystem.hpp"

#include "utils/env.hpp"
#include "utils/logger.hpp"
#include "utils/requests.hpp"
#include "utils/snapshot.hpp"
#include "utils/types.hpp"

std::string *capio_dir = nullptr;

std::string *current_dir = nullptr;

CPSemsWrite_t *sems_write = nullptr;

CPFileDescriptors_t *capio_files_descriptors = nullptr;
CPFilesPaths_t *capio_files_paths = nullptr;
CPFiles_t *files = nullptr;

CPStatEnabled_t *stat_enabled = nullptr; //TODO: protect with a semaphore
CPThreadDataBufs_t *threads_data_bufs = nullptr;


void mtrace_init(long tid) {

  CAPIO_DBG("mtrace_init CHILD_TID[%ld]: enter\n", tid);

    if (stat_enabled == nullptr) {
        stat_enabled = new std::unordered_map<long int, bool>;
    }

  (*stat_enabled)[tid] = false;

  if (capio_files_descriptors == nullptr) {

    CAPIO_DBG("mtrace_init CHILD_TID[%ld]: init data structures\n", tid);

    capio_files_descriptors = new std::unordered_map<int, std::string>;
    capio_files_paths = new std::unordered_set<std::string>;

    files = new std::unordered_map<int, std::tuple<off64_t*, off64_t*, int, int>>;

    int* fd_shm = get_fd_snapshot(tid);
    if (fd_shm != nullptr) {
      initialize_from_snapshot(fd_shm, files, capio_files_descriptors, capio_files_paths, tid);
    }
    threads_data_bufs = new std::unordered_map<int, std::pair<SPSC_queue<char>*, SPSC_queue<char>*>>;
    std::string shm_name = "capio_write_data_buffer_tid_" + std::to_string(tid);
    auto* write_queue = new SPSC_queue<char>(shm_name, N_ELEMS_DATA_BUFS, WINDOW_DATA_BUFS);
    shm_name = "capio_read_data_buffer_tid_" + std::to_string(tid);
    auto* read_queue = new SPSC_queue<char>(shm_name, N_ELEMS_DATA_BUFS, WINDOW_DATA_BUFS);
    threads_data_bufs->insert({tid, {write_queue, read_queue}});
  }
  char* val;
  if (capio_dir == nullptr) {
    val = getenv("CAPIO_DIR");
    try {
      if (val == nullptr) {
        capio_dir = new std::string(std::filesystem::canonical("."));
      }
      else {
        capio_dir = new std::string(std::filesystem::canonical(val));
      }
      current_dir = new std::string(*capio_dir);
    }
    catch (const std::exception& ex) {
      exit(1);
    }
    int res = is_directory(capio_dir->c_str());
    if (res == 0) {
      std::cerr << "dir " << capio_dir << " is not a directory" << std::endl;
      exit(1);
    }
  }

  CAPIO_DBG("mtrace_init CHILD_TID[%ld]: CAPIO_DIR is set to %s\n", tid, capio_dir->c_str());

  if (sems_write == nullptr)
    sems_write = new std::unordered_map<int, sem_t*>();
  sem_t* sem_write = sem_open(("sem_write" + std::to_string(tid)).c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, 0);
  sems_write->insert(std::make_pair(tid, sem_write));

  CAPIO_DBG("mtrace_init CHILD_TID[%ld]: register thread to make requests\n", tid);

  register_thread(tid);

  CAPIO_DBG("mtrace_init CHILD_TID[%ld]: perform server handshake\n", tid);

  const char* capio_app_name = get_capio_app_name();
  long pid = syscall_no_intercept(SYS_getpid);
  if (capio_app_name == nullptr)
    handshake_anonymous_request(tid, pid);
  else
    handshake_named_request(tid, pid, capio_app_name);
  (*stat_enabled)[tid] = true;

  CAPIO_DBG("mtrace_init CHILD_TID[%ld]: return\n", tid);
}

#endif // SRC_CAPIO_POSIX_GLOBALS_H
