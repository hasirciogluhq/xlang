#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

static __thread char time_format_buffer[32];

int64_t xlang_now_ms(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return 0;
    }
    return (int64_t)tv.tv_sec * 1000 + (int64_t)tv.tv_usec / 1000;
}

const char* xlang_time_format(int64_t unix_ms) {
    const time_t sec = (time_t)(unix_ms / 1000);
    int ms = (int)(unix_ms % 1000);
    if (ms < 0) {
        ms = 0;
    }
    struct tm* tm_info = localtime(&sec);
    if (tm_info == NULL) {
        time_format_buffer[0] = '\0';
        return time_format_buffer;
    }
    const size_t n =
        strftime(time_format_buffer, sizeof(time_format_buffer), "%Y/%m/%d - %H:%M:%S", tm_info);
    if (n == 0) {
        time_format_buffer[0] = '\0';
        return time_format_buffer;
    }
    snprintf(time_format_buffer + n, sizeof(time_format_buffer) - n, ".%03d", ms);
    return time_format_buffer;
}
