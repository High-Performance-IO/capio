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

The last thing to do, is to let the user know that your backend actually exists. To do this, add its name to the
allowed values (`choices`) of the `capio.backend.type` option, defined in the option table inside
`CAPIO_ROOT_DIR/capio/server/src/runtime_configuration.cpp`. Backend specific settings should be added to the same
table, under `capio.backend.<name>.*`, so that they are validated and documented by `capio_server --genconf`.
