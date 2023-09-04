//
// Created by marco on 12/09/23.
//

#ifndef CAPIO_UTIL_PRODUCER_HPP
#define CAPIO_UTIL_PRODUCER_HPP
std::string get_producer_name(std::string path) {
    std::string producer_name = "";
    //we handle also prefixes
    auto it_metadata = metadata_conf.find(path);
    if (it_metadata == metadata_conf.end()) {
        long int pos = match_globs(path, &metadata_conf_globs);
        if (pos != -1) {
            producer_name = std::get<3>(metadata_conf_globs[pos]);
#ifdef CAPIOLOG
            logfile << "pos " << pos << " producer_name " << producer_name << std::endl;
#endif
        }
    }
    else {
        producer_name = std::get<2>(it_metadata->second);
#ifdef CAPIOLOG
        logfile << "get producer_name " << producer_name << std::endl;
#endif
    }
    return producer_name;
}

bool is_producer(int tid, std::string path) {
    bool res = false;
    auto it = apps.find(tid);
#ifdef CAPIOLOG
    logfile << "is producer " << path << std::endl;
#endif
    if (it != apps.end()) {
        std::string app_name = apps[tid];
        std::string prod_name = get_producer_name(path);
#ifdef CAPIOLOG
        logfile << "app_name " << app_name << " prod_name " << prod_name << std::endl;
#endif
        res = app_name == prod_name;
    }
    return res;
}
#endif //CAPIO_UTIL_PRODUCER_HPP
