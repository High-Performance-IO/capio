#ifndef CAPIO_FILE_HPP
#define CAPIO_FILE_HPP

class File {
  public:
    File()          = default;
    virtual ~File() = default;

  private:
    virtual void write(void *content, capio_off64_t offset, capio_off64_t buffer_size);
    virtual void read(capio_off64_t offset, capio_off64_t count);
    virtual void seek(capio_off64_t offset);
    virtual void close();
};

class FilePage {
  private:
    char *buffer;
    capio_off64_t internal_offset = 0;

  public:
    FilePage() { buffer = new char[1024]; };
    ~FilePage() { delete[] buffer; }

    void write(const void *content, const capio_off64_t offset,
               const capio_off64_t buffer_size) const {
        memcpy(buffer + offset, content, buffer_size);
    };

    void write(const void *content, const capio_off64_t buffer_size) const {
        memcpy(buffer + internal_offset, content, buffer_size);
    }
};

class HomeNodeFile : public File {
    std::list<FilePage *> *pages;
    FilePage *current             = nullptr;
    capio_off64_t _file_page_size = 0;

  public:
    explicit HomeNodeFile(capio_off64_t file_page_size) : File(), _file_page_size(file_page_size) {
        pages = new std::list<FilePage *>;
    }
    ~HomeNodeFile() override { delete pages; }

    void write(void *content, capio_off64_t offset, capio_off64_t buffer_size) override{};
    void read(capio_off64_t offset, capio_off64_t count) override{};
    void seek(capio_off64_t offset) override{};
    void close() override{
        // call metadata service to close the pointer
    };
};

#endif // CAPIO_FILE_HPP
