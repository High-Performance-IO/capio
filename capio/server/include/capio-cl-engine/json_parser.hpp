#ifndef JSON_PARSER_HPP
#define JSON_PARSER_HPP
#include <capio-cl-engine/capio_cl_engine.hpp>

/**
 * @brief Contains the code to parse a JSON based CAPIO-CL configuration file
 *
 */
class JsonParser {

    /**
     * @brief Check if a string is a representation of a integer number
     *
     * @param s
     * @return true
     * @return false
     */
    static bool is_int(const std::string &s);

    /**
     * @brief compare two paths
     *
     * @param path
     * @param base
     * @return true if @p path is a subdirectory of base
     * @return false otherwise
     */
    static inline bool first_is_subpath_of_second(const std::filesystem::path &path,
                                                  const std::filesystem::path &base);

  public:
    /**
     * @brief Perform the parsing of the capio_server configuration file
     *
     * @param source
     * @return CapioCLEngine instance with the information provided by the config file
     */
    static CapioCLEngine *parse(const std::filesystem::path &source,
                                const std::filesystem::path resolve_prexix);
};

#endif // JSON_PARSER_HPP
