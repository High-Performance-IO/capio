#ifndef STORAGE_HPP
#define STORAGE_HPP

/**
 * Check if the given path needs to be stored in memory.
 * Request is sent lazily
 */
inline bool store_file_in_memory(const std::filesystem::path &file_name, const long pid) {
    START_LOG(capio_syscall(SYS_gettid), "call(path=%s, pid=%ld)", file_name.c_str(), pid);
    if (paths_to_store_in_memory == nullptr) {
        /*
        * Request which files need to be handled in memory instead of file system
        */
        LOG("Vector is empty and not allocated. performing request");
        paths_to_store_in_memory = file_in_memory_request(pid);
    }

    return std::any_of(
        paths_to_store_in_memory->begin(), paths_to_store_in_memory->end(),
        [file_name](const std::regex &rx) { return std::regex_match(file_name.string(), rx); });
}

#endif // STORAGE_HPP