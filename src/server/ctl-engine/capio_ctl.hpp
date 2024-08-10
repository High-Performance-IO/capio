#ifndef CAPIO_FS_CAPIO_CTL_HPP
#define CAPIO_FS_CAPIO_CTL_HPP

class CapioCtlEngine {

    std::thread *th;

    bool *continue_execution = new bool;
    CSBufRequest_t *readQueue;
    CSBufRequest_t *writeQueue;

    static void _main(const bool *continue_execution, CSBufRequest_t *readQueue,
                      CSBufRequest_t *writeQueue) {
        START_LOG(gettid(), "call()");

        char request[CAPIO_REQ_MAX_SIZE];

        while (*continue_execution) {
            LOG("Reading incoming request");
            readQueue->read(request);
            LOG("Recived request %s", request);

            writeQueue->write("CIAO PLUTO");
        }
    }

  public:
    CapioCtlEngine() {
        readQueue = new CSBufRequest_t("RX", CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE, workflow_name);
        writeQueue =
            new CSBufRequest_t("TX", CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE, workflow_name);

        *continue_execution = true;
        th                  = new std::thread(_main, continue_execution, readQueue, writeQueue);
    }

    ~CapioCtlEngine() {
        *continue_execution = false;
        pthread_cancel(th->native_handle());
        th->join();
        delete continue_execution;
        delete readQueue;
        delete writeQueue;
    }
};

CapioCtlEngine *ctl_engine;

#endif // CAPIO_FS_CAPIO_CTL_HPP
