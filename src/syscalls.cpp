#include "xlang/syscalls.h"

#include "xlang/error.h"

#if defined(__linux__)
constexpr int kScNprocessorsOnln = 84;
#elif defined(__APPLE__)
constexpr int kScNprocessorsOnln = 58;
#else
constexpr int kScNprocessorsOnln = 58;
#endif

namespace xlang {

bool isKnownSyscall(const std::string& name) {
    return name == "start_thread" || name == "sleep_ms" ||
           name == "random_range" || name == "print_done" || name == "wait_all_jobs" ||
           name == "cpu_count" || name == "mutex_init" || name == "mutex_lock" ||
           name == "mutex_unlock" || name == "cond_init" || name == "cond_wait" ||
           name == "cond_signal" || name == "cond_broadcast" ||
           name == "panic" || name == "recover" || name == "try_invoke0" ||
           name == "env_get" || name == "run_capture" || name == "capture_stdout" ||
           name == "env_set" || name == "cwd" || name == "chdir" ||
           name == "proc_fork" || name == "proc_exec" || name == "proc_wait" ||
           name == "proc_exit" || name == "proc_kill" || name == "pipe_create" ||
           name == "pipe_read_fd" || name == "pipe_write_fd" ||
           name == "fd_close" || name == "fd_read" || name == "fd_write" ||
           name == "fd_dup2" || name == "file_read" ||
           name == "net_tcp_connect" || name == "net_send" || name == "net_recv" ||
           name == "net_close" || name == "net_tcp_listen" || name == "net_tcp_accept" ||
           name == "net_tls_connect" || name == "net_tls_send" ||
           name == "net_tls_recv" || name == "net_tls_close";
}

namespace {

void emitThreadSupport(std::string& output) {
    output += "; xlang thread pool (pthread backend)\n";
    output += "declare i32 @pthread_create(i64*, i8*, i8* (i8*)*, i8*)\n";
    output += "declare i32 @pthread_join(i64, i8**)\n";
    output += "%__xlang_thread_ctx = type { i32 (i32)*, i32 }\n";
    output += "@__xlang_thread_slots = weak global [64 x i64] zeroinitializer\n";
    output += "@__xlang_thread_len = weak global i32 0\n";
    output += "define internal i8* @__xlang_thread_bootstrap(i8* %ctx) {\n";
    output += "  %pair = bitcast i8* %ctx to %__xlang_thread_ctx*\n";
    output += "  %fn_ptr = getelementptr %__xlang_thread_ctx, %__xlang_thread_ctx* %pair, i32 0, i32 0\n";
    output += "  %fn = load i32 (i32)*, i32 (i32)** %fn_ptr\n";
    output += "  %arg_ptr = getelementptr %__xlang_thread_ctx, %__xlang_thread_ctx* %pair, i32 0, i32 1\n";
    output += "  %arg = load i32, i32* %arg_ptr\n";
    output += "  call i32 %fn(i32 %arg)\n";
    output += "  call void @free(i8* %ctx)\n";
    output += "  ret i8* null\n";
    output += "}\n\n";
}

void emitStartThread(std::string& output) {
    output += "; xlang syscall: start_thread\n";
    output += "define weak i32 @start_thread(i64 %entry_ptr, i32 %arg) {\n";
    output += "entry:\n";
    output += "  %fn = inttoptr i64 %entry_ptr to i32 (i32)*\n";
    output += "  %ctx = call i8* @malloc(i64 16)\n";
    output += "  %pair = bitcast i8* %ctx to %__xlang_thread_ctx*\n";
    output += "  %fn_ptr = getelementptr %__xlang_thread_ctx, %__xlang_thread_ctx* %pair, i32 0, i32 0\n";
    output += "  store i32 (i32)* %fn, i32 (i32)** %fn_ptr\n";
    output += "  %arg_ptr = getelementptr %__xlang_thread_ctx, %__xlang_thread_ctx* %pair, i32 0, i32 1\n";
    output += "  store i32 %arg, i32* %arg_ptr\n";
    output += "  %tid = alloca i64, align 8\n";
    output += "  %rc = call i32 @pthread_create(i64* %tid, i8* null, "
              "i8* (i8*)* @__xlang_thread_bootstrap, i8* %ctx)\n";
    output += "  %len = load i32, i32* @__xlang_thread_len\n";
    output += "  %slot = getelementptr [64 x i64], [64 x i64]* @__xlang_thread_slots, i32 0, i32 %len\n";
    output += "  %tid_val = load i64, i64* %tid\n";
    output += "  store i64 %tid_val, i64* %slot\n";
    output += "  %next = add i32 %len, 1\n";
    output += "  store i32 %next, i32* @__xlang_thread_len\n";
    output += "  ret i32 %rc\n";
    output += "}\n\n";
}

void emitWaitAllJobs(std::string& output) {
    output += "; xlang syscall: wait_all_jobs\n";
    output += "define weak i32 @wait_all_jobs() {\n";
    output += "entry:\n";
    output += "  %len = load i32, i32* @__xlang_thread_len\n";
    output += "  br label %loop\n";
    output += "loop:\n";
    output += "  %i = phi i32 [ 0, %entry ], [ %next, %body ]\n";
    output += "  %done = icmp uge i32 %i, %len\n";
    output += "  br i1 %done, label %exit, label %body\n";
    output += "body:\n";
    output += "  %slot = getelementptr [64 x i64], [64 x i64]* @__xlang_thread_slots, i32 0, i32 %i\n";
    output += "  %tid = load i64, i64* %slot\n";
    output += "  call i32 @pthread_join(i64 %tid, i8** null)\n";
    output += "  %next = add i32 %i, 1\n";
    output += "  br label %loop\n";
    output += "exit:\n";
    output += "  store i32 0, i32* @__xlang_thread_len\n";
    output += "  ret i32 0\n";
    output += "}\n\n";
}

void emitSyncSupport(std::string& output) {
    output += "; xlang OS bridge syscalls (cpu + pthread sync)\n";
    output += "declare i64 @sysconf(i64)\n";
    output += "declare i32 @pthread_mutex_init(i8*, i8*)\n";
    output += "declare i32 @pthread_mutex_lock(i8*)\n";
    output += "declare i32 @pthread_mutex_unlock(i8*)\n";
    output += "declare i32 @pthread_cond_init(i8*, i8*)\n";
    output += "declare i32 @pthread_cond_wait(i8*, i8*)\n";
    output += "declare i32 @pthread_cond_signal(i8*)\n";
    output += "declare i32 @pthread_cond_broadcast(i8*)\n\n";

    output += "define weak i32 @cpu_count() {\n";
    output += "  %cpus = call i64 @sysconf(i64 " + std::to_string(kScNprocessorsOnln) + ")\n";
    output += "  %cpus32 = trunc i64 %cpus to i32\n";
    output += "  %bad = icmp slt i32 %cpus32, 1\n";
    output += "  %result = select i1 %bad, i32 1, i32 %cpus32\n";
    output += "  ret i32 %result\n";
    output += "}\n\n";

    output += "define weak i64 @mutex_init() {\n";
    output += "  %m = call i8* @malloc(i64 64)\n";
    output += "  call i32 @pthread_mutex_init(i8* %m, i8* null)\n";
    output += "  %h = ptrtoint i8* %m to i64\n";
    output += "  ret i64 %h\n";
    output += "}\n\n";

    output += "define weak i32 @mutex_lock(i64 %handle) {\n";
    output += "  %m = inttoptr i64 %handle to i8*\n";
    output += "  call i32 @pthread_mutex_lock(i8* %m)\n";
    output += "  ret i32 0\n";
    output += "}\n\n";

    output += "define weak i32 @mutex_unlock(i64 %handle) {\n";
    output += "  %m = inttoptr i64 %handle to i8*\n";
    output += "  call i32 @pthread_mutex_unlock(i8* %m)\n";
    output += "  ret i32 0\n";
    output += "}\n\n";

    output += "define weak i64 @cond_init() {\n";
    output += "  %c = call i8* @malloc(i64 64)\n";
    output += "  call i32 @pthread_cond_init(i8* %c, i8* null)\n";
    output += "  %h = ptrtoint i8* %c to i64\n";
    output += "  ret i64 %h\n";
    output += "}\n\n";

    output += "define weak i32 @cond_wait(i64 %cond_handle, i64 %mutex_handle) {\n";
    output += "  %c = inttoptr i64 %cond_handle to i8*\n";
    output += "  %m = inttoptr i64 %mutex_handle to i8*\n";
    output += "  call i32 @pthread_cond_wait(i8* %c, i8* %m)\n";
    output += "  ret i32 0\n";
    output += "}\n\n";

    output += "define weak i32 @cond_signal(i64 %cond_handle) {\n";
    output += "  %c = inttoptr i64 %cond_handle to i8*\n";
    output += "  call i32 @pthread_cond_signal(i8* %c)\n";
    output += "  ret i32 0\n";
    output += "}\n\n";

    output += "define weak i32 @cond_broadcast(i64 %cond_handle) {\n";
    output += "  %c = inttoptr i64 %cond_handle to i8*\n";
    output += "  call i32 @pthread_cond_broadcast(i8* %c)\n";
    output += "  ret i32 0\n";
    output += "}\n\n";
}

#if defined(__APPLE__)
constexpr int kAddrinfoAddrlenOff = 16;
constexpr int kAddrinfoAddrOff = 32;
constexpr int kAddrinfoNextOff = 40;
constexpr int kAddrinfoFamilyOff = 4;
#elif defined(__linux__)
constexpr int kAddrinfoAddrlenOff = 16;
constexpr int kAddrinfoAddrOff = 24;
constexpr int kAddrinfoNextOff = 40;
constexpr int kAddrinfoFamilyOff = 4;
#else
constexpr int kAddrinfoAddrlenOff = 16;
constexpr int kAddrinfoAddrOff = 32;
constexpr int kAddrinfoNextOff = 40;
constexpr int kAddrinfoFamilyOff = 4;
#endif

void emitNetSupport(std::string& output) {
    output += "; xlang OS bridge syscalls (TCP sockets)\n";
    output += "declare i32 @getaddrinfo(i8*, i8*, i8*, i8**)\n";
    output += "declare void @freeaddrinfo(i8*)\n";
    output += "declare i32 @socket(i32, i32, i32)\n";
    output += "declare i32 @connect(i32, i8*, i32)\n";
    output += "declare i32 @close(i32)\n";
    output += "declare i64 @send(i32, i8*, i64, i32)\n";
    output += "declare i64 @recv(i32, i8*, i64, i32)\n";
    output += "@__xlang_port_fmt = private unnamed_addr constant [3 x i8] c\"%d\\00\"\n\n";

    output += "define internal i8* @__xlang_port_to_str(i32 %port) {\n";
    output += "  %buf = call i8* @malloc(i64 16)\n";
    output += "  call i32 (i8*, i8*, ...) @sprintf(i8* %buf, i8* getelementptr inbounds "
              "([3 x i8], [3 x i8]* @__xlang_port_fmt, i32 0, i32 0), i32 %port)\n";
    output += "  ret i8* %buf\n";
    output += "}\n\n";

    output += "define weak i64 @net_tcp_connect(i8* %host, i32 %port) {\n";
    output += "entry:\n";
    output += "  %portbuf = call i8* @__xlang_port_to_str(i32 %port)\n";
    output += "  %res_ptr = alloca i8*, align 8\n";
    output += "  %ga_rc = call i32 @getaddrinfo(i8* %host, i8* %portbuf, i8* null, i8** %res_ptr)\n";
    output += "  call void @free(i8* %portbuf)\n";
    output += "  %fail_ga = icmp ne i32 %ga_rc, 0\n";
    output += "  br i1 %fail_ga, label %fail_out, label %try_init\n";
    output += "try_init:\n";
    output += "  %head = load i8*, i8** %res_ptr\n";
    output += "  br label %try_loop\n";
    output += "try_loop:\n";
    output += "  %node = phi i8* [ %head, %try_init ], [ %next, %next_node ]\n";
    output += "  %empty = icmp eq i8* %node, null\n";
    output += "  br i1 %empty, label %fail_free, label %do_try\n";
    output += "do_try:\n";
    output += "  %family_p = getelementptr i8, i8* %node, i64 " +
              std::to_string(kAddrinfoFamilyOff) + "\n";
    output += "  %family = load i32, i32* %family_p\n";
    output += "  %addrlen_p = getelementptr i8, i8* %node, i64 " +
              std::to_string(kAddrinfoAddrlenOff) + "\n";
    output += "  %addrlen = load i32, i32* %addrlen_p\n";
    output += "  %addr_ptr_p = getelementptr i8, i8* %node, i64 " +
              std::to_string(kAddrinfoAddrOff) + "\n";
    output += "  %addr_ptr = load i8*, i8** %addr_ptr_p\n";
    output += "  %sock = call i32 @socket(i32 %family, i32 1, i32 0)\n";
    output += "  %sock_bad = icmp slt i32 %sock, 0\n";
    output += "  br i1 %sock_bad, label %next_node, label %do_connect\n";
    output += "do_connect:\n";
    output += "  %conn = call i32 @connect(i32 %sock, i8* %addr_ptr, i32 %addrlen)\n";
    output += "  %conn_ok = icmp eq i32 %conn, 0\n";
    output += "  br i1 %conn_ok, label %success, label %close_sock\n";
    output += "close_sock:\n";
    output += "  call i32 @close(i32 %sock)\n";
    output += "  br label %next_node\n";
    output += "next_node:\n";
    output += "  %next_p = getelementptr i8, i8* %node, i64 " +
              std::to_string(kAddrinfoNextOff) + "\n";
    output += "  %next = load i8*, i8** %next_p\n";
    output += "  br label %try_loop\n";
    output += "success:\n";
    output += "  %res_list = load i8*, i8** %res_ptr\n";
    output += "  call void @freeaddrinfo(i8* %res_list)\n";
    output += "  %fd64 = sext i32 %sock to i64\n";
    output += "  ret i64 %fd64\n";
    output += "fail_free:\n";
    output += "  %res_fail = load i8*, i8** %res_ptr\n";
    output += "  call void @freeaddrinfo(i8* %res_fail)\n";
    output += "  br label %fail_out\n";
    output += "fail_out:\n";
    output += "  ret i64 -1\n";
    output += "}\n\n";

    output += "define weak i32 @net_send(i64 %fd, i8* %data) {\n";
    output += "  %fd32 = trunc i64 %fd to i32\n";
    output += "  %len = call i64 @strlen(i8* %data)\n";
    output += "  %sent = call i64 @send(i32 %fd32, i8* %data, i64 %len, i32 0)\n";
    output += "  %sent32 = trunc i64 %sent to i32\n";
    output += "  ret i32 %sent32\n";
    output += "}\n\n";

    output += "define weak i8* @net_recv(i64 %fd, i32 %max) {\n";
    output += "entry:\n";
    output += "  %max64 = sext i32 %max to i64\n";
    output += "  %buf = call i8* @malloc(i64 %max64)\n";
    output += "  %fd32 = trunc i64 %fd to i32\n";
    output += "  %n = call i64 @recv(i32 %fd32, i8* %buf, i64 %max64, i32 0)\n";
    output += "  %bad = icmp sle i64 %n, 0\n";
    output += "  br i1 %bad, label %empty, label %term\n";
    output += "empty:\n";
    output += "  call void @free(i8* %buf)\n";
    output += "  %z = call i8* @malloc(i64 1)\n";
    output += "  store i8 0, i8* %z\n";
    output += "  ret i8* %z\n";
    output += "term:\n";
    output += "  %n1 = add i64 %n, 1\n";
    output += "  %buf2 = call i8* @realloc(i8* %buf, i64 %n1)\n";
    output += "  %end = getelementptr i8, i8* %buf2, i64 %n\n";
    output += "  store i8 0, i8* %end\n";
    output += "  ret i8* %buf2\n";
    output += "}\n\n";

    output += "define weak i32 @net_close(i64 %fd) {\n";
    output += "  %fd32 = trunc i64 %fd to i32\n";
    output += "  %rc = call i32 @close(i32 %fd32)\n";
    output += "  ret i32 %rc\n";
    output += "}\n\n";
}

void emitNetServerSupport(std::string& output) {
    output += "; xlang OS bridge syscalls (TCP server listen/accept)\n";
    output += "declare i64 @xlang_net_tcp_listen(i8*, i32)\n";
    output += "declare i64 @xlang_net_tcp_accept(i64)\n\n";

    output += "define weak i64 @net_tcp_listen(i8* %host, i32 %port) {\n";
    output += "  %fd = call i64 @xlang_net_tcp_listen(i8* %host, i32 %port)\n";
    output += "  ret i64 %fd\n";
    output += "}\n\n";

    output += "define weak i64 @net_tcp_accept(i64 %listen_fd) {\n";
    output += "  %fd = call i64 @xlang_net_tcp_accept(i64 %listen_fd)\n";
    output += "  ret i64 %fd\n";
    output += "}\n\n";
}

void emitTlsSupport(std::string& output) {
    output += "; xlang OS bridge syscalls (TLS via OpenSSL)\n";
    output += "declare i64 @xlang_tls_connect(i8*, i32)\n";
    output += "declare i32 @xlang_tls_send(i64, i8*)\n";
    output += "declare i8* @xlang_tls_recv(i64, i32)\n";
    output += "declare i32 @xlang_tls_close(i64)\n\n";

    output += "define weak i64 @net_tls_connect(i8* %host, i32 %port) {\n";
    output += "  %fd = call i64 @xlang_tls_connect(i8* %host, i32 %port)\n";
    output += "  ret i64 %fd\n";
    output += "}\n\n";

    output += "define weak i32 @net_tls_send(i64 %fd, i8* %data) {\n";
    output += "  %n = call i32 @xlang_tls_send(i64 %fd, i8* %data)\n";
    output += "  ret i32 %n\n";
    output += "}\n\n";

    output += "define weak i8* @net_tls_recv(i64 %fd, i32 %max) {\n";
    output += "  %buf = call i8* @xlang_tls_recv(i64 %fd, i32 %max)\n";
    output += "  ret i8* %buf\n";
    output += "}\n\n";

    output += "define weak i32 @net_tls_close(i64 %fd) {\n";
    output += "  %rc = call i32 @xlang_tls_close(i64 %fd)\n";
    output += "  ret i32 %rc\n";
    output += "}\n\n";
}

void emitPanicSupport(std::string& output) {
    output += "; xlang panic/recover bridge\n";
    output += "declare void @xlang_panic(i8*)\n";
    output += "declare i32 @xlang_try_enter()\n";
    output += "declare void @xlang_try_leave()\n";
    output += "declare i8* @xlang_recover_message()\n";
    output += "declare i32 @xlang_try_invoke0(i64)\n\n";

    output += "define weak i32 @panic(i8* %msg) {\n";
    output += "  call void @xlang_panic(i8* %msg)\n";
    output += "  ret i32 0\n";
    output += "}\n\n";

    output += "define weak i8* @recover() {\n";
    output += "  %msg = call i8* @xlang_recover_message()\n";
    output += "  ret i8* %msg\n";
    output += "}\n\n";

    output += "define weak i32 @try_invoke0(i64 %entry) {\n";
    output += "  %rc = call i32 @xlang_try_invoke0(i64 %entry)\n";
    output += "  ret i32 %rc\n";
    output += "}\n\n";
}

void emitProcessSupport(std::string& output) {
    output += "; xlang process bridge\n";
    output += "declare i32 @xlang_run_capture(i8*, i8*)\n";
    output += "declare i8* @xlang_capture_stdout()\n";
    output += "declare i8* @xlang_env_get(i8*)\n";
    output += "declare i32 @xlang_env_set(i8*, i8*)\n";
    output += "declare i8* @xlang_cwd_get()\n";
    output += "declare i32 @xlang_chdir(i8*)\n";
    output += "declare i32 @xlang_proc_fork()\n";
    output += "declare i32 @xlang_proc_exec(i8*, i8*)\n";
    output += "declare i32 @xlang_proc_wait(i32)\n";
    output += "declare void @xlang_proc_exit(i32)\n";
    output += "declare i32 @xlang_proc_kill(i32, i32)\n";
    output += "declare i64 @xlang_pipe_create()\n";
    output += "declare i32 @xlang_pipe_read_fd(i64)\n";
    output += "declare i32 @xlang_pipe_write_fd(i64)\n";
    output += "declare i32 @xlang_fd_close(i32)\n";
    output += "declare i8* @xlang_fd_read(i32, i32)\n";
    output += "declare i32 @xlang_fd_write(i32, i8*)\n";
    output += "declare i32 @xlang_fd_dup2(i32, i32)\n";
    output += "declare i8* @xlang_file_read(i8*)\n\n";

    output += "define weak i32 @run_capture(i8* %path, i8* %args) {\n";
    output += "  %rc = call i32 @xlang_run_capture(i8* %path, i8* %args)\n";
    output += "  ret i32 %rc\n";
    output += "}\n\n";

    output += "define weak i8* @capture_stdout() {\n";
    output += "  %out = call i8* @xlang_capture_stdout()\n";
    output += "  ret i8* %out\n";
    output += "}\n\n";

    output += "define weak i8* @env_get(i8* %key) {\n";
    output += "  %val = call i8* @xlang_env_get(i8* %key)\n";
    output += "  ret i8* %val\n";
    output += "}\n\n";

    output += "define weak i32 @env_set(i8* %key, i8* %value) {\n";
    output += "  %rc = call i32 @xlang_env_set(i8* %key, i8* %value)\n";
    output += "  ret i32 %rc\n";
    output += "}\n\n";

    output += "define weak i8* @cwd() {\n";
    output += "  %path = call i8* @xlang_cwd_get()\n";
    output += "  ret i8* %path\n";
    output += "}\n\n";

    output += "define weak i32 @chdir(i8* %path) {\n";
    output += "  %rc = call i32 @xlang_chdir(i8* %path)\n";
    output += "  ret i32 %rc\n";
    output += "}\n\n";

    output += "define weak i32 @proc_fork() {\n";
    output += "  %pid = call i32 @xlang_proc_fork()\n";
    output += "  ret i32 %pid\n";
    output += "}\n\n";

    output += "define weak i32 @proc_exec(i8* %path, i8* %args) {\n";
    output += "  %rc = call i32 @xlang_proc_exec(i8* %path, i8* %args)\n";
    output += "  ret i32 %rc\n";
    output += "}\n\n";

    output += "define weak i32 @proc_wait(i32 %pid) {\n";
    output += "  %rc = call i32 @xlang_proc_wait(i32 %pid)\n";
    output += "  ret i32 %rc\n";
    output += "}\n\n";

    output += "define weak i32 @proc_exit(i32 %code) {\n";
    output += "  call void @xlang_proc_exit(i32 %code)\n";
    output += "  ret i32 0\n";
    output += "}\n\n";

    output += "define weak i32 @proc_kill(i32 %pid, i32 %sig) {\n";
    output += "  %rc = call i32 @xlang_proc_kill(i32 %pid, i32 %sig)\n";
    output += "  ret i32 %rc\n";
    output += "}\n\n";

    output += "define weak i64 @pipe_create() {\n";
    output += "  %fds = call i64 @xlang_pipe_create()\n";
    output += "  ret i64 %fds\n";
    output += "}\n\n";

    output += "define weak i32 @pipe_read_fd(i64 %h) {\n";
    output += "  %fd = call i32 @xlang_pipe_read_fd(i64 %h)\n";
    output += "  ret i32 %fd\n";
    output += "}\n\n";

    output += "define weak i32 @pipe_write_fd(i64 %h) {\n";
    output += "  %fd = call i32 @xlang_pipe_write_fd(i64 %h)\n";
    output += "  ret i32 %fd\n";
    output += "}\n\n";

    output += "define weak i32 @fd_close(i32 %fd) {\n";
    output += "  %rc = call i32 @xlang_fd_close(i32 %fd)\n";
    output += "  ret i32 %rc\n";
    output += "}\n\n";

    output += "define weak i8* @fd_read(i32 %fd, i32 %max) {\n";
    output += "  %buf = call i8* @xlang_fd_read(i32 %fd, i32 %max)\n";
    output += "  ret i8* %buf\n";
    output += "}\n\n";

    output += "define weak i32 @fd_write(i32 %fd, i8* %data) {\n";
    output += "  %n = call i32 @xlang_fd_write(i32 %fd, i8* %data)\n";
    output += "  ret i32 %n\n";
    output += "}\n\n";

    output += "define weak i32 @fd_dup2(i32 %old_fd, i32 %new_fd) {\n";
    output += "  %rc = call i32 @xlang_fd_dup2(i32 %old_fd, i32 %new_fd)\n";
    output += "  ret i32 %rc\n";
    output += "}\n\n";

    output += "define weak i8* @file_read(i8* %path) {\n";
    output += "  %buf = call i8* @xlang_file_read(i8* %path)\n";
    output += "  ret i8* %buf\n";
    output += "}\n\n";
}

}  // namespace

// Compiler backend: xlang `declare syscall` primitiflerini LLVM IR'ye indirger.
// Kullanıcı kodu OS/pthread API görmez — sadece xlang syscall isimlerini kullanır.
void emitSyscallDefinitions(std::string& output, const std::unordered_set<std::string>& syscalls) {
    const bool needs_sync = syscalls.find("cpu_count") != syscalls.end() ||
                            syscalls.find("mutex_init") != syscalls.end() ||
                            syscalls.find("mutex_lock") != syscalls.end() ||
                            syscalls.find("mutex_unlock") != syscalls.end() ||
                            syscalls.find("cond_init") != syscalls.end() ||
                            syscalls.find("cond_wait") != syscalls.end() ||
                            syscalls.find("cond_signal") != syscalls.end() ||
                            syscalls.find("cond_broadcast") != syscalls.end();
    const bool needs_threads = syscalls.find("start_thread") != syscalls.end() ||
                               syscalls.find("wait_all_jobs") != syscalls.end() ||
                               needs_sync;
    const bool needs_printf = false;

    if (needs_printf) {
        output += "declare i32 @printf(i8*, ...)\n\n";
    }

    if (syscalls.find("sleep_ms") != syscalls.end()) {
        output += "; xlang syscall: sleep_ms\n";
        output += "declare i32 @usleep(i32)\n";
        output += "define weak i32 @sleep_ms(i32 %ms) {\n";
        output += "  %us = mul i32 %ms, 1000\n";
        output += "  call i32 @usleep(i32 %us)\n";
        output += "  ret i32 0\n";
        output += "}\n\n";
    }

    if (syscalls.find("random_range") != syscalls.end()) {
        output += "; xlang syscall: random_range\n";
        output += "declare i32 @rand()\n";
        output += "declare void @srand(i32)\n";
        output += "declare i64 @time(i8*)\n";
        output += "@__xlang_rand_seeded = weak global i1 false\n";
        output += "define weak i32 @random_range(i32 %min, i32 %max) {\n";
        output += "entry:\n";
        output += "  %seeded = load i1, i1* @__xlang_rand_seeded\n";
        output += "  br i1 %seeded, label %pick, label %seed\n";
        output += "seed:\n";
        output += "  %now = call i64 @time(i8* null)\n";
        output += "  %seed32 = trunc i64 %now to i32\n";
        output += "  call void @srand(i32 %seed32)\n";
        output += "  store i1 true, i1* @__xlang_rand_seeded\n";
        output += "  br label %pick\n";
        output += "pick:\n";
        output += "  %span = sub i32 %max, %min\n";
        output += "  %width = add i32 %span, 1\n";
        output += "  %raw = call i32 @rand()\n";
        output += "  %mod = srem i32 %raw, %width\n";
        output += "  %val = add i32 %min, %mod\n";
        output += "  ret i32 %val\n";
        output += "}\n\n";
    }

    if (syscalls.find("print_done") != syscalls.end()) {
        output += "; xlang syscall: print_done\n";
        output += "@__xlang_done_fmt = private unnamed_addr constant [16 x i8] c\"[job %d] bitti\\0A\\00\"\n";
        output += "define weak i32 @print_done(i32 %job_id) {\n";
        output += "  call i32 (i8*, ...) @printf(i8* getelementptr inbounds "
                  "([16 x i8], [16 x i8]* @__xlang_done_fmt, i32 0, i32 0), i32 %job_id)\n";
        output += "  ret i32 0\n";
        output += "}\n\n";
    }

    if (needs_threads) {
        emitThreadSupport(output);
        if (syscalls.find("start_thread") != syscalls.end()) {
            emitStartThread(output);
        }
        if (syscalls.find("wait_all_jobs") != syscalls.end()) {
            emitWaitAllJobs(output);
        }
    }

    if (needs_sync) {
        emitSyncSupport(output);
    }

    const bool needs_net = syscalls.find("net_tcp_connect") != syscalls.end() ||
                           syscalls.find("net_send") != syscalls.end() ||
                           syscalls.find("net_recv") != syscalls.end() ||
                           syscalls.find("net_close") != syscalls.end() ||
                           syscalls.find("net_tcp_listen") != syscalls.end() ||
                           syscalls.find("net_tcp_accept") != syscalls.end();
    if (needs_net) {
        emitNetSupport(output);
    }

    const bool needs_server = syscalls.find("net_tcp_listen") != syscalls.end() ||
                              syscalls.find("net_tcp_accept") != syscalls.end();
    if (needs_server) {
        emitNetServerSupport(output);
    }

    const bool needs_tls = syscalls.find("net_tls_connect") != syscalls.end() ||
                           syscalls.find("net_tls_send") != syscalls.end() ||
                           syscalls.find("net_tls_recv") != syscalls.end() ||
                           syscalls.find("net_tls_close") != syscalls.end();
    if (needs_tls) {
        emitTlsSupport(output);
    }

    const bool needs_panic = syscalls.find("panic") != syscalls.end() ||
                             syscalls.find("recover") != syscalls.end() ||
                             syscalls.find("try_invoke0") != syscalls.end();
    if (needs_panic) {
        emitPanicSupport(output);
    }

    const bool needs_process =
        syscalls.find("env_get") != syscalls.end() ||
        syscalls.find("run_capture") != syscalls.end() ||
        syscalls.find("capture_stdout") != syscalls.end() ||
        syscalls.find("proc_fork") != syscalls.end() ||
        syscalls.find("proc_exec") != syscalls.end() ||
        syscalls.find("proc_wait") != syscalls.end() ||
        syscalls.find("proc_exit") != syscalls.end() ||
        syscalls.find("pipe_create") != syscalls.end() ||
        syscalls.find("fd_close") != syscalls.end();
    if (needs_process) {
        emitProcessSupport(output);
    }

    for (const std::string& name : syscalls) {
        if (!isKnownSyscall(name)) {
            throw XlangError("unknown xlang syscall: " + name);
        }
    }
}

bool syscallsNeedThreadLink(const std::unordered_set<std::string>& syscalls) {
    return syscalls.find("start_thread") != syscalls.end() ||
           syscalls.find("wait_all_jobs") != syscalls.end() ||
           syscalls.find("mutex_init") != syscalls.end() ||
           syscalls.find("cond_init") != syscalls.end();
}

bool syscallsNeedSslLink(const std::unordered_set<std::string>& syscalls) {
    return syscalls.find("net_tls_connect") != syscalls.end() ||
           syscalls.find("net_tls_send") != syscalls.end() ||
           syscalls.find("net_tls_recv") != syscalls.end() ||
           syscalls.find("net_tls_close") != syscalls.end();
}

bool syscallsNeedServerLink(const std::unordered_set<std::string>& syscalls) {
    return syscalls.find("net_tcp_listen") != syscalls.end() ||
           syscalls.find("net_tcp_accept") != syscalls.end();
}

bool syscallsNeedPanicLink(const std::unordered_set<std::string>& syscalls) {
    return syscalls.find("panic") != syscalls.end() ||
           syscalls.find("recover") != syscalls.end() ||
           syscalls.find("try_invoke0") != syscalls.end();
}

bool syscallsNeedProcessLink(const std::unordered_set<std::string>& syscalls) {
    static const char* kProcessSyscalls[] = {
        "env_get",       "env_set",         "cwd",           "chdir",
        "run_capture",   "capture_stdout",  "proc_fork",     "proc_exec",
        "proc_wait",     "proc_exit",       "proc_kill",     "pipe_create",
        "pipe_read_fd",  "pipe_write_fd",   "fd_close",      "fd_read",
        "fd_write",      "fd_dup2",         "file_read",
    };
    for (const char* name : kProcessSyscalls) {
        if (syscalls.find(name) != syscalls.end()) {
            return true;
        }
    }
    return false;
}

}  // namespace xlang
