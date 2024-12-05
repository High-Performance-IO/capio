#ifndef CONSENT_REQUEST_CACHE_HPP
#define CONSENT_REQUEST_CACHE_HPP
class ConsentRequestCache {

    std::unordered_map<std::string, capio_off64_t> *available_consent;

    // Block until server allows for proceeding to a generic request
    static inline capio_off64_t _consent_to_proceed_request(const std::filesystem::path &path,
                                                            const long tid,
                                                            const std::string &source_func) {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld, source_func=%s)", path.c_str(),
                  tid, source_func.c_str());
        char req[CAPIO_REQ_MAX_SIZE];
        sprintf(req, "%04d %ld %s %s", CAPIO_REQUEST_CONSENT, tid, path.c_str(),
                source_func.c_str());
        buf_requests->write(req, CAPIO_REQ_MAX_SIZE);
        capio_off64_t res;
        bufs_response->at(tid)->read(&res);
        LOG("Obtained from server %llu", res);
        return res;
    }

  public:
    explicit ConsentRequestCache() {
        available_consent = new std::unordered_map<std::string, capio_off64_t>;
    };

    ~ConsentRequestCache() { delete available_consent; };

    void consent_request(const std::filesystem::path &path, long tid,
                         const std::string &source_func) const {
        START_LOG(capio_syscall(SYS_gettid), "call(path=%s, tid=%ld, source=%s)", path.c_str(), tid,
                  source_func.c_str());

        /**
         * If entry is not present in cache, then proceed to perform request. othrewise if present,
         * there is no need to perform request to server and can proceed
         */
        if (available_consent->find(path) == available_consent->end()) {
            LOG("File not present in cache. performing request");
            auto res = ConsentRequestCache::_consent_to_proceed_request(path, tid, source_func);
            LOG("Registering new file for consent to proceed");
            available_consent->emplace(path, res);
        }
        LOG("Unlocking thread");
    }
};

#endif // CONSENT_REQUEST_CACHE_HPP
