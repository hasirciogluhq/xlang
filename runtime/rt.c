#include <stdio.h>

int __xlang_io_print_int(int value) {
    printf("%d\n", value);
    return 0;
}
