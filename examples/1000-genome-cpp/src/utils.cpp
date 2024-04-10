#include "utils.hpp"

#include <iostream>
#include <vector>

#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>

void copy_file(struct archive *in_ar, struct archive *out_ar) {
    const size_t buff_size = 80 * 1024;
    const void *buffer = new char[buff_size];
    const void *buff;
    size_t bytes_read;
    ssize_t bytes_written;
    int64_t offset, progress = 0;
    int res;

    while ((res = archive_read_data_block(in_ar, &buff, &bytes_read, &offset)) == ARCHIVE_OK) {
        if (offset > progress) {
            int64_t sparse = offset - progress;
            size_t ns;

            while (sparse > 0) {
                if (sparse > (int64_t) buff_size) {
                    ns = buff_size;
                } else {
                    ns = (size_t) sparse;
                }
                if ((bytes_written = archive_write_data(out_ar, buffer, ns)) < ns) {
                    std::cerr << "Error in archive_write_data() while building tar: "
                              << archive_error_string(out_ar) << std::endl;
                    exit(EXIT_FAILURE);
                }
                progress += bytes_written;
                sparse -= bytes_written;
            }
        }
        if ((bytes_written = archive_write_data(out_ar, buff, bytes_read)) < bytes_read) {
            std::cerr << "Error in archive_write_data() while building tar: "
                      << archive_error_string(out_ar) << std::endl;
            exit(EXIT_FAILURE);
        }
        progress += bytes_written;
    }
    if (res < ARCHIVE_WARN) {
        std::cerr << "Error in archive_read_data_block() while building tar: "
                  << archive_error_string(out_ar) << std::endl;
        exit(EXIT_FAILURE);
    }
}


void create_tar_archive(const std::filesystem::path &in_path, const std::filesystem::path &out_path) {
    create_tar_archive(std::vector{in_path}, out_path);
}


void create_tar_archive(const std::vector<std::filesystem::path> &in_paths, const std::filesystem::path &out_path) {
    struct archive *tar_archive = archive_write_new();
    archive_write_add_filter_gzip(tar_archive);
    archive_write_set_format_ustar(tar_archive);
    archive_write_set_bytes_per_block(tar_archive, 20 * 512);
    archive_write_open_filename(tar_archive, (out_path).c_str());
    struct archive *disk = archive_read_disk_new();
    archive_read_disk_set_standard_lookup(disk);
    for (const std::filesystem::path &in_path: in_paths) {
        if (archive_read_disk_open(disk, in_path.c_str()) != ARCHIVE_OK) {
            std::cerr << "Error in archive_read_disk_open() while building tar: "
                      << archive_error_string(disk) << std::endl;
            exit(EXIT_FAILURE);
        }
        struct archive_entry *entry;
        while (true) {
            entry = archive_entry_new();
            int res = archive_read_next_header2(disk, entry);
            if (res == ARCHIVE_EOF) {
                break;
            } else if (res != ARCHIVE_OK) {
                std::cerr << "Error in archive_read_next_header2() while building tar: "
                          << archive_error_string(disk) << std::endl;
                exit(EXIT_FAILURE);
            }
            archive_read_disk_descend(disk);
            if (!std::filesystem::equivalent(out_path, archive_entry_pathname(entry))) {
                res = archive_write_header(tar_archive, entry);
                if (res > ARCHIVE_FAILED) {
                    copy_file(disk, tar_archive);
                } else {
                    std::cerr << "Error in archive_write_header() while building tar: "
                              << archive_error_string(tar_archive) << std::endl;
                    exit(EXIT_FAILURE);
                }
            }
            archive_entry_free(entry);
        }
        archive_read_close(disk);
    }
    archive_read_free(disk);
    archive_write_close(tar_archive);
    archive_write_free(tar_archive);
}

void extract_tar_archive(const std::filesystem::path &in_path, const std::filesystem::path &out_path) {
    struct archive *tar_archive = archive_read_new();
    archive_read_support_format_all(tar_archive);
    archive_read_support_filter_gzip(tar_archive);
    if (archive_read_open_filename(tar_archive, in_path.c_str(), 20 * 512)) {
        std::cerr << "Error in archive_read_open_filename() while extracting tar: "
                  << archive_error_string(tar_archive) << std::endl;
        exit(EXIT_FAILURE);
    }
    struct archive_entry *entry;
    while (true) {
        int res = archive_read_next_header(tar_archive, &entry);
        if (res == ARCHIVE_EOF) {
            break;
        } else if (res == ARCHIVE_RETRY) {
            continue;
        } else if (res < ARCHIVE_OK) {
            std::cerr << "Error in archive_read_next_header() while extracting tar: "
                      << archive_error_string(tar_archive) << std::endl;
            exit(EXIT_FAILURE);
        }
        std::filesystem::path entry_path = out_path / std::filesystem::path(archive_entry_pathname(entry));
        int fd = open(entry_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (archive_read_data_into_fd(tar_archive, fd) != ARCHIVE_OK) {
            std::cerr << "Error in archive_read_extract2() while extracting tar: "
                      << archive_error_string(tar_archive) << " (errno " << archive_errno(tar_archive)
                      << ")" << std::endl;
            exit(EXIT_FAILURE);
        }
        close(fd);
    }
    archive_read_close(tar_archive);
    archive_read_free(tar_archive);
}
