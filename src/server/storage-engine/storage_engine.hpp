#ifndef STORAGE_ENGINE_HPP
#define STORAGE_ENGINE_HPP
#include <mutex>

#include "src/capio_file.hpp"

class StorageEngine {
  private:
    // path -> CapioFile (only for metadata)
    std::unordered_map<std::string, CapioFile *> *open_metadata_descriptors;

    // path -> [tids waiting for response on path]
    std::unordered_map<std::string, std::vector<long> *> *pending_requests_on_file;

    // tid, path  -> number of open on file (used to know opened and how many times a file has been
    // open by a thread)
    std::unordered_map<long, std::unordered_map<std::string, long> *> *threads_opened_files;

    std::mutex metadata_mutex;

    /**
     * check if metadata associated with a given file exists.
     * If it exists, then there is an assumption that the associated file is present
     * @param path
     * @return
     */
    static bool exists(std::filesystem::path &path) {
        std::filesystem::path check = path.string() + ".capio";
        return std::filesystem::exists(check);
    }

    void unlock_awaiting_threads_for_commit(const std::filesystem::path &path) const {
        if (open_metadata_descriptors->at(path)->is_committed()) {
            auto queue = pending_requests_on_file->at(path);
            for (long tid : *queue) {
                client_manager->reply_to_client(tid, 1);
            }
        }
    }

  public:
    StorageEngine() {
        open_metadata_descriptors = new std::unordered_map<std::string, CapioFile *>;
        pending_requests_on_file  = new std::unordered_map<std::string, std::vector<long> *>;
        threads_opened_files =
            new std::unordered_map<long, std::unordered_map<std::string, long> *>;
    }

    ~StorageEngine() { delete open_metadata_descriptors; }

    void create_capio_file(std::filesystem::path filename, long tid) {
        std::lock_guard<std::mutex> lg(metadata_mutex);
        if (open_metadata_descriptors->find(filename) == open_metadata_descriptors->end()) {
            open_metadata_descriptors->emplace(filename, new CapioFile(filename));
        }

        // register a new open on a file
        if (threads_opened_files->find(tid) == threads_opened_files->end()) {
            threads_opened_files->emplace(tid, new std::unordered_map<std::string, long>);
        }

        if (threads_opened_files->at(tid)->find(filename) == threads_opened_files->at(tid)->end()) {
            threads_opened_files->at(tid)->emplace(filename, 1);
        } else {
            threads_opened_files->at(tid)->at(filename) =
                threads_opened_files->at(tid)->at(filename) + 1;
        }
    }

    void deleteFile(std::filesystem::path path) {
        std::lock_guard<std::mutex> lg(metadata_mutex);

        std::filesystem::path metadata_name = path.string() + ".capio";
        std::filesystem::remove(metadata_name);
        std::filesystem::remove(path);
        open_metadata_descriptors->erase(open_metadata_descriptors->find(path));
    }

    auto get_metadata(std::filesystem::path &filename) {
        std::lock_guard<std::mutex> lg(metadata_mutex);
        auto metadata = open_metadata_descriptors->at(filename)->get_metadata();
        unlock_awaiting_threads_for_commit(
            filename); // if committed unlock threads waiting to continue
        return metadata;
    }

    void update_metadata(std::filesystem::path &filename, long filesize, long n_close,
                         bool committed) {
        std::lock_guard<std::mutex> lg(metadata_mutex);
        open_metadata_descriptors->at(filename)->update_metadata(filesize, n_close, committed);
        if (committed) {
            unlock_awaiting_threads_for_commit(filename);
        }
    }

    void update_size(std::filesystem::path &filename, long size) {
        std::lock_guard<std::mutex> lg(metadata_mutex);

        open_metadata_descriptors->at(filename)->update_size(size);
    }

    void update_n_close(std::filesystem::path &filename, long n_close) {
        std::lock_guard<std::mutex> lg(metadata_mutex);

        open_metadata_descriptors->at(filename)->update_n_close(n_close);
    }

    void set_committed(std::filesystem::path &filename) const {
        open_metadata_descriptors->at(filename)->set_committed();
        unlock_awaiting_threads_for_commit(filename);
    }

    bool is_committed(std::filesystem::path &filename, long tid) {
        this->create_capio_file(filename, tid);
        return open_metadata_descriptors->at(filename)->is_committed();
    }

    void add_thread_awaiting_for_commit(const std::filesystem::path &filename, long tid) const {
        if (pending_requests_on_file->find(filename) == pending_requests_on_file->end()) {
            pending_requests_on_file->emplace(filename, new std::vector<long>);
        }
        pending_requests_on_file->at(filename)->emplace_back(tid);
    }

    void close_all_files(int tid) {
        auto map = threads_opened_files->at(tid);

        for (auto entry : *map) {
            std::filesystem::path filename(entry.first);
            long opens = entry.second;
            update_n_close(filename, opens);
            // TODO: check if is committed
        }

        threads_opened_files->erase(threads_opened_files->find(tid));
    }
};

StorageEngine *storage_engine;

#endif // STORAGE_ENGINE_HPP
