#ifndef CONSENT_REQUEST_CACHE_HPP
#define CONSENT_REQUEST_CACHE_HPP

class ConsentRequestCache {
    std::unordered_map<std::string, capio_off64_t> *available_consent;

    // Block until the server allows for proceeding to a generic request
    static capio_off64_t _consent_to_proceed_request(const std::filesystem::path &path,
                                                     const long tid,
                                                     const std::string &source_func) {
        START_LOG(tid, "call(path=%s, tid=%ld, source_func=%s)", path.c_str(), tid,
                  source_func.c_str());
        char req[CAPIO_REQ_MAX_SIZE];
        sprintf(req, "%04d %ld %s %s", CAPIO_REQUEST_CONSENT, tid, path.c_str(),
                source_func.c_str());
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
        capio_off64_t res = bufs_response->at(tid)->read();
        LOG("Obtained from server %llu", res);
        return res;
    }

  public:
    explicit ConsentRequestCache() {
        available_consent = new std::unordered_map<std::string, capio_off64_t>;
    };

    ~ConsentRequestCache() {
        START_LOG(capio_current_thread_id, "call()");
        delete available_consent;
    };

    void consent_request(const std::filesystem::path &path, long tid,
                         const std::string &source_func) const {
        START_LOG(tid, "call(path=%s, tid=%ld, source=%s)", path.c_str(), tid, source_func.c_str());

        const auto resolved_path = resolve_possible_symlink(path);

        if (!is_capio_path(resolved_path)) {
            LOG("PATH is forbidden. Skipping request!");
            return;
        }

        /**
         * If entry is not present in cache, then proceed to perform request. othrewise if present,
         * there is no need to perform request to server and can proceed
         */
        if (!available_consent->contains(resolved_path)) {
            LOG("File not present in cache. performing request");
            auto res = _consent_to_proceed_request(resolved_path, tid, source_func);
            LOG("Registering new file for consent to proceed");
            available_consent->emplace(resolved_path, res);
        }
        LOG("Unlocking thread");
    }
};

#endif // CONSENT_REQUEST_CACHE_HPP