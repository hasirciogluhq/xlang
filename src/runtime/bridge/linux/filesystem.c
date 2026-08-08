/* filesystem — POSIX ABI for xlang.
 *
 * Never aborts, never throws. Failures return -1 / 0 / empty string and
 * stash errno in a thread-local last-error for the frontend to inspect.
 */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum {
    FS_MODE_READ = 1,
    FS_MODE_WRITE = 2,
    FS_MODE_APPEND = 3,
    FS_MODE_READ_WRITE = 4,
    FS_MODE_READ_WRITE_EXISTING = 5,
};

enum {
    FS_SEEK_SET = 0,
    FS_SEEK_CUR = 1,
    FS_SEEK_END = 2,
};

static __thread int g_fs_errno = 0;
static __thread char g_io_buffer[1024 * 1024];
static __thread char g_path_buffer[PATH_MAX];

struct FsFile {
    FILE* fp;
    int used;
};

#define FS_MAX_HANDLES 256
static struct FsFile g_files[FS_MAX_HANDLES];

static void fs_set_err(int err) {
    g_fs_errno = err;
}

static const char* fs_empty(void) {
    g_io_buffer[0] = '\0';
    return g_io_buffer;
}

static FILE* fs_fp(int64_t handle) {
    if (handle <= 0 || handle > FS_MAX_HANDLES) {
        return NULL;
    }
    struct FsFile* slot = &g_files[handle - 1];
    if (!slot->used || slot->fp == NULL) {
        return NULL;
    }
    return slot->fp;
}

static int64_t fs_alloc(FILE* fp) {
    for (int i = 0; i < FS_MAX_HANDLES; ++i) {
        if (!g_files[i].used) {
            g_files[i].fp = fp;
            g_files[i].used = 1;
            return (int64_t)(i + 1);
        }
    }
    fs_set_err(EMFILE);
    return -1;
}

static const char* fs_fopen_mode(int32_t mode) {
    switch (mode) {
        case FS_MODE_READ:
            return "rb";
        case FS_MODE_WRITE:
            return "wb";
        case FS_MODE_APPEND:
            return "ab";
        case FS_MODE_READ_WRITE:
            return "w+b";
        case FS_MODE_READ_WRITE_EXISTING:
            return "r+b";
        default:
            return "rb";
    }
}

int32_t xl_filesystem_errno(void) {
    return (int32_t)g_fs_errno;
}

int64_t xl_filesystem_open(const char* path, int32_t mode) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        return -1;
    }
    FILE* fp = fopen(path, fs_fopen_mode(mode));
    if (fp == NULL) {
        fs_set_err(errno);
        return -1;
    }
    const int64_t handle = fs_alloc(fp);
    if (handle < 0) {
        fclose(fp);
        return -1;
    }
    return handle;
}

int32_t xl_filesystem_close(int64_t handle) {
    fs_set_err(0);
    if (handle <= 0 || handle > FS_MAX_HANDLES) {
        fs_set_err(EBADF);
        return 1;
    }
    struct FsFile* slot = &g_files[handle - 1];
    if (!slot->used || slot->fp == NULL) {
        fs_set_err(EBADF);
        return 1;
    }
    const int rc = fclose(slot->fp);
    slot->fp = NULL;
    slot->used = 0;
    if (rc != 0) {
        fs_set_err(errno);
        return 1;
    }
    return 0;
}

const char* xl_filesystem_read(int64_t handle, int32_t max) {
    fs_set_err(0);
    FILE* fp = fs_fp(handle);
    if (fp == NULL) {
        fs_set_err(EBADF);
        return fs_empty();
    }
    if (max <= 0) {
        return fs_empty();
    }
    size_t cap = (size_t)max;
    if (cap > sizeof(g_io_buffer) - 1) {
        cap = sizeof(g_io_buffer) - 1;
    }
    const size_t n = fread(g_io_buffer, 1, cap, fp);
    g_io_buffer[n] = '\0';
    if (n == 0 && ferror(fp)) {
        fs_set_err(errno);
        clearerr(fp);
        return fs_empty();
    }
    return g_io_buffer;
}

