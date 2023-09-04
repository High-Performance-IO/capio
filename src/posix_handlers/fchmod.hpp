#ifndef CAPIO_FCHMOD_HPP
#define CAPIO_FCHMOD_HPP

int fchmod_handler(long arg0, long arg1, long arg2,long arg3, long arg4, long arg5, long* result,long my_tid){

    int res = (files->find(static_cast<int>(arg0)) == files->end()) ? -2 : 0;

    if (res != -2) {
        *result = (res < 0 ? -errno : res);
        return 0;
    }
    return 1;
}
#endif //CAPIO_FCHMOD_HPP
