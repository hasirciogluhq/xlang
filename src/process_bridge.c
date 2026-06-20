#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static __thread char capture_buffer[1024 * 1024];
static __thread int capture_len;

static void reset_capture(void) {
    capture_len = 0;
    capture_buffer[0] = '\0';
}

static void append_capture(const char* data, int len) {
    if (len <= 0 || data == NULL) {
        return;
    }
    const int room = (int)sizeof(capture_buffer) - 1 - capture_len;
    if (room <= 0) {
        return;
    }
    const int n = len < room ? len : room;
    memcpy(capture_buffer + capture_len, data, (size_t)n);
    capture_len += n;
    capture_buffer[capture_len] = '\0';
}

const char* xlang_capture_stdout(void) {
    return capture_buffer;
}

static int split_args(const char* blob, char* argv[], int max_argv) {
    if (blob == NULL) {
        return 0;
    }
    int count = 0;
    const char* start = blob;
    for (const char* p = blob;; ++p) {
        if (*p == '\n' || *p == '\0') {
            if (p > start && count < max_argv - 1) {
                argv[count] = strndup(start, (size_t)(p - start));
                ++count;
            }
            if (*p == '\0') {
                break;
            }
            start = p + 1;
        }
    }
    argv[count] = NULL;
    return count;
}

int xlang_run_capture(const char* path, const char* args_blob) {
    reset_capture();
    if (path == NULL || path[0] == '\0') {
        return 127;
    }

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return 1;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return 1;
    }

    if (pid == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        char* argv[64];
        argv[0] = strdup(path);
        (void)split_args(args_blob, argv + 1, 63);
        execv(path, argv);
        _exit(127);
    }

    close(pipe_fds[1]);
    char chunk[4096];
    while (1) {
        const ssize_t n = read(pipe_fds[0], chunk, sizeof(chunk));
        if (n <= 0) {
            break;
        }
        append_capture(chunk, (int)n);
    }
    close(pipe_fds[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return 1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

int32_t xlang_proc_fork(void) {
    return (int32_t)fork();
}

int32_t xlang_proc_exec(const char* path, const char* args_blob) {
    if (path == NULL) {
        return 1;
    }
    char* argv[64];
    argv[0] = strdup(path);
    split_args(args_blob, argv + 1, 63);
    execv(path, argv);
    return 127;
}

int32_t xlang_proc_wait(int32_t pid) {
    int status = 0;
    if (waitpid((pid_t)pid, &status, 0) < 0) {
        return 1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

void xlang_proc_exit(int32_t code) {
    _exit(code);
}

int64_t xlang_pipe_create(void) {
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return -1;
    }
    return ((int64_t)pipe_fds[0] << 32) | (uint32_t)pipe_fds[1];
}

int32_t xlang_fd_close(int32_t fd) {
    return close(fd);
}

const char* xlang_env_get(const char* key) {
    if (key == NULL) {
        return "";
    }
    const char* value = getenv(key);
    return value != NULL ? value : "";
}
