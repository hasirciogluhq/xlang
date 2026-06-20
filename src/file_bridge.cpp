#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

static thread_local char io_buffer[1024 * 1024];

struct XlangFile {
    std::fstream stream;
    std::string path;
    int32_t mode;
};

static std::vector<std::unique_ptr<XlangFile>> g_files;

static void copy_to_buffer(const std::string& data) {
    const std::size_t n = data.size();
    const std::size_t cap = sizeof(io_buffer) - 1;
    const std::size_t copy = n < cap ? n : cap;
    if (copy > 0) {
        std::memcpy(io_buffer, data.data(), copy);
    }
    io_buffer[copy] = '\0';
}

static XlangFile* file_at(int64_t handle) {
    if (handle <= 0) {
        return nullptr;
    }
    const std::size_t idx = static_cast<std::size_t>(handle - 1);
    if (idx >= g_files.size() || g_files[idx] == nullptr) {
        return nullptr;
    }
    return g_files[idx].get();
}

static int64_t alloc_handle(std::unique_ptr<XlangFile> file) {
    for (std::size_t i = 0; i < g_files.size(); ++i) {
        if (g_files[i] == nullptr) {
            g_files[i] = std::move(file);
            return static_cast<int64_t>(i + 1);
        }
    }
    g_files.push_back(std::move(file));
    return static_cast<int64_t>(g_files.size());
}

static std::ios::openmode map_mode(int32_t mode) {
    switch (mode) {
        case 1:
            return std::ios::in;
        case 2:
            return std::ios::out | std::ios::trunc;
        case 3:
            return std::ios::out | std::ios::app;
        case 4:
            return std::ios::in | std::ios::out | std::ios::trunc;
        default:
            return std::ios::in;
    }
}

}  // namespace

extern "C" {

int64_t xlang_file_open(const char* path, int32_t mode) {
    if (path == nullptr || path[0] == '\0') {
        return -1;
    }
    auto file = std::make_unique<XlangFile>();
    file->path = path;
    file->mode = mode;
    file->stream.open(path, map_mode(mode));
    if (!file->stream.is_open()) {
        return -1;
    }
    return alloc_handle(std::move(file));
}

int32_t xlang_file_close(int64_t handle) {
    XlangFile* file = file_at(handle);
    if (file == nullptr) {
        return 1;
    }
    if (file->stream.is_open()) {
        file->stream.close();
    }
    const std::size_t idx = static_cast<std::size_t>(handle - 1);
    g_files[idx].reset();
    return 0;
}

const char* xlang_file_read_path(const char* path) {
    if (path == nullptr) {
        io_buffer[0] = '\0';
        return io_buffer;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        io_buffer[0] = '\0';
        return io_buffer;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    copy_to_buffer(ss.str());
    return io_buffer;
}

int32_t xlang_file_write_path(const char* path, const char* data, int32_t append) {
    if (path == nullptr) {
        return 1;
    }
    const char* payload = data != nullptr ? data : "";
    std::ofstream out;
    if (append != 0) {
        out.open(path, std::ios::binary | std::ios::app);
    } else {
        out.open(path, std::ios::binary | std::ios::trunc);
    }
    if (!out.is_open()) {
        return 1;
    }
    out << payload;
    return out.good() ? 0 : 1;
}

int32_t xlang_file_exists(const char* path) {
    if (path == nullptr) {
        return 0;
    }
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
}

int64_t xlang_file_size(const char* path) {
    if (path == nullptr) {
        return -1;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    return static_cast<int64_t>(st.st_size);
}

const char* xlang_file_read_handle(int64_t handle) {
    XlangFile* file = file_at(handle);
    if (file == nullptr || !file->stream.is_open()) {
        io_buffer[0] = '\0';
        return io_buffer;
    }
    std::ostringstream ss;
    ss << file->stream.rdbuf();
    copy_to_buffer(ss.str());
    return io_buffer;
}

int32_t xlang_file_write_handle(int64_t handle, const char* data) {
    XlangFile* file = file_at(handle);
    if (file == nullptr || !file->stream.is_open()) {
        return 1;
    }
    const char* payload = data != nullptr ? data : "";
    file->stream << payload;
    file->stream.flush();
    return file->stream.good() ? 0 : 1;
}

}  // extern "C"
