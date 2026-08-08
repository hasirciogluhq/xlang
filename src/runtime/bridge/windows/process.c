/* process — Windows stub ABI (unsupported). */
#include <stdint.h>

const char* xl_capture_stdout(void) { return ""; }
int xl_run_capture(const char* path, const char* args_blob) {
    (void)path; (void)args_blob; return -1;
}
int32_t xl_proc_fork(void) { return -1; }
int32_t xl_proc_exec(const char* path, const char* args_blob) {
    (void)path; (void)args_blob; return -1;
}
int32_t xl_proc_wait(int32_t pid) { (void)pid; return -1; }
void xl_proc_exit(int32_t code) { (void)code; }
int64_t xl_pipe_create(void) { return -1; }
int32_t xl_fd_close(int32_t fd) { (void)fd; return -1; }
const char* xl_env_get(const char* key) { (void)key; return ""; }
int32_t xl_env_set(const char* key, const char* value) {
    (void)key; (void)value; return -1;
}
const char* xl_cwd_get(void) { return ""; }
int32_t xl_chdir(const char* path) { (void)path; return -1; }
const char* xl_fd_read(int32_t fd, int32_t max) {
    (void)fd; (void)max; return "";
}
int32_t xl_fd_write(int32_t fd, const char* data) {
    (void)fd; (void)data; return -1;
}
int32_t xl_fd_dup2(int32_t old_fd, int32_t new_fd) {
    (void)old_fd; (void)new_fd; return -1;
}
int32_t xl_proc_kill(int32_t pid, int32_t sig) {
    (void)pid; (void)sig; return -1;
}
int32_t xl_pipe_read_fd(int64_t handle) { (void)handle; return -1; }
int32_t xl_pipe_write_fd(int64_t handle) { (void)handle; return -1; }
