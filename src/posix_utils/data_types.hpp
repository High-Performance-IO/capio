#ifndef CAPIO_DATA_TYPES_HPP
#define CAPIO_DATA_TYPES_HPP

#include "../data_structure/circular_buffer.hpp"

//All names begin with CP as for CapioPosix

typedef std::unordered_map<int, std::tuple<off64_t *, off64_t *, int, int>> CPFiles_t;
typedef Circular_buffer<char> CPBufRequest_t;
typedef std::unordered_map<int, Circular_buffer<off_t> *> CPBufResponse_t;
typedef std::unordered_map<int, sem_t *> CPSemsWrite_t;
typedef std::unordered_map<int, std::string> CPFileDescriptors_t;
typedef std::unordered_set<std::string> CPFilesPaths_t;
typedef std::unordered_map<long int, bool> CPStatEnabled_t;
typedef std::unordered_map<int, std::pair<SPSC_queue<char> *, SPSC_queue<char> *>> CPThreadDataBufs_t;


//pointer to how a function handler should be made... TODO: document this thing
typedef int(*CPHandler_t)(long, long, long, long, long, long, long*, long);

#endif //CAPIO_DATA_TYPES_HPP
