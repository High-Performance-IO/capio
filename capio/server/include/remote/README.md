# How to add a new backend

To implement a new backend for CAPIO, first create a new backend file inside the
directory `CAPIO_ROOT_DIR/src/server/remote/backend/`. This file must be included inside the
file `CAPIO_ROOT_DIR/src/server/remote/backend/include.hpp`.

## Class requirement for the new backend.

The newly created backend must extend the class `Backend` (defined in `CAPIO_ROOT_DIR/src/server/remote/backend.hpp`)
and implements its methods. More methods can be implemented, but CAPIO will only use the ones that are defined
into the interface.

## Register a new backend inside capio

Finally, to be able to use the newly created backend, the function `select_backend()`, defined inside the file
`CAPIO_ROOT_DIR/src/server/remote/`, has to be modified. In particular, a new conditional branch should be added before
the final return statement, comparing the new backend name with the one chosen by the user, returning an instance of the
new backend if the names matches.

The last thing to do, is to let the user know that your backend actually exists. To do this, you should change the
constant `CAPIO_SERVER_ARG_PARSER_CONFIG_BACKEND_HELP`, defined in the
file `CAPIO_ROOT_DIR/src/common/capio/constants.hpp`, to include the name of your backend.

# File locations

Every CAPIO file has a home: the server that holds it. `HomeNodeService`
(`home_node_service.hpp`) keeps, on each server, a table of the files it has heard of and their
homes, through the communication backend, as ARP keeps a table of addresses:

| request | sent | meaning |
|---|---|---|
| `LOCATION_ANNOUNCE <path>` | to every other server, when a server creates a file | the sender is the home of `path` |
| `LOCATION_FORGET <path>` | to every other server, when the home removes a file | `path` is gone from the sender |
| `LOCATION_QUERY <id> <path>` | to every other server, by one that does not know `path` | who holds `path`? |
| `LOCATION_ANSWER <id> <0 or 1> <path>` | to the server that asked | whether the sender is the home of `path` |

Every server answers a query, so a lookup of a file that no server holds ends as soon as all have
answered. A server waiting for a file to be created is woken by its announcement, and asks again
every 500 ms in case the announcement was sent before it could receive it, as with MTCL, whose
servers connect as they discover each other. A rename or a migration is a removal followed by a
creation; a `LOCATION_FORGET` removes a path only if the sender is still its recorded home, so it
cannot undo an announcement from the file's new home that arrived first.

Only running servers are asked: a file whose producer and server ended before its readers started
is assumed to have been written to the file system. A new backend needs nothing more for this than
`get_nodes()` and `send_request()`.
