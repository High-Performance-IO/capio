#ifndef FILES_TABLE_HPP_
#define FILES_TABLE_HPP_

#include <tuple>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <dirent.h>
#include <time.h>
#include <utime.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <string>
#include <cstring>
#include "params.h"
#include "log.hpp"

class files_table {
	private:
		/*
		 * The key of the map is the equivalent of the file decsriptor.
		 * The value of the map is the pointer to the current region of the file. 
		 */
		std::unordered_map<int, std::tuple<char**, int, struct stat*, int*>> table;
		std::unordered_map<std::string, char*> files_mem;
		std::unordered_map<std::string, std::pair<struct stat, int>> files_metadata;
		
		std::unordered_map<int, std::pair<std::vector<struct dirent>*, int>> dirs_table;
		std::unordered_map<std::string, std::vector<struct dirent>*> dirs_mem;
		std::unordered_map<std::string, struct stat> dirs_metadata;
		int count = 3;
		int dirs_count = 0;
	public:

		//constructor

		files_table() {
			dirs_mem["/"] = new std::vector<struct dirent>();
			dirs_metadata["/"] = create_dir_metadata();
		}

		//destructor

		~files_table() {
			for (auto& pair : files_mem) {
				free(pair.second);
			}
			for (auto& pair : dirs_mem) {
				delete pair.second;
			}
		}

		char** get_pointer(int fd) {
			char** p = nullptr;
			auto it = table.find(fd);
			if (it != table.end()) 
				p = std::get<0>(it->second);	
			return p;	
		}

		int get_metadata(const char *path, struct stat *statbuf) {
			int res = 0;
			auto it = files_metadata.find(path);	
			if (it == files_metadata.end()) {
				auto dirs_it = dirs_metadata.find(path);
				if (dirs_it == dirs_metadata.end()) {
					res = -1;
					errno = ENOENT;
				}
				else 
					*statbuf = dirs_it->second;
			}
			else
				*statbuf = it->second.first;
			
			return res;
		}

		struct stat create_dir_metadata() {
			struct stat res;
			res.st_dev = 66307;
			res.st_ino = 22678271;
			res.st_mode = 040755;
			res.st_nlink = 2;
			res.st_uid = 1000;
			res.st_gid = 1000;
			res.st_rdev = 0;
			res.st_size = 4096;
			res.st_blksize = 4096;
			res.st_blocks = 8;
			res.st_atime = 0x60bddb54;
			res.st_mtime = 0x60bddfdb;
			res.st_ctime = 0x60bddfdb;
			return res;
		}

		struct stat create_metadata() {
			struct stat res;
			res.st_dev = 66307;
			res.st_ino = 22675854;
			res.st_mode = 0100644;
			res.st_nlink = 1;
			res.st_uid = 1000;
			res.st_gid = 1000;
			res.st_rdev = 0;
			res.st_size = 0;
			res.st_blksize = 4096;
			res.st_blocks = 0;
			res.st_atime = time(NULL); 
			res.st_mtime = time(NULL);
			res.st_ctime = time(NULL);
			return res;
		}

		int mknod(const char* path, mode_t mode, dev_t dev) {
			int res = 0;
			auto it = files_mem.find(path);
			if (it == files_mem.end()) {
				res = -1;
				errno = EEXIST;
			}
			else {
				char* p = (char*)malloc(128 * sizeof(char)); 
				++count;
				files_mem[std::string(path)] = p;
				//metadata
				files_metadata[std::string(path)].first = create_metadata();
				files_metadata[std::string(path)].second = 128;
				//update metadata of the directory of the file
				std::string str_path(path);
				int pos = get_pos_last_slash(str_path);
				std::string dir(str_path.substr(0, pos));
				char file_name[128];
				strcpy(file_name, path + pos);
				auto it = dirs_mem.find(dir);
				if (it == dirs_mem.end())
					dirs_mem[dir] = new std::vector<struct dirent>;
				int dirs_length = (*dirs_mem[dir]).size();
				dirs_mem[dir]->push_back(create_dirent(file_name, dirs_length));
				table[count] = std::make_tuple(&files_mem[path], 0, &files_metadata[path].first, &files_metadata[path].second);
			}
			
			return res;
		}

		struct dirent create_dirent(const char* file_name, int i) {
			struct dirent d;
			d.d_ino = 1;
			d.d_off = i;
			d.d_type = 0;
			strcpy(d.d_name, file_name);
			d.d_reclen = sizeof(d); // mmm look at the notes in the man page of readdir
			return d;
		}

