#ifndef CAPIO_FILE_HPP_
#define CAPIO_FILE_HPP_

#include <iostream>
#include <set>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "utils/common.hpp"

/*
 * Only the server have all the information
 * A process that only read from a file doesn't have the info on the sectors
 * A process that writes only have info on the sector that he wrote
 * the file size is in shm because all the processes need this info
 * and it's easy to provide it to them using the shm
 */

struct compare {
	bool operator() (const std::pair<off64_t,off64_t>& lhs, const std::pair<off64_t,off64_t>& rhs) const{
   		return (lhs.first < rhs.first);
    }
};

class Capio_file {
	private:
		bool _directory;
		bool _permanent;
		// _fd is useful only when the file is memory-mapped
		int _fd = -1; 
		bool _home_node = false;
		std::string _committed = "";
		std::string _mode = "";
		char* _buf = nullptr; //buffer containing the data
		// sectors stored in memory of the files (only the home node is forced to be up to date)
		std::set<std::pair<off64_t, off64_t>, compare> sectors;
		//vector of (tid, fd)
		std::vector<std::pair<int, int>>* threads_fd = nullptr;
		std::ostream& logfile;
	public:
		bool complete = false;
		bool first_write = true;
		int n_links = 1;
		int n_opens = 0;
		long int n_files = 0; //useful for directories
		long int n_files_expected = -1; //useful for directories
		long int _n_close = 0;
		long int _n_close_expected = -1;
		std::size_t _buf_size;
		/* 
		 * file size in the home node. In a given moment could not be up to date.
		 * This member is useful because a node different from the home node
		 * could need to known the size of the file but not its content
		 */
		std::size_t real_file_size = 0;

	    Capio_file() : logfile(std::cerr) {
            _committed = "on_termination";
            _directory = false;
            _permanent = false;
            _buf_size = 0;
			threads_fd = new std::vector<std::pair<int, int>>;
        }

		Capio_file(std::string committed, std::string mode,
				bool directory, long int n_files_expected, bool permanent, std::size_t init_size, std::ostream& logstream, long int n_close_expected) : logfile(logstream) {
			_committed = committed;
			_mode = mode;
			_directory = directory;
			_permanent = permanent;
			this->n_files_expected = n_files_expected + 2; // +2 for . and ..
			_buf_size = init_size;
			threads_fd = new std::vector<std::pair<int, int>>;
			_n_close_expected = n_close_expected;
		}

		Capio_file(bool directory, bool permanent, std::size_t init_size, std::ostream& logstream, long int n_close_expected = -1) : logfile(logstream) {
			_committed = "on_termination";
			_directory = directory;
			_permanent = permanent;
			_buf_size = init_size;
			threads_fd = new std::vector<std::pair<int, int>>;
			_n_close_expected = n_close_expected;
		}

		~Capio_file() {
			#ifdef CAPIOLOG
			logfile << "calling capio_file destructor" << std::endl;
			#endif
			free_buf();
			delete threads_fd;
		}

		void remove_fd(int tid, int fd) {
			auto it = std::find(threads_fd->begin(), threads_fd->end(), std::make_pair(tid, fd));
			if (it != threads_fd->end())
				threads_fd->erase(it);
		}

		std::string get_committed() {
			return _committed;
		}

		std::string get_mode() {
			return _mode;
		}

		bool is_dir() {
			return _directory;
		}

		bool buf_to_allocate() {
			return _buf == nullptr;
		}

		off64_t get_file_size() {
			if (sectors.size() != 0)
				return sectors.rbegin()->second;	
			else
				return 0;
		}

		/*
		 * To be called when a process
		 * execute a read or a write syscall
		 */

