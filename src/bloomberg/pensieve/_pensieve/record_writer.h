#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <variant>
#include <vector>

#include <fcntl.h>
#include <type_traits>

#include "records.h"

namespace pensieve::tracking_api {

class RecordWriter
{
  public:
    explicit RecordWriter(const std::string& file_name);
    ~RecordWriter();

    template<typename T>
    bool inline writeRecord(const RecordType& token, const T& item)
    {
        std::lock_guard<std::mutex> lock(d_mutex);
        constexpr const size_t total = sizeof(RecordType) + sizeof(T);
        static_assert(
                std::is_trivially_copyable<T>::value,
                "Cannot writeRecord as binary records that cannot be trivially copyable");
        static_assert(total < BUFFER_CAPACITY, "cannot writeRecord line larger than d_buffer capacity");

        if (total > availableSpace() && !_flush()) {
            return false;
        }
        ::memcpy(bufferNeedle(), reinterpret_cast<const void*>(&token), sizeof(RecordType));
        d_used_bytes += sizeof(RecordType);
        ::memcpy(bufferNeedle(), reinterpret_cast<const void*>(&item), sizeof(T));
        d_used_bytes += sizeof(T);
        return true;
    }

    bool flush();

  private:
    // Constants
    static const size_t BUFFER_CAPACITY = PIPE_BUF;

    // Data members
    int fd{-1};
    unsigned d_used_bytes{0};
    std::unique_ptr<char[]> d_buffer{nullptr};
    std::mutex d_mutex;

    // Methods
    inline size_t availableSpace() const;
    inline char* bufferNeedle() const;
    bool _flush();
};

inline size_t
RecordWriter::availableSpace() const
{
    return BUFFER_CAPACITY - d_used_bytes;
}

inline char*
RecordWriter::bufferNeedle() const
{
    return d_buffer.get() + d_used_bytes;
}

template<>
bool inline RecordWriter::writeRecord(const RecordType& token, const pyframe_map_val_t& item)
{
    std::lock_guard<std::mutex> lock(d_mutex);
    if (!_flush()) {
        return false;
    }
    auto writeSimpleType = [&](auto&& item) {
        int ret;
        do {
            ret = ::write(fd, reinterpret_cast<const char*>(&item), sizeof(item));
        } while (ret < 0 && errno == EINTR);
        return ret != 0;
    };

    auto writeString = [&](const std::string& the_string) {
        int ret;
        do {
            ret = ::write(fd, the_string.c_str(), the_string.size());
        } while (ret < 0 && errno == EINTR);
        writeSimpleType('\0');
        return ret != 0;
    };

    return writeSimpleType(token) && writeSimpleType(item.first)
           && writeString(item.second.function_name) && writeString(item.second.filename)
           && writeSimpleType(item.second.lineno);
}

}  // namespace pensieve::tracking_api
