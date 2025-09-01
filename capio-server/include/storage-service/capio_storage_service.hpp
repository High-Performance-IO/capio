#ifndef CAPIO_STORAGE_SERVICE_H
#define CAPIO_STORAGE_SERVICE_H

#include <capio/env.hpp>
#include <include/capio-cl-engine/capio_cl_engine.hpp>
#include <include/storage-service/capio_file.hpp>

class CapioStorageService {
    // TODO: put all of this conde on a different thread

    std::unordered_map<pid_t, SPSCQueue *> *_client_to_server_queue;
    std::unordered_map<pid_t, SPSCQueue *> *_server_to_client_queue;
    std::unordered_map<std::string, CapioFile *> *_stored_files;

    std::unordered_map<std::string, std::vector<std::tuple<capio_off64_t, capio_off64_t, pid_t>>>
        *_threads_waiting_for_memory_data;

    /**
     * Return a file if exists. if not, create it and then return it
     * @param file_name
     * @return
     */
    [[nodiscard]] auto getFile(const std::string &file_name) const;

  public:
    CapioStorageService();

    ~CapioStorageService();

    void createMemoryFile(const std::string &file_name) const;

    /**
     * Create a CapioRemoteFile, after checking that an instance of CapioMemoryFile (meaning a local
     * file) is not present
     * @param file_name file path
     * @param home_node
     */
    void createRemoteFile(const std::string &file_name, const std::string &home_node) const;

    void deleteFile(const std::string &file_name) const;

    /**
     * Notify the occurrence of an operation on a given file, for which other servers running at a
     * certain point might be required to know. This function is used to allow CAPIO to function in
     * the event that a distributed file system (or at least a shared directory) is not available
     */
    void notifyEvent(const std::string &event_name, const std::filesystem::path &filename) const;

    /**
     * Add a new thread in the list of thhreads awaiting for expected_size to be available
     * @param tid
     * @param path
     * @param offset
     * @param  size
     */
    void addThreadWaitingForData(pid_t tid, const std::string &path, capio_off64_t offset,
                                 capio_off64_t size) const;

    void check_and_unlock_thread_awaiting_data(const std::string &path);

    /**
     * Return size of given path as contained inside memory
     * @param path
     * @return
     */
    size_t sizeOf(const std::string &path) const;

    /**
     * Initialize a new client data structure
     * @param app_name
     * @param pid
     */
    void register_client(const std::string &app_name, pid_t pid) const;

    /**
     * Send the file content to a client application
     * @param pid
     * @param file
     * @param offset
     * @param size
     */
    void reply_to_client(pid_t pid, const std::string &file, capio_off64_t offset,
                         capio_off64_t size) const;

    /**
     * Send raw data to client without fetching from the storage manager itself
     * @param pid
     * @param data
     * @param len
     */
    void reply_to_client_raw(pid_t pid, const char *data, capio_off64_t len) const;

    /**
     * Receive the file content from the client application
     * @param tid
     * @param file
     * @param offset
     * @param size
     */
    void recive_from_client(pid_t tid, const std::string &file, capio_off64_t offset,
                            off64_t size) const;

    /**
     * Return a list of regex used to match files that need to be stored inside memory first
     * to a posix application
     * @param pid
     * @return
     */
    [[nodiscard]] size_t sendFilesToStoreInMemory(long pid) const;

    void remove_client(pid_t pid) const;
};

inline CapioStorageService *storage_service;

#endif // CAPIO_STORAGE_SERVICE_H