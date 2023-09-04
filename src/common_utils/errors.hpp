#ifndef CAPIO_ERRORS_HPP
#define CAPIO_ERRORS_HPP

static inline void err_exit(std::string error_msg,std::ostream& outstream = std::cerr) {
    outstream << "error: " << error_msg << " errno " <<  errno << " strerror(errno): " << strerror(errno) << std::endl;
    exit(1);
}

static inline void err_exit(std::string error_msg, std::string invoker, std::ostream& outstream = std::cerr) {
    outstream << "error: at " <<invoker <<", " << error_msg << " errno " <<  errno << " strerror(errno): " << strerror(errno) << std::endl;
    exit(1);
}


#endif //CAPIO_ERRORS_HPP
