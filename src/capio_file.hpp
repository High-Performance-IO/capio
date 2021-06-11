#ifndef FILE_INFO_HPP_
#define FILE_INFO_HPP_

class capio_file {
	private:
		char* m_p_memory;
		int m_offset;
		int m_memory_size;
		struct stat m_metadata;
		
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

	public:

		//constructor

		capio_file() {
			m_p_memory = nullptr;
			m_memory_size = 0;
		}

		int allocate_memory() {
			m_p_memory = (char*) malloc(128 * sizeof(int));
			if (m_p_memory == NULL)
				return -1;
			m_memory_size = 128;
			return 0;
		}

		void free_memory() {
			free(m_p_memory);
		}

		struct stat get_metadata() {
			return m_metadata;
		}
		
		void set_memory_size(int memory_size) {
			m_memory_size = memory_size;
		}



		void set_metadata() {
			m_metadata = create_metadata();
		}

		int write(const char* buf, int offset, size_t nbytes) {
			std::cout << "inmemory size: " << m_memory_size << " offset: " << offset << " nbytes: " << nbytes << std::endl;
			if (m_memory_size < offset + nbytes) {
				if (m_memory_size * 2 >= offset + nbytes)
					m_memory_size *= 2;
				else
					m_memory_size = offset + nbytes + m_memory_size;
				//rellocate memory
				char* p_tmp = (char*) realloc(m_p_memory, m_memory_size * sizeof(char));
				if (p_tmp == NULL)
					std::cerr << "error realloc failed" << std::endl;
				else
					m_p_memory = p_tmp;
			}
			 std::cout << "after if inmemory size: " << m_memory_size << " offset: " << offset << " nbytes: " << nbytes << std::endl;

			memcpy(m_p_memory + offset, buf, nbytes);
			//update metadata
			m_metadata.st_size +=  nbytes;
			return nbytes;
		}

		int read(char* buf, int offset, int nbytes) {
			int size = m_metadata.st_size;
			int nbytes_read = size - offset;
			if (nbytes_read > nbytes)
				nbytes_read = nbytes;
			 std::cerr << "inmemory size: " << m_memory_size << " offset: " << offset << " nbytes: " << nbytes << "nbytes_read" << nbytes_read << std::endl;

			memcpy(buf, m_p_memory + offset, nbytes_read);
			return nbytes_read;
		}

};

#endif
