#ifndef COMMAND_LINE_PARSER_HPP
#define COMMAND_LINE_PARSER_HPP
#include <iostream>
#include <string>

/**
 * Parse command line arguments.
 *
 * @param return value with string that contains the resolve prefix to use when parsing capio_cl
 * file
 * @return capio_cl configuration path
 */
std::string parseCLI(int argc, char **argv, char *resolve_prefix);

#endif // COMMAND_LINE_PARSER_HPP
