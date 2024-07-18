#ifndef CAPIO_FILE_HPP
#define CAPIO_FILE_HPP

#include "utils/distributed_semaphore.hpp"

// TODO: concurrency is kinda guaranteed but more checks needs to be performed.
// TODO: as writes are below PIPE_MAX the distributed semaphore might not be needed

class CapioFile {
  private:
    std::string name;
    std::string metadataname;
    FILE *metadata_file_fd = nullptr;
    long _size = 0, _n_close = 0;
    int _committed = 0;

  public:
    explicit CapioFile(std::filesystem::path filename) : name(filename) {
        metadataname = name + ".capio";
        metadata_file_fd =
            fopen(metadataname.c_str(),
                  "w+"); // open (or create if not exists) metadata associated with file
    }

    ~CapioFile() { fclose(metadata_file_fd); }

    /**
     *  Query metadata related to the descirbed file
     * @return a tuple of <filesize, number of close, if it is committed or not>
     */
    std::tuple<long, long, double> get_metadata() {

        fseek(metadata_file_fd, 0, SEEK_SET);
        fscanf(metadata_file_fd, "%ld %ld %d", &_size, &_n_close, &_committed);

        return {_size, _n_close, _committed};
    }

    void update_metadata(long filesize, long n_close, bool committed) {
        DistributedSemaphore lock(metadataname, 100);
        get_metadata();

        if (filesize != _size) {
            _size = filesize;
        }
        if (n_close != _n_close) {
            _n_close = n_close;
        }
        if (committed != _committed) {
            _committed = committed;
        }

        fseek(metadata_file_fd, 0, SEEK_SET);
        fprintf(metadata_file_fd, "%ld %ld %d", filesize, n_close, committed ? 1 : 0);
    }

    void update_size(long size) { update_metadata(size, _n_close, _committed); }

    void update_n_close(long n_close) { update_metadata(_size, n_close, _committed); }

    void set_committed() { update_metadata(_size, _n_close, _committed); }

    bool is_committed() {
        get_metadata();
        return _committed;
    }
};

#endif // CAPIO_FILE_HPP
