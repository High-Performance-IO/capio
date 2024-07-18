#ifndef CAPIO_CLONE_HPP
#define CAPIO_CLONE_HPP
void clone_handler(const char *const str) { START_LOG(gettid(), "call(str=%s)", str); }
#endif // CAPIO_CLONE_HPP
