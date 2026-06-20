#include "xlang/syscalls.h"

#include "xlang/error.h"

namespace xlang {

bool isKnownSyscall(const std::string& name) {
    return name == "print_int" || name == "print_cstr" || name == "start_thread" ||
           name == "sleep_ms" || name == "random_range" || name == "print_done" ||
           name == "wait_all_jobs";
}

namespace {

void emitThreadSupport(std::string& output) {
    output += "; xlang thread pool (pthread backend)\n";
    output += "declare i32 @pthread_create(i64*, i8*, i8* (i8*)*, i8*)\n";
    output += "declare i32 @pthread_join(i64, i8**)\n";
    output += "declare i8* @malloc(i64)\n";
    output += "declare void @free(i8*)\n";
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

}  // namespace

// Compiler backend: xlang `declare syscall` primitiflerini LLVM IR'ye indirger.
// Kullanıcı kodu OS/pthread API görmez — sadece xlang syscall isimlerini kullanır.
void emitSyscallDefinitions(std::string& output, const std::unordered_set<std::string>& syscalls) {
    const bool needs_threads = syscalls.find("start_thread") != syscalls.end() ||
                               syscalls.find("wait_all_jobs") != syscalls.end();

    if (syscalls.find("print_int") != syscalls.end()) {
        output += "; xlang syscall: print_int\n";
        output += "@__xlang_syscall_fmt = private unnamed_addr constant [4 x i8] c\"%d\\0A\\00\"\n";
        output += "declare i32 @printf(i8*, ...)\n";
        output += "define weak i32 @print_int(i32 %n) {\n";
        output += "  call i32 (i8*, ...) @printf(i8* getelementptr inbounds "
                  "([4 x i8], [4 x i8]* @__xlang_syscall_fmt, i32 0, i32 0), i32 %n)\n";
        output += "  ret i32 0\n";
        output += "}\n\n";
    }

    if (syscalls.find("print_cstr") != syscalls.end()) {
        output += "; xlang syscall: print_cstr\n";
        output += "@__xlang_syscall_strfmt = private unnamed_addr constant [4 x i8] c\"%s\\0A\\00\"\n";
        output += "declare i32 @printf(i8*, ...)\n";
        output += "define weak i32 @print_cstr(i8* %s) {\n";
        output += "  call i32 (i8*, ...) @printf(i8* getelementptr inbounds "
                  "([4 x i8], [4 x i8]* @__xlang_syscall_strfmt, i32 0, i32 0), i8* %s)\n";
        output += "  ret i32 0\n";
        output += "}\n\n";
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
        output += "declare i32 @printf(i8*, ...)\n";
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

    for (const std::string& name : syscalls) {
        if (!isKnownSyscall(name)) {
            throw XlangError("unknown xlang syscall: " + name);
        }
    }
}

bool syscallsNeedThreadLink(const std::unordered_set<std::string>& syscalls) {
    return syscalls.find("start_thread") != syscalls.end() ||
           syscalls.find("wait_all_jobs") != syscalls.end();
}

}  // namespace xlang
