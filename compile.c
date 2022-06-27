#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define run_command(comp, ...) run_command_(comp, __VA_ARGS__, NULL);

void run_command_(char* comp, ...)
{
    va_list ap;
    va_start(ap, comp);
    char* cmd = malloc(256);
    uint64_t len = 256;
    uint64_t new_len = strlen(comp);
    strcpy(cmd, comp);
    char* next_arg = NULL;

    while ((next_arg = va_arg(ap, char*)) != NULL) {
        new_len += strlen(next_arg) + 3;
        if (new_len >= len) {
            cmd = realloc(cmd, new_len << 1);
            len = new_len << 1;
        }
        strcat(cmd, " ");
        strcat(cmd, next_arg);
    }

    system(cmd);
    free(cmd);
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "wrong number of argmuents\n");
        exit(1);
    }
    char* compiler = argv[1];

printf("compiling: ./main.c\n");
run_command(compiler, "-c","pp", "./main.c", "-o ./main.o");

run_command(compiler, "-o pp", "pp" , "./main.o");
return 0;
}
