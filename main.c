#include "lib/hashset.h"
#include "lib/panic.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fcntl.h>
#include <unistd.h>

hashset_t* exclude = NULL;
hashset_t* include = NULL;

// name buffer
//--------------------------------------------------------------------------------------------------------------------------------

typedef struct name_buf_t name_buf_t;
struct name_buf_t {
    char** buf;
    uint64_t used;
    uint64_t allocated;
};
name_buf_t* nb_create(uint64_t init_length);
int nb_push(name_buf_t* nb, char* name);

name_buf_t* nb_copy(name_buf_t* nb)
{
    name_buf_t* new = nb_create(nb->used);
    panic_if(new == NULL, "could not copy name buf");
    char** name;
    for (name = nb->buf; name != nb->buf + nb->used; name++) {
        nb_push(new, *name);
    }
    return new;
}

int nb_push(name_buf_t* nb, char* name)
{
    if (nb->used >= nb->allocated) {
        nb->allocated <<= 1;
        nb->buf = realloc(nb->buf, nb->allocated * sizeof(*nb->buf));
        if (nb->buf == NULL) {
            return -1;
        }
    }
    nb->buf[nb->used] = malloc(strlen(name) + 1);
    panic_if(nb->buf == NULL, "could not realloc name_buf: %s", strerror(errno));
    strcpy(nb->buf[nb->used++], name);
    return 0;
}

name_buf_t* nb_create(uint64_t init_length)
{
    name_buf_t* nb = malloc(sizeof(*nb));
    if (nb == NULL) {
        return NULL;
    }
    nb->allocated = init_length;
    nb->used = 0;
    nb->buf = malloc(sizeof(*nb->buf) * nb->allocated);
    if (nb->buf == NULL) {
        free(nb);
        return NULL;
    }
    return nb;
}

void nb_free(name_buf_t* nb)
{
    if (nb == NULL) {
        return;
    }
    char** p;
    for (p = nb->buf; p != nb->buf + nb->used; p++) {
        free(*p);
    }
    free(nb->buf);
    free(nb);
}

// file_t
//--------------------------------------------------------------------------------------------------------------------------------

bool is_c_file(struct dirent* entry)
{
    uint64_t len = strlen(entry->d_name);
    if (entry->d_name[len - 2] != '.') {
        return 0;
    }
    if (entry->d_name[len - 1] != 'c' && entry->d_name[len - 1] != 'h') {
        return 0;
    }
    return 1;
}

typedef struct file_name_t file_name_t;
struct file_name_t {
    uint64_t block_size;
    char* buf;
};

void file_name_init(file_name_t* fn)
{
    fn->buf = calloc(1, 256);
    fn->block_size = 256;
}

void file_name_uninit(file_name_t* fn)
{
    free(fn->buf);
}

char* file_name_cat(file_name_t* fn, char* s1, char* s2)
{
    uint64_t len = strlen(s1) + strlen(s2) + 3;
    if (len >= fn->block_size) {
        fn->block_size = len;
        fn->buf = realloc(fn->buf, fn->block_size);
    }
    memset(fn->buf, 0, fn->block_size);
    strcpy(fn->buf, s1);
    strcat(fn->buf, "/");
    strcat(fn->buf, s2);
    return fn->buf;
}

void push_all_files_in_directory(name_buf_t* file_buf, name_buf_t* dir_buf, char* dir_name)
{
    DIR* dir = opendir(dir_name);
    file_name_t fn;
    file_name_init(&fn);
    panic_if(dir == NULL, "could not open directory: %s: %s", dir_name, strerror(errno));
    char* sub_dir;
    bool dir_added = 0;
    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0) {
            continue;
        }
        if (entry->d_type == DT_DIR) {
            sub_dir = file_name_cat(&fn, dir_name, entry->d_name);
            push_all_files_in_directory(file_buf, dir_buf, sub_dir);
            continue;
        } else if (is_c_file(entry)) {
            if (!dir_added) {
                nb_push(dir_buf, dir_name);
                dir_added = 1;
            }
            nb_push(file_buf, file_name_cat(&fn, dir_name, entry->d_name));
        }
    }
    file_name_uninit(&fn);
}

void out_structure(char* main_dir, name_buf_t* dir_buf)
{
    mkdir(main_dir, S_IRWXU);
    file_name_t fn;
    file_name_init(&fn);
    char** dir;
    for (dir = dir_buf->buf; dir != dir_buf->buf + dir_buf->used; dir++) {
        mkdir(file_name_cat(&fn, main_dir, *dir), S_IRWXU);
    }
    file_name_uninit(&fn);
}