int32_t xl_filesystem_write(int64_t handle, const char* data) {
    fs_set_err(0);
    FILE* fp = fs_fp(handle);
    if (fp == NULL) {
        fs_set_err(EBADF);
        return -1;
    }
    const char* payload = data != NULL ? data : "";
    const size_t len = strlen(payload);
    if (len == 0) {
        return 0;
    }
    const size_t n = fwrite(payload, 1, len, fp);
    if (n != len) {
        fs_set_err(errno);
        return -1;
    }
    return (int32_t)n;
}

int32_t xl_filesystem_write_n(int64_t handle, const char* data, int32_t n) {
    fs_set_err(0);
    FILE* fp = fs_fp(handle);
    if (fp == NULL) {
        fs_set_err(EBADF);
        return -1;
    }
    if (n <= 0) {
        return 0;
    }
    const char* payload = data != NULL ? data : "";
    const size_t want = (size_t)n;
    const size_t wrote = fwrite(payload, 1, want, fp);
    if (wrote != want) {
        fs_set_err(errno);
        return -1;
    }
    return (int32_t)wrote;
}

int64_t xl_filesystem_seek(int64_t handle, int64_t offset, int32_t whence) {
    fs_set_err(0);
    FILE* fp = fs_fp(handle);
    if (fp == NULL) {
        fs_set_err(EBADF);
        return -1;
    }
    int origin = SEEK_SET;
    if (whence == FS_SEEK_CUR) {
        origin = SEEK_CUR;
    } else if (whence == FS_SEEK_END) {
        origin = SEEK_END;
    }
    if (fseeko(fp, (off_t)offset, origin) != 0) {
        fs_set_err(errno);
        return -1;
    }
    return (int64_t)ftello(fp);
}

int64_t xl_filesystem_tell(int64_t handle) {
    fs_set_err(0);
    FILE* fp = fs_fp(handle);
    if (fp == NULL) {
        fs_set_err(EBADF);
        return -1;
    }
    const off_t pos = ftello(fp);
    if (pos < 0) {
        fs_set_err(errno);
        return -1;
    }
    return (int64_t)pos;
}

int32_t xl_filesystem_flush(int64_t handle) {
    fs_set_err(0);
    FILE* fp = fs_fp(handle);
    if (fp == NULL) {
        fs_set_err(EBADF);
        return 1;
    }
    if (fflush(fp) != 0) {
        fs_set_err(errno);
        return 1;
    }
    return 0;
}

int32_t xl_filesystem_truncate(int64_t handle, int64_t size) {
    fs_set_err(0);
    FILE* fp = fs_fp(handle);
    if (fp == NULL) {
        fs_set_err(EBADF);
        return 1;
    }
    const int fd = fileno(fp);
    if (fd < 0) {
        fs_set_err(errno);
        return 1;
    }
    if (fflush(fp) != 0) {
        fs_set_err(errno);
        return 1;
    }
    if (ftruncate(fd, (off_t)size) != 0) {
        fs_set_err(errno);
        return 1;
    }
    return 0;
}

const char* xl_filesystem_read_all(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        return fs_empty();
    }
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        fs_set_err(errno);
        return fs_empty();
    }
    if (fseeko(fp, 0, SEEK_END) != 0) {
        fs_set_err(errno);
        fclose(fp);
        return fs_empty();
    }
    const off_t size = ftello(fp);
    if (size < 0) {
        fs_set_err(errno);
        fclose(fp);
        return fs_empty();
    }
    if (fseeko(fp, 0, SEEK_SET) != 0) {
        fs_set_err(errno);
        fclose(fp);
        return fs_empty();
    }
    size_t cap = (size_t)size;
    if (cap > sizeof(g_io_buffer) - 1) {
        cap = sizeof(g_io_buffer) - 1;
        fs_set_err(EFBIG);
    }
    const size_t n = fread(g_io_buffer, 1, cap, fp);
    g_io_buffer[n] = '\0';
    if (ferror(fp)) {
        fs_set_err(errno);
        clearerr(fp);
        fclose(fp);
        return fs_empty();
    }
    fclose(fp);
    return g_io_buffer;
}

