#ifndef CAPIO_MISC_HPP
#define CAPIO_MISC_HPP

void get_app_name(std::string* capio_app_name) {
    char* val;
    if (capio_app_name == nullptr) {
        val = getenv("CAPIO_APP_NAME");
        if (val != NULL)
            capio_app_name = new std::string(val);
    }
}

#endif //CAPIO_MISC_HPP
