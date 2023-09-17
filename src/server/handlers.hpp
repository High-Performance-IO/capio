#ifndef CAPIO_SERVER_HANDLERS_HPP
#define CAPIO_SERVER_HANDLERS_HPP

// TODO: remove these headers from here and ensure all handlers are self-contained

#include "handlers/utils/util_delete.hpp"
#include "handlers/utils/util_files.hpp"
#include "handlers/utils/util_filesys.hpp"
#include "handlers/utils/util_producer.hpp"
#include "handlers/common.hpp"

#include "handlers/access.hpp"
#include "handlers/clone.hpp"
#include "handlers/close.hpp"
#include "handlers/crax.hpp"
#include "handlers/dup.hpp"
#include "handlers/exig.hpp"
#include "handlers/handshake.hpp"
#include "handlers/lseek.hpp"
#include "handlers/mkdir.hpp"
#include "handlers/open.hpp"
#include "handlers/read.hpp"
#include "handlers/rename.hpp"
#include "handlers/sdat.hpp"
#include "handlers/seek_end.hpp"
#include "handlers/shol.hpp"
#include "handlers/signals.hpp"
#include "handlers/stat.hpp"
#include "handlers/unlink.hpp"
#include "handlers/write.hpp"

#endif // CAPIO_SERVER_HANDLERS_HPP