int32_t xl_filesystem_write_all(const char* path, const char* data) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        return 1;
    }
    FILE* fp = fopen(path, "wb");
    if (fp == NULL) {
        fs_set_err(errno);
        return 1;
    }
    const char* payload = data != NULL ? data : "";
    const size_t len = strlen(payload);
    if (len > 0 && fwrite(payload, 1, len, fp) != len) {
        fs_set_err(errno);
        fclose(fp);
        return 1;
    }
    if (fclose(fp) != 0) {
        fs_set_err(errno);
        return 1;
    }
    return 0;
}

int32_t xl_filesystem_append_all(const char* path, const char* data) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        return 1;
    }
    FILE* fp = fopen(path, "ab");
    if (fp == NULL) {
        fs_set_err(errno);
        return 1;
    }
    const char* payload = data != NULL ? data : "";
    const size_t len = strlen(payload);
    if (len > 0 && fwrite(payload, 1, len, fp) != len) {
        fs_set_err(errno);
        fclose(fp);
        return 1;
    }
    if (fclose(fp) != 0) {
        fs_set_err(errno);
        return 1;
    }
    return 0;
}

int32_t xl_filesystem_exists(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        fs_set_err(errno);
        return 0;
    }
    return 1;
}

int32_t xl_filesystem_is_file(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        fs_set_err(errno);
        return 0;
    }
    return S_ISREG(st.st_mode) ? 1 : 0;
}

int32_t xl_filesystem_is_dir(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        fs_set_err(errno);
        return 0;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

int32_t xl_filesystem_is_symlink(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    struct stat st;
    if (lstat(path, &st) != 0) {
        fs_set_err(errno);
        return 0;
    }
    return S_ISLNK(st.st_mode) ? 1 : 0;
}

int64_t xl_filesystem_size(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        return -1;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        fs_set_err(errno);
        return -1;
    }
    return (int64_t)st.st_size;
}

int64_t xl_filesystem_mtime(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        return -1;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        fs_set_err(errno);
        return -1;
    }
    return (int64_t)st.st_mtime;
}

int32_t xl_filesystem_mkdir(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        return 1;
    }
    if (mkdir(path, 0755) != 0) {
        fs_set_err(errno);
        return 1;
    }
    return 0;
}

int32_t xl_filesystem_mkdir_all(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        return 1;
    }
    char tmp[PATH_MAX];
    const size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) {
        fs_set_err(ENAMETOOLONG);
        return 1;
    }
    memcpy(tmp, path, len + 1);

    for (char* p = tmp + 1; *p != '\0'; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (tmp[0] != '\0' && mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            fs_set_err(errno);
            return 1;
        }
        *p = '/';
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        fs_set_err(errno);
        return 1;
    }
    fs_set_err(0);
    return 0;
}

int32_t xl_filesystem_remove(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        return 1;
    }
    if (unlink(path) != 0) {
        fs_set_err(errno);
        return 1;
    }
    return 0;
}

int32_t xl_filesystem_rmdir(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        return 1;
    }
    if (rmdir(path) != 0) {
        fs_set_err(errno);
        return 1;
    }
    return 0;
}

int32_t xl_filesystem_rename(const char* from, const char* to) {
    fs_set_err(0);
    if (from == NULL || to == NULL || from[0] == '\0' || to[0] == '\0') {
        fs_set_err(EINVAL);
        return 1;
    }
    if (rename(from, to) != 0) {
        fs_set_err(errno);
        return 1;
    }
    return 0;
}

