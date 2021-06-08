#ifndef CAPIO_BROKER_HPP_
#define CAPIO_BROKER_HPP_

#include "files_table.hpp"
#include <dirent.h>
#include <cstring>
class capio_broker {
	private:
		files_table ftable;
	public:
		int mknod(const char*path, mode_t mode, dev_t dev) {
			return ftable.mknod(path, mode, dev);
		}

		int open(const char* path, int flags, ...) {
			return ftable.open(path);
		}
		
		int close(int fd) {
			return ftable.close(fd);
		}

		int lstat(const char *path, struct stat *statbuf) {
			return ftable.get_metadata(path, statbuf);
		}

		int read(int fd, char* buf, size_t nbytes) {
			return ftable.read(fd, buf, nbytes);
		}
		
		int write(int fd, const char* buf, size_t nbytes) {
			return ftable.write(fd, buf, nbytes);
		}

		int opendir(const char* path) {
			return ftable.opendir(path);
		}
		
		int closedir(int fd) {
			return ftable.closedir(fd);
		}

		/*
		 *Read the metadata of the next file in the directory represented by fd
		 */


		struct dirent*  readdir(int fd) {
			return ftable.readdir(fd);	
		}
};

#endif
