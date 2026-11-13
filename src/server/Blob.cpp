#include "Blob.h"

#include <string.h>

Blob::Blob(): data(nullptr), size(0) {}

Blob::Blob(const Blob& other): data(nullptr), size(0) {
	if (other.data)
		replace_with(other.data.get(), other.size);
}

Blob& Blob::operator=(const Blob& other) {
	if (this != &other) {
		if (other.data)
			replace_with(other.data.get(), other.size);
		else
			free();
	}
	return *this;
}

Blob::~Blob() = default;

void Blob::free() {
	data.reset();
	size = 0;
}

void Blob::replace_with(const void* src, size_t src_size) {
	// Copy before releasing the old buffer, in case `src` points into it
	auto newData = std::make_unique<uint8_t[]>(src_size);
	memcpy(newData.get(), src, src_size);
	data = std::move(newData);
	size = src_size;
}

bool Blob::empty() const {
	return size == 0;
}

const uint8_t* Blob::get_data() const {
	return data.get();
}

size_t Blob::get_size() const {
	return size;
}
