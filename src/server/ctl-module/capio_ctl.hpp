#ifndef CAPIO_FS_CAPIO_CTL_HPP
#define CAPIO_FS_CAPIO_CTL_HPP
/**
 * @brief Class that contains the logic to to the CapioCTL component yet to e implemented
 *
 */
class CapioCTLModule {

    std::thread *th;

    bool *continue_execution = new bool;
    CSBufRequest_t *readQueue;
    CSBufRequest_t *writeQueue;

    static void _main(const bool *continue_execution, CSBufRequest_t *readQueue,
                      CSBufRequest_t *writeQueue) {
        START_LOG(gettid(), "INFO: instance of CapioCTLModule");

        char request[CAPIO_REQ_MAX_SIZE];

        while (*continue_execution) {
            LOG("Reading incoming request");
            readQueue->read(request);
            LOG("Received request %s", request);
            /*
             * TODO: implement logic of CAPIO-CTL
             */
        }
    }

  public:
    CapioCTLModule() {
        readQueue = new CSBufRequest_t("RX", CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE, workflow_name);
        writeQueue =
            new CSBufRequest_t("TX", CAPIO_REQ_BUFF_CNT, CAPIO_REQ_MAX_SIZE, workflow_name);

        *continue_execution = true;
        th                  = new std::thread(_main, continue_execution, readQueue, writeQueue);
        std::cout << CAPIO_SERVER_CLI_LOG_SERVER << " [ " << node_name << " ] "
                  << "CapioCTL initialization completed." << std::endl;
    }

    ~CapioCTLModule() {
        *continue_execution = false;
        pthread_cancel(th->native_handle());
        th->join();
        delete continue_execution;
        delete readQueue;
        delete writeQueue;
    }
};

inline CapioCTLModule *ctl_module;

#endif // CAPIO_FS_CAPIO_CTL_HPP