		void create_buffer(std::string path, bool home_node) {
			#ifdef CAPIOLOG
			logfile << "creating buf for file " << path << " home node " << home_node << " permanent " << _permanent << " dir " <<  _directory << std::endl;
			#endif
			_home_node = home_node;
			if (_permanent && home_node) {
				if (_directory) {
					if (mkdir(path.c_str(), 0700) == -1)
						err_exit("mkdir capio_file create_buffer");
					#ifdef CAPIOLOG
					logfile<< "creating buf dir" << std::endl;
					#endif
				_buf = new char[_buf_size];
				}
				else {
					#ifdef CAPIOLOG
					logfile<< "creating mem mapped file" << std::endl;
					#endif
					_fd = open(path.c_str(), O_RDWR | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH);
					if (_fd == -1)
						err_exit("open " + path + " " + "Capio_file constructor");
				
					
					if (ftruncate(_fd, _buf_size) == -1)
						err_exit("ftruncate Capio_file constructor");

					_buf = (char*) mmap(NULL, _buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
					if (_buf == MAP_FAILED)
						err_exit("mmap Capio_file constructor");

				}
			}
			else
				_buf = new char[_buf_size];
		}

		char* expand_buffer(std::size_t data_size) {
			size_t new_size, double_size = _buf_size * 2;
			new_size = data_size > double_size ? data_size : double_size;
			char* new_buf = new char[new_size];
			//	memcpy(new_p, old_p, file_shm_size); //TODO memcpy only the sector stored in Capio_file
			memcpy_capio_file(new_buf, _buf, new_size);
			delete [] _buf;
			_buf = new_buf;
			_buf_size = new_size;
			return new_buf;
		}

		char* get_buffer() {
			return _buf;
		}

		void free_buf() {
			if (_permanent && _home_node) {
				if (_directory)
					delete [] _buf;
				else {
					int res = munmap(_buf, _buf_size);	
					if (res == -1)
						err_exit("munmap Capio_file");
				}
			}
			else 
				delete [] _buf;
		}

		std::size_t get_buf_size() {
			return _buf_size;
		}

		/*
		 * get the size of the data stored in this node
		 * If the node is the home node then this is equals to
		 * the real size of the file
		 */

		off64_t get_stored_size() {
			auto it = sectors.rbegin();
			if (it == sectors.rend())
				return 0;
			else
				return it->second;
		}

		/*
		 * Insert the new sector automatically modifying the
		 * existent sectors if needed.
		 *
		 * Params:
		 * off64_t new_start: the beginning of the sector to insert
		 * off64_t new_end: the beginning of the sector to insert
		 * 
		 * new_srart must be > new_end otherwise the behaviour
		 * in undefined
		 *
		 */

		void insert_sector(off64_t new_start, off64_t new_end) {
			auto p = std::make_pair(new_start, new_end);

			if (sectors.size() == 0) {
				sectors.insert(p);
				return;
			}
			auto it_lbound = sectors.upper_bound(p);
			if (it_lbound == sectors.begin()) {
				if (new_end < it_lbound->first)
					sectors.insert(p);
				else {
				auto it = it_lbound;
				bool end_before = false;
				bool end_inside = false;
				while (it != sectors.end() && !end_before && !end_inside) {
					end_before = p.second < it->first;
					if (!end_before) {
						end_inside = p.second <= it->second;
						if (!end_inside)
							++it;
					}
				}

				if (end_inside) {
					p.second = it->second;
					++it;
				}
				sectors.erase(it_lbound, it);
				sectors.insert(p);				}
			}
			else {
				--it_lbound;
				auto it = it_lbound;
				if (p.first <= it_lbound->second) { 
					//new sector starts inside a sector
					p.first = it_lbound->first;
				}
				else //in this way the sector will not be deleted
					++it_lbound;
				bool end_before = false;
				bool end_inside = false;
				while (it != sectors.end() && !end_before && !end_inside) {
					end_before = p.second < it->first;
					if (!end_before) {
						end_inside = p.second <= it->second;
						if (!end_inside)
							++it;
					}
				}

				if (end_inside) {
					p.second = it->second;
					++it;
				}
				sectors.erase(it_lbound, it);
				sectors.insert(p);
			}
			
		}
		
		/*
		 * Returns the offset to the end of the sector 
		 * if the offset parameter is inside of the
		 * sector, -1 otherwise
		 *
		 */

		off64_t get_sector_end(off64_t offset) {
			off64_t sector_end = -1;
			auto it = sectors.upper_bound(std::make_pair(offset, 0));

			if (sectors.size() != 0 && it != sectors.begin()) {
				--it;
				if (offset <= it->second)
					sector_end = it->second;
			}

			return sector_end;
		}

		/*
		 * From the manual:
		 *
		 * Adjust the file offset to the next location in the file
         * greater than or equal to offset containing data.  If
         * offset points to data, then the file offset is set to
         * offset.
		 *
		 * Fails if offset points past the end of the file.
		 *
		 */

		off64_t seek_data(off64_t offset) {
			if (sectors.size() == 0) {
				if (offset == 0)
					return 0;
				else
					return -1;
			}

			auto it = sectors.upper_bound(std::make_pair(offset, 0));

			if (it == sectors.begin())
				return it->first;
			--it;
			if (offset <= it->second)
				return offset;
			else {
				++it;
				if (it == sectors.end())
					return -1;
				else 
					return it->first;
			}

		}

		/*
		 * From the manual:
		 *
		 * Adjust the file offset to the next hole in the file
         * greater than or equal to offset.  If offset points into
         * the middle of a hole, then the file offset is set to
         * offset.  If there is no hole past offset, then the file
         * offset is adjusted to the end of the file (i.e., there is
         * an implicit hole at the end of any file).
		 *
		 *
		 * Fails if offset points past the end of the file.
		 *
		 */

		off64_t seek_hole(off64_t offset) {
			if (sectors.size() == 0) {
				if (offset == 0)
					return 0;
				else
					return -1;
			}
			auto it = sectors.upper_bound(std::make_pair(offset, 0));
			if (it == sectors.begin())
				return offset;
			--it;
			if (offset <= it->second)
				return it->second;
			else {
				++it;
				if (it == sectors.end()) 
					return -1;
				else 
					return offset;
			}
		}

		void memcpy_capio_file(char* new_p, char* old_p, off64_t new_size) {
			for (auto& sector : sectors) {
				off64_t lbound = sector.first;
				off64_t ubound = sector.second;
				off64_t sector_length = ubound - lbound;
				memcpy(new_p + lbound, old_p + lbound, sector_length);
			}
		}

		void print(std::ostream& out_stream) {
			out_stream << "sectors" << std::endl;
			for (auto& sector : sectors) {
				out_stream << "<" << sector.first << ", " << sector.second << ">" << std::endl;
			}
		}

		void commit() {
			if (_permanent && !_directory && _home_node) {
				off64_t size = get_file_size();
				if (ftruncate(_fd, size) == -1)
					err_exit("ftruncate commit capio_file");
            	_buf_size = size;
				if (close(_fd) == -1)
					err_exit("close commit capio_file");
			}
		}
		
		void add_fd(int tid, int fd) {
			threads_fd->push_back({tid, fd});
		}
};

#endif
