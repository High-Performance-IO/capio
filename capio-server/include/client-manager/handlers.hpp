#ifndef HANDLERS_HPP
#define HANDLERS_HPP

/**
 * @brief Handle the close systemcall
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
void close_handler(const char *const str);

/**
 * @brief Handle the consent to proceed request. This handler only checks whether the conditions for
 * a systemcall to continue are met.
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
void consent_to_proceed_handler(const char *const str);

/**
 * @brief Handle the create systemcall
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
void create_handler(const char *const str);

/**
 * @brief Handle the exit systemcall
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
void exit_handler(const char *const str);

/**
 * @brief Handle the exit systemcall
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
void files_to_store_in_memory_handler(const char *const str);

/**
 * @brief Perform handshake while providing the posix application name
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
void handshake_handler(const char *const str);

/**
 * @brief Handle the open systemcall
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
void open_handler(const char *const str);

/**
*
*/
void posix_readdir_handler(const char *const str);

/**
 * @brief Handle the read systemcall
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
void read_handler(const char *const str);

void read_mem_handler(const char *const str);


/**
 * @brief Handle the rename systemcall
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
void rename_handler(const char *const str);

/**
 * @brief Handle the write systemcall
 *
 * @param str raw request as read from the shared memory interface stripped of the request number
 * (first parameter of the request)
 */
void write_handler(const char *const str);

void write_mem_handler(const char *const str);



#endif //HANDLERS_HPP
