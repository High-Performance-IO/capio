# How to add a new backend

To implement a new backend for capio, first create a new backend file inside the
directory `CAPIO_ROOT_DIR/src/server/remote/backend/`. This file must be included inside the
file `CAPIO_ROOT_DIR/src/server/remote/backend/include.hpp`.

## Class requirement for the new backend.

The newly created backend must extend the class `Backend` (defined in `CAPIO_ROOT_DIR/src/server/remote/backend.hpp`)
and implements its methods. More methods can be implemented, but CAPIO will only use the ones that are defined
into the interface. 

## Register a new backend inside capio

Finally, to be able to use the newly created backend, the function `select_backend()`, defined inside the file 
`CAPIO_ROOT_DIR/src/server/remote/`, has to be modified. a new if clause should be added before the final result, comparing
the given backend name with the chosen backend name, returning an instantiation of your new backend.

The last thinks that needs to be done, is to let the user know that your backend exists. To do this, you should change
the constant `CAPIO_SERVER_ARG_PARSER_CONFIG_BACKEND_HELP`, defined in the file 
`CAPIO_ROOT_DIR/src/common/capio/constants.hpp`, to include your backend.