		int get_pos_last_slash(std::string path) {
			std::string str_path(path);
			int i = str_path.length() - 1;
			while (i >= 0 && str_path[i] != '/')
				--i;
			return i;
		}


		/*
		 * returns the file descriptor in case of success, -1 otherwise
		 *
		 * If this is called the directory in which this file is saved
		 * already exists. This is true beacause in fuse before call the open, the
		 * function getattr is called on the directory of the file.
		 * (https://github.com/libfuse/libfuse/wiki/Invariants)
		 * TODO: handle errno and flags
		 */

		int open(const char* path) {
			int fd = count;
			char* p;
			++count;
			auto it = files_mem.find(path);
			if (it == files_mem.end()) { 
				p = (char*) malloc(128 * sizeof(char)); 
				files_mem[path] = p;
				files_metadata[path].first = create_metadata();
				files_metadata[path].second = 128;
				//update metadata of the directory of the file
				std::string str_path(path);
				int pos = get_pos_last_slash(str_path) + 1;
				std::string dir(str_path.substr(0, pos));
				char file_name[128];
				strcpy(file_name, path + pos);
				auto it = dirs_mem.find(dir);
				if (it == dirs_mem.end())
					dirs_mem[dir] = new std::vector<struct dirent>;
				int dirs_length = (*dirs_mem[dir]).size();
				(*dirs_mem[dir]).push_back(create_dirent(file_name, dirs_length));

			}
			else { 
				p = it->second;
			}
			table[fd] = std::make_tuple(&files_mem[path], 0, &files_metadata[path].first, &files_metadata[path].second);
			return fd;
		}

		/*
		 * returns 0 in case of success, 1 otherwise
		 * TODO: smart cleaning of shared memory
		 */

		int close(int fd) {
			int res = table.erase(fd);
			std::cout << "close fd " << fd << " erase " << res << std::endl;
			if (res == 0) {
				errno = EBADF;
				return 1;
			}
			return 0;
		}
		
		// TODO: opendir should fail if the dir does not exist

		int opendir(const char* path) {
			int fd = dirs_count;
			std::vector<struct dirent>* p;
			++dirs_count;
			auto it = dirs_mem.find(path);
			if (it == dirs_mem.end()) {
				p = new std::vector<struct dirent>();
				dirs_mem[path] = p;
				dirs_metadata[path] = create_dir_metadata();
			}
			else { 
				p = it->second;
			}
			dirs_table[fd] = std::pair<std::vector<struct dirent>*, int>(p, 0);
			return fd;
		}

		int closedir(int fd) {
			int nelements_deleted = dirs_table.erase(fd);
			if (nelements_deleted == 0) {
				errno = EBADF;
				return -1;
			}
			return 0;
		}

		struct dirent* readdir(int fd) {
			auto it = dirs_table.find(fd);
			struct dirent* p;
			if (it == dirs_table.end()) {
				p = NULL;
			}
			else {
				int i = it->second.second;
				if (i < it->second.first->size()) {
					p = &((*(it->second.first))[i]);
					++(it->second.second);
				}
				else 
					p = NULL;
			}
			return p;
		}


		int write(int fd, const char* buf, size_t nbytes) {
			char** pp = get_pointer(fd);
			int offset = std::get<1>(table[fd]);
			int* p_inmemory_size = std::get<3>(table[fd]);
			std::cout << "inmemory size: " << *p_inmemory_size << " offset: " << offset << " nbytes: " << nbytes << std::endl;
			if (*p_inmemory_size < offset + nbytes) {
				if (*p_inmemory_size * 2 >= offset + nbytes)
					*p_inmemory_size *= 2;
				else
					*p_inmemory_size = offset + nbytes + *p_inmemory_size;
				//rellocate memory
				*pp = (char*) realloc(*pp, *p_inmemory_size * sizeof(char));
			}
			memcpy(*pp + offset, buf, nbytes);
			std::get<1>(table[fd]) = offset + nbytes;
			//update metadata
			std::get<2>(table[fd])->st_size = nbytes;
			return nbytes;
		}


		int read(int fd, char* buf, size_t nbytes) {
			char** pp = get_pointer(fd);
			auto opened_file = table[fd];
			int offset = std::get<1>(opened_file);
			int size = std::get<2>(opened_file)->st_size;
			int nbytes_read = size - offset;
			if (nbytes_read > nbytes)
				nbytes_read = nbytes;
			memcpy(buf, *pp, nbytes_read);
			std::cout << "buf " << buf << std::endl;
			std::get<1>(table[fd]) = offset + nbytes_read;
			return nbytes_read;
		}
};

#endif
