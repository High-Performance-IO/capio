#include <semaphore.h>

#define SEM_WAIT_CHECK(sem, sem_name)                                                              \
    if (sem_wait(sem) == -1) {                                                                     \
        char message[1024];                                                                        \
        sprintf(message, "unable to wait on %s", sem_name);                                        \
        ERR_EXIT(message);                                                                         \
    }

#define SEM_POST_CHECK(sem, sem_name)                                                              \
    if (sem_post(sem) == -1) {                                                                     \
        char message[1024];                                                                        \
        sprintf(message, "unable to post on %s", sem_name);                                        \
        ERR_EXIT(message);                                                                         \
    }

#define SEM_DESTROY_CHECK(sem, sem_name, action)                                                   \
    int res = sem_destroy(sem);                                                                    \
    if ((res = sem_destroy(sem)) != 0) {                                                           \
        action;                                                                                    \
        ERR_EXIT("destruction of semaphore %s failed with status %d", sem_name, res);              \
    };