int32_t xl_filesystem_copy(const char* from, const char* to) {
    fs_set_err(0);
    if (from == NULL || to == NULL || from[0] == '\0' || to[0] == '\0') {
        fs_set_err(EINVAL);
        return 1;
    }
    FILE* in = fopen(from, "rb");
    if (in == NULL) {
        fs_set_err(errno);
        return 1;
    }
    FILE* out = fopen(to, "wb");
    if (out == NULL) {
        fs_set_err(errno);
        fclose(in);
        return 1;
    }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fs_set_err(errno);
            fclose(in);
            fclose(out);
            return 1;
        }
    }
    if (ferror(in)) {
        fs_set_err(errno);
        fclose(in);
        fclose(out);
        return 1;
    }
    fclose(in);
    if (fclose(out) != 0) {
        fs_set_err(errno);
        return 1;
    }
    return 0;
}

int32_t xl_filesystem_chmod(const char* path, int32_t mode) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        return 1;
    }
    if (chmod(path, (mode_t)mode) != 0) {
        fs_set_err(errno);
        return 1;
    }
    return 0;
}

const char* xl_filesystem_cwd(void) {
    fs_set_err(0);
    if (getcwd(g_path_buffer, sizeof(g_path_buffer)) == NULL) {
        fs_set_err(errno);
        g_path_buffer[0] = '\0';
        return g_path_buffer;
    }
    return g_path_buffer;
}

int32_t xl_filesystem_chdir(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        return 1;
    }
    if (chdir(path) != 0) {
        fs_set_err(errno);
        return 1;
    }
    return 0;
}

const char* xl_filesystem_realpath(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        g_path_buffer[0] = '\0';
        return g_path_buffer;
    }
    if (realpath(path, g_path_buffer) == NULL) {
        fs_set_err(errno);
        g_path_buffer[0] = '\0';
        return g_path_buffer;
    }
    return g_path_buffer;
}

const char* xl_filesystem_read_dir(const char* path) {
    fs_set_err(0);
    if (path == NULL || path[0] == '\0') {
        fs_set_err(EINVAL);
        return fs_empty();
    }
    DIR* dir = opendir(path);
    if (dir == NULL) {
        fs_set_err(errno);
        return fs_empty();
    }

    size_t used = 0;
    g_io_buffer[0] = '\0';
    struct dirent* ent;
    errno = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        const size_t name_len = strlen(ent->d_name);
        if (used + name_len + 2 >= sizeof(g_io_buffer)) {
            fs_set_err(ENOMEM);
            closedir(dir);
            return fs_empty();
        }
        if (used > 0) {
            g_io_buffer[used++] = '\n';
        }
        memcpy(g_io_buffer + used, ent->d_name, name_len);
        used += name_len;
        g_io_buffer[used] = '\0';
    }
    if (errno != 0) {
        fs_set_err(errno);
        closedir(dir);
        return fs_empty();
    }
    closedir(dir);
    return g_io_buffer;
}

/* Compatibility helpers used by older frontend call shapes. */
const char* xl_filesystem_read_handle(int64_t handle) {
    fs_set_err(0);
    FILE* fp = fs_fp(handle);
    if (fp == NULL) {
        fs_set_err(EBADF);
        return fs_empty();
    }
    const off_t start = ftello(fp);
    if (start < 0) {
        fs_set_err(errno);
        return fs_empty();
    }
    if (fseeko(fp, 0, SEEK_END) != 0) {
        fs_set_err(errno);
        return fs_empty();
    }
    const off_t end = ftello(fp);
    if (end < 0 || fseeko(fp, start, SEEK_SET) != 0) {
        fs_set_err(errno);
        return fs_empty();
    }
    size_t cap = (size_t)(end - start);
    if (cap > sizeof(g_io_buffer) - 1) {
        cap = sizeof(g_io_buffer) - 1;
    }
    const size_t n = fread(g_io_buffer, 1, cap, fp);
    g_io_buffer[n] = '\0';
    if (ferror(fp)) {
        fs_set_err(errno);
        clearerr(fp);
        return fs_empty();
    }
    return g_io_buffer;
}

int32_t xl_filesystem_write_handle(int64_t handle, const char* data) {
    const int32_t n = xl_filesystem_write(handle, data);
    if (n < 0) {
        return 1;
    }
    return xl_filesystem_flush(handle);
}
