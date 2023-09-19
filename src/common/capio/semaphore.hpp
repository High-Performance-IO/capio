#ifndef CAPIO_COMMON_SEMAPHORE_H
#define CAPIO_COMMON_SEMAPHORE_H

#include <semaphore.h>

inline sem_t init_sem(const char * const name, int value) {

    sem_t sem;
    sem_init(&sem, 0, value);
    return sem;
}

class sem_guard {
private:
    sem_t *_sem;
public:
    inline explicit sem_guard(sem_t *sem): _sem(sem) {
        START_LOG(-1, "call()");

        if (sem_wait(this->_sem) == -1) {
            ERR_EXIT("error sem_wait");
        }
    }
    inline ~sem_guard() {
        START_LOG(-1, "call()");
        if (sem_post(this->_sem) == -1) {
            ERR_EXIT("error sem_post");
        }
    }
};

#endif // CAPIO_COMMON_SEMAPHORE_H
