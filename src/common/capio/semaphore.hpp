#include <semaphore.h>

#ifndef CAPIO_SEMS_HPP
#define CAPIO_SEMS_HPP

#ifdef __CAPIO_POSIX

#define SEM_WAIT_CHECK(sem, sem_name)                                                              \
    if (sem_wait(sem) == -1) {                                                                     \
        char message[1024];                                                                        \
        sprintf(message, "unable to wait on %s", sem_name);                                        \
        ERR_EXIT(message);                                                                         \
    };

#define SEM_POST_CHECK(sem, sem_name)                                                              \
    if (sem_post(sem) == -1) {                                                                     \
        char message[1024];                                                                        \
        sprintf(message, "unable to post on %s", sem_name);                                        \
        ERR_EXIT(message);                                                                         \
    };

#define SEM_DESTROY_CHECK(sem, sem_name, action)                                                   \
    if (sem_destroy(sem) != 0) {                                                                   \
        action;                                                                                    \
        ERR_EXIT("destruction of semaphore %s failed", sem_name);                                  \
    };

#define SEM_NAMED_DESTROY_CHECK(sem)                                                               \
    if (sem_unlink(sem) != 0) {                                                                    \
        ERR_EXIT("destruction of semaphore %s failed", sem);                                       \
    };

#define SEM_CREATE_CHECK(sem, source)                                                              \
    if (sem == SEM_FAILED) {                                                                       \
        LOG(CAPIO_SHM_OPEN_ERROR);                                                                 \
        LOG("error creating opening %s", _shm_name);                                               \
    };

#else

#define SEM_WAIT_CHECK(sem, sem_name)                                                              \
    if (sem_wait(sem) == -1) {                                                                     \
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR                                              \
                  << "Unable to wait on semaphore:  " << sem_name << std::endl;                    \
        ERR_EXIT("Unable to wait on semaphore:  %s", sem_name);                                    \
    };

#define SEM_POST_CHECK(sem, sem_name)                                                              \
    if (sem_post(sem) == -1) {                                                                     \
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR                                              \
                  << "Unable to post on semaphore:  " << sem_name << std::endl;                    \
        ERR_EXIT("Unable to post on semaphore:  %s", sem_name);                                    \
    };

#define SEM_DESTROY_CHECK(sem, sem_name, action)                                                   \
    if (sem_destroy(sem) != 0) {                                                                   \
        action;                                                                                    \
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR                                              \
                  << "Unable to destroy semaphore:  " << sem_name << std::endl;                    \
        ERR_EXIT("Unable to destroy semaphore:  ", sem_name);                                      \
    };

#define SEM_NAMED_DESTROY_CHECK(sem_name)                                                          \
    if (sem_unlink(sem_name) == -1) {                                                              \
        std::cout << CAPIO_LOG_SERVER_CLI_LEVEL_ERROR                                              \
                  << "Unable to destroy semaphore:  " << sem_name << std::endl;                    \
        ERR_EXIT("Unable to destroy semaphore:  ", sem_name);                                      \
    }

#define SEM_CREATE_CHECK(sem, source)                                                              \
    if (sem == SEM_FAILED) {                                                                       \
        LOG(CAPIO_SHM_OPEN_ERROR);                                                                 \
        LOG("error while opening %s", source);                                                     \
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER_ERROR << "Unable to open shm: " << source         \
                  << std::endl;                                                                    \
        ERR_EXIT("Unable to open shm: %s", source);                                                \
    };

#endif

#endif // CAPIO_SEMS_HPP