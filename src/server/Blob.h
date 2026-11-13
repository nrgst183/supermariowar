#ifndef BLOB_H
#define BLOB_H

#include <memory>
#include <stddef.h>
#include <stdint.h>

class Blob {
public:
    Blob();
    Blob(const Blob& other);
    Blob& operator=(const Blob& other);
    virtual ~Blob();

    bool empty() const;
    void replace_with(const void*, size_t);
    void free();

    const uint8_t* get_data() const;
    size_t get_size() const;

protected:
    std::unique_ptr<uint8_t[]> data;
    size_t size;
};

#endif // BLOB_H
