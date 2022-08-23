#ifndef CAPIO_FILE_HPP_
#define CAPIO_FILE_HPP_

#include <iostream>
#include <set>
#include <cstddef>

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
		std::set<std::pair<off64_t, off64_t>, compare> sectors;
		bool _directory;

	public:
		bool complete = false;
		int n_links = 1;
		int n_opens = 0;


		Capio_file() {
			_directory = false;
		}

		Capio_file(bool directory) {
			_directory = directory;
		}

		bool is_dir() {
			return _directory;
		}

		off64_t get_file_size() {
			if (sectors.size() != 0)
				return sectors.rbegin()->second;	
			else
				return 0;
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

		void print() {
			std::cout << "sectors" << std::endl;
			for (auto& sector : sectors) {
				std::cout << "<" << sector.first << ", " << sector.second << ">" << std::endl;
			}
		}
};

#endif
