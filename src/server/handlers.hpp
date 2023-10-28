#ifndef CAPIO_SERVER_HANDLERS_HPP
#define CAPIO_SERVER_HANDLERS_HPP

// TODO: remove these headers from here and ensure all handlers are
// self-contained

#include "handlers/access.hpp"
#include "handlers/clone.hpp"
#include "handlers/close.hpp"
#include "handlers/common.hpp"
#include "handlers/dup.hpp"
#include "handlers/exig.hpp"
#include "handlers/handshake.hpp"
#include "handlers/mkdir.hpp"
#include "handlers/open.hpp"
#include "handlers/read.hpp"
#include "handlers/rename.hpp"
#include "handlers/rmdir.hpp"
#include "handlers/seek.hpp"
#include "handlers/stat.hpp"
#include "handlers/unlink.hpp"
#include "handlers/utils/util_filesys.hpp"
#include "handlers/utils/util_producer.hpp"
#include "handlers/write.hpp"
#include "utils/requests.hpp" //TODO: check whether to include requests here or in capio posix

#endif // CAPIO_SERVER_HANDLERS_HPP