void copy_file(char* dest_name, char* src_name)
{
    int dest = open(dest_name, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    int src = open(src_name, O_RDONLY);

    uint64_t len;
    struct stat st;
    stat(src_name, &st);
    len = st.st_size;

    sendfile(dest, src, 0, len);

    close(dest);
    close(src);
}

void out_files(char* out_dir, name_buf_t* file_buf)
{
    char** src_file;
    file_name_t fn;
    file_name_init(&fn);
    for (src_file = file_buf->buf; src_file != file_buf->buf + file_buf->used; src_file++) {
        copy_file(file_name_cat(&fn, out_dir, *src_file), *src_file);
    }

    file_name_uninit(&fn);
}

char static_instructions[] = "#include <stdarg.h>\n"
                             "#include <stdint.h>\n"
                             "#include <stdio.h>\n"
                             "#include <stdlib.h>\n"
                             "#include <string.h>\n"
                             "\n"
                             "#define run_command(comp, ...) run_command_(comp, __VA_ARGS__, NULL);\n"
                             "\n"
                             "void run_command_(char* comp, ...)\n"
                             "{\n"
                             "    va_list ap;\n"
                             "    va_start(ap, comp);\n"
                             "    char* cmd = malloc(256);\n"
                             "    uint64_t len = 256;\n"
                             "    uint64_t new_len = strlen(comp);\n"
                             "    strcpy(cmd, comp);\n"
                             "    char* next_arg = NULL;\n"
                             "\n"
                             "    while ((next_arg = va_arg(ap, char*)) != NULL) {\n"
                             "        new_len += strlen(next_arg) + 3;\n"
                             "        if (new_len >= len) {\n"
                             "            cmd = realloc(cmd, new_len << 1);\n"
                             "            len = new_len << 1;\n"
                             "        }\n"
                             "        strcat(cmd, \" \");\n"
                             "        strcat(cmd, next_arg);\n"
                             "    }\n"
                             "\n"
                             "    system(cmd);\n"
                             "    free(cmd);\n"
                             "}\n"
                             "\n"
                             "int main(int argc, char** argv)\n"
                             "{\n"
                             "    if (argc != 2) {\n"
                             "        fprintf(stderr, \"wrong number of argmuents\\n\");\n"
                             "        exit(1);\n"
                             "    }\n"
                             "    char* compiler = argv[1];\n"
                             "\n";

void out_compile_instructions(char* out_dir, char* program_name, char* flags, name_buf_t* file_buf)
{
    file_name_t fn;
    file_name_init(&fn);
    FILE* compile_file = fopen(file_name_cat(&fn, out_dir, "compile.c"), "w");
    panic_if(compile_file == NULL, "could not open compile.c");

    fprintf(compile_file, "%s", static_instructions);

    char** file;
    uint64_t file_len;
    for (file = file_buf->buf; file != file_buf->buf + file_buf->used; file++) {
        file_len = strlen(*file);
        if ((*file)[file_len - 1] == 'c') {
            fprintf(compile_file, "printf(\"compiling: %s\\n\");\n", *file);
            fprintf(compile_file, "run_command(compiler, \"-c\",\"%s\", \"%s\", ", flags, *file);
            (*file)[file_len - 1] = 'o';
            fprintf(compile_file, "\"-o %s\");\n\n", *file);
        }
    }

    fprintf(compile_file, "run_command(compiler, \"-o %s\", \"%s\" ", program_name, flags);

    for (file = file_buf->buf; file != file_buf->buf + file_buf->used; file++) {
        file_len = strlen(*file);
        if ((*file)[file_len - 1] == 'o') {
            fprintf(compile_file, ", \"%s\"", *file);
        }
    }
    fprintf(compile_file, ");\nreturn 0;\n}\n");

    fclose(compile_file);

    file_name_uninit(&fn);
}

char default_out_dir[] = "package";
char* out_dir = default_out_dir;

void print_usage()
{
    char* usage = "pp - HELP PAGE\n"
                  "--------------\n"
                  "pp is a software packing tool. Run it in the project order to genertae a fully packed\n"
                  " directory you can ship. Only a c compiler will be needed to compile the project\n"
                  "pp --out-dir \"<out_dir>\" --exclude \"file1.c;file2.c\" --bin-name \"<name_of_later_binary>\""
                  "\n\n"
                  "-h / --help    show this page\n"
                  "--out-dir      name of the output directory\n"
                  "--bin-name     name of the compiled binary\n"
                  "--exclude      exclude files which are not needed in later package\n";
    puts(usage);
    exit(0);
}

void parse_argv(char** argv)
{
    while (*argv) {
        if (strcmp(*argv, "-exc") == 0) {
            argv++;
            panic_if(*argv == NULL, "missing argument for -exc");
            hashset_set(exclude, *argv);
        } else if (strcmp(*argv, "--exclude") == 0) {
            argv++;
            panic_if(*argv == NULL, "missing argument for --exclude");
            hashset_set(exclude, *argv);
        } else if (strcmp(*argv, "-inc") == 0) {
            argv++;
            panic_if(*argv == NULL, "missing argument for -inc");
            hashset_set(include, *argv);
        } else if (strcmp(*argv, "--include") == 0) {
            argv++;
            panic_if(*argv == NULL, "missing argument for --include");
            hashset_set(include, *argv);
        } else if (strcmp(*argv, "--help") == 0) {
            print_usage();
        } else if (strcmp(*argv, "-h") == 0) {
            print_usage();
        } else if (strcmp(*argv, "--name-of-executable") == 0) {
        }
        argv++;
    }
}

uint64_t hash_str(char* s)
{
    uint64_t h = 0;
    while (*s) {
        h += *s;
        s++;
    }
    return h;
}

bool cmp_str(char* s1, char* s2)
{
    return strcmp(s1, s2) == 0;
}

int main(int argc, char** argv)
{
    hashset_error_t err = hashset_create(&exclude, (hashset_hash_function_t)hash_str, (hashset_cmp_function_t)cmp_str, 100);
    panic_if(err != HASHSET_SUCCESS, "could not create exclude hashset");
    err = hashset_create(&include, (hashset_hash_function_t)hash_str, (hashset_cmp_function_t)cmp_str, 100);
    panic_if(err != HASHSET_SUCCESS, "could not create include hashset");

    parse_argv(argv);
    // TODO: actual argument stuff
    panic_if(argc != 4, "wrong number of arguments");

    name_buf_t* file_buf = nb_create(16);
    name_buf_t* dir_buf = nb_create(16);

    push_all_files_in_directory(file_buf, dir_buf, ".");

    out_structure(out_dir, dir_buf);
    out_files(out_dir, file_buf);

    out_compile_instructions(out_dir, argv[2], argv[3], file_buf);

    hashset_free(exclude);
    hashset_free(include);

    nb_free(file_buf);
    nb_free(dir_buf);

    return 0;
}
