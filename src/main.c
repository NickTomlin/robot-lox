#include "common.h"
#include "vm.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }
    fseek(f, 0L, SEEK_END);
    size_t size = (size_t)ftell(f);
    rewind(f);

    char *buf = (char *)malloc(size + 1);
    if (buf == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }
    size_t read = fread(buf, sizeof(char), size, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

static void run_file(const char *path) {
    char *source = read_file(path);
    InterpretResult result = vm_interpret(source);
    free(source);
    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

static void run_repl(void) {
    char line[1024];
    for (;;) {
        printf("> ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }
        vm_interpret(line);
    }
}

int main(int argc, char *argv[]) {
    vm_init();

    if (argc == 1) {
        run_repl();
    } else if (argc == 2) {
        run_file(argv[1]);
    } else {
        fprintf(stderr, "Usage: lox [script]\n");
        exit(64);
    }

    vm_free();
    return 0;
}
