/* filesystem — Windows stub ABI (unsupported). */
#include <stdint.h>

int32_t xl_filesystem_errno(void) {
    return -1;
}

int64_t xl_filesystem_open(const char* path, int32_t mode) {
    (void)path; (void)mode; return -1;
}

int32_t xl_filesystem_close(int64_t handle) {
    (void)handle; return -1;
}

const char* xl_filesystem_read(int64_t handle, int32_t max) {
    (void)handle; (void)max; return "";
}

int32_t xl_filesystem_write(int64_t handle, const char* data) {
    (void)handle; (void)data; return -1;
}

int32_t xl_filesystem_write_n(int64_t handle, const char* data, int32_t n) {
    (void)handle; (void)data; (void)n; return -1;
}

int64_t xl_filesystem_seek(int64_t handle, int64_t offset, int32_t whence) {
    (void)handle; (void)offset; (void)whence; return -1;
}

int64_t xl_filesystem_tell(int64_t handle) {
    (void)handle; return -1;
}

int32_t xl_filesystem_flush(int64_t handle) {
    (void)handle; return -1;
}

int32_t xl_filesystem_truncate(int64_t handle, int64_t size) {
    (void)handle; (void)size; return -1;
}

const char* xl_filesystem_read_all(const char* path) {
    (void)path; return "";
}

int32_t xl_filesystem_write_all(const char* path, const char* data) {
    (void)path; (void)data; return -1;
}

int32_t xl_filesystem_append_all(const char* path, const char* data) {
    (void)path; (void)data; return -1;
}

int32_t xl_filesystem_exists(const char* path) {
    (void)path; return 0;
}

int32_t xl_filesystem_is_file(const char* path) {
    (void)path; return 0;
}

int32_t xl_filesystem_is_dir(const char* path) {
    (void)path; return 0;
}

int32_t xl_filesystem_is_symlink(const char* path) {
    (void)path; return 0;
}

int64_t xl_filesystem_size(const char* path) {
    (void)path; return -1;
}

int64_t xl_filesystem_mtime(const char* path) {
    (void)path; return -1;
}

int32_t xl_filesystem_mkdir(const char* path) {
    (void)path; return -1;
}

int32_t xl_filesystem_mkdir_all(const char* path) {
    (void)path; return -1;
}

int32_t xl_filesystem_remove(const char* path) {
    (void)path; return -1;
}

int32_t xl_filesystem_rmdir(const char* path) {
    (void)path; return -1;
}

int32_t xl_filesystem_rename(const char* from, const char* to) {
    (void)from; (void)to; return -1;
}

int32_t xl_filesystem_copy(const char* from, const char* to) {
    (void)from; (void)to; return -1;
}

int32_t xl_filesystem_chmod(const char* path, int32_t mode) {
    (void)path; (void)mode; return -1;
}

const char* xl_filesystem_cwd(void) {
    return "";
}

int32_t xl_filesystem_chdir(const char* path) {
    (void)path; return -1;
}

const char* xl_filesystem_realpath(const char* path) {
    (void)path; return "";
}

const char* xl_filesystem_read_dir(const char* path) {
    (void)path; return "";
}

const char* xl_filesystem_read_handle(int64_t handle) {
    (void)handle; return "";
}

int32_t xl_filesystem_write_handle(int64_t handle, const char* data) {
    (void)handle; (void)data; return -1;
}

