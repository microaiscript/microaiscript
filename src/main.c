/*
 * AiScript - Main program and REPL
 */

#include "aiscript.h"
#include "types.h"
#include "memory.h"
#include "traceback.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <setjmp.h>

#if !defined(_WIN32)
#define AS_ENABLE_FATAL_SIGNAL_HANDLERS 1
#else
#define AS_ENABLE_FATAL_SIGNAL_HANDLERS 0
#endif

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define MAX_INPUT_SIZE 4096

#if AS_ENABLE_FATAL_SIGNAL_HANDLERS
static sigjmp_buf g_fatal_jmp;
static volatile sig_atomic_t g_fatal_sig = 0;
static volatile sig_atomic_t g_fatal_jmp_active = 0;

static const char* fatal_signal_name(int sig) {
    if (sig == SIGSEGV) return "Segmentation fault";
    if (sig == SIGABRT) return "Aborted";
    if (sig == SIGFPE) return "Floating point exception";
    if (sig == SIGILL) return "Illegal instruction";
#ifdef SIGBUS
    if (sig == SIGBUS) return "Bus error";
#endif
    return "Fatal signal";
}

static void fatal_signal_handler(int sig) {
    g_fatal_sig = sig;
    if (g_fatal_jmp_active) {
        siglongjmp(g_fatal_jmp, 1);
    }
    _exit(128 + sig);
}

static void setup_fatal_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fatal_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
#ifdef SIGBUS
    sigaction(SIGBUS, &sa, NULL);
#endif
}

static bool should_core_dump(void) {
    const char* v = getenv("AISCRIPT_COREDUMP");
    if (!v) return false;
    return (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "TRUE") == 0 ||
            strcmp(v, "yes") == 0 || strcmp(v, "YES") == 0);
}

static void trigger_core_dump(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGABRT, &sa, NULL);
    abort();
}
#endif

/* Print version information */
static void print_version(void) {
    printf("MicroAiScript %s\n", AISCRIPT_VERSION);
    printf("A lightweight scripting language that runs on C\n");
}

/* Print help information */
static void print_help(void) {
    printf("Usage: microaiscript [options] [file]\n");
    printf("\nOptions:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -v, --version  Show version information\n");
    printf("  -e <code>      Execute code string\n");
    printf("\nIf no file is provided, starts an interactive REPL.\n");
}

/* Read file contents */
static char* read_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return NULL;
    }
    
    /* Get file size */
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    /* Read file */
    char* content = as_malloc(size + 1);
    size_t read_size = fread(content, 1, size, file);
    content[read_size] = '\0';
    
    fclose(file);
    return content;
}

/* A NULL result without a syntax error means the script failed at runtime.
 * Errors raised by the interpreter itself have already been reported through
 * the traceback system; errors raised by native functions are only recorded
 * on the interpreter, so print those here. Returns the process exit code. */
static int runtime_exit_code(as_interpreter_t* interp, as_value_t* result) {
    if (result) {
        return result->type == AS_VALUE_ERROR ? 1 : 0;
    }
    const char* message = as_interpreter_get_error(interp);
    if (message) {
        fprintf(stderr, "Runtime error: %s\n", message);
    }
    return 1;
}

/* Run a file */
static int run_file(const char* filename) {
    char* source = read_file(filename);
    if (!source) return 1;
    
    as_interpreter_t* interp = as_interpreter_new();
    if (!interp) {
        as_free(source);
        return 1;
    }
    
    /* Set filename for error reporting */
    interp->current_file = filename;
    
    char* error = NULL;
#if AS_ENABLE_FATAL_SIGNAL_HANDLERS
    g_fatal_jmp_active = 1;
    if (sigsetjmp(g_fatal_jmp, 1) != 0) {
        g_fatal_jmp_active = 0;
        char fatal_msg[256];
        snprintf(fatal_msg, sizeof(fatal_msg), "%s (signal %d)", fatal_signal_name((int)g_fatal_sig), (int)g_fatal_sig);
        as_error_report((as_error_type_t)AS_ERROR_PYTHONIC_FATAL, fatal_msg);
        fflush(stderr);
        if (should_core_dump()) {
            trigger_core_dump();
        }
        as_free(source);
        as_interpreter_free(interp);
        return 1;
    }
#endif
    as_value_t* result = as_eval(interp, source, &error);
#if AS_ENABLE_FATAL_SIGNAL_HANDLERS
    g_fatal_jmp_active = 0;
#endif
    
    if (error) {
        fprintf(stderr, "%s\n", error);
        as_free(error);
        as_free(source);
        as_interpreter_free(interp);
        return 1;
    }
    
    /* Non-zero exit code for errors, like Python */
    int exit_code = runtime_exit_code(interp, result);
    
    if (result) {
        as_value_release(result);
    }
    
    as_free(source);
    as_interpreter_free(interp);
    return exit_code;
}

/* Run code string */
static int run_code(const char* code) {
    as_interpreter_t* interp = as_interpreter_new();
    if (!interp) return 1;
    
    char* error = NULL;
#if AS_ENABLE_FATAL_SIGNAL_HANDLERS
    g_fatal_jmp_active = 1;
    if (sigsetjmp(g_fatal_jmp, 1) != 0) {
        g_fatal_jmp_active = 0;
        char fatal_msg[256];
        snprintf(fatal_msg, sizeof(fatal_msg), "%s (signal %d)", fatal_signal_name((int)g_fatal_sig), (int)g_fatal_sig);
        as_error_report((as_error_type_t)AS_ERROR_PYTHONIC_FATAL, fatal_msg);
        fflush(stderr);
        if (should_core_dump()) {
            trigger_core_dump();
        }
        as_interpreter_free(interp);
        return 1;
    }
#endif
    as_value_t* result = as_eval(interp, code, &error);
#if AS_ENABLE_FATAL_SIGNAL_HANDLERS
    g_fatal_jmp_active = 0;
#endif
    
    if (error) {
        fprintf(stderr, "%s\n", error);
        as_free(error);
        as_interpreter_free(interp);
        return 1;
    }
    
    int exit_code = runtime_exit_code(interp, result);
    if (result) {
        as_value_release(result);
    }
    
    as_interpreter_free(interp);
    return exit_code;
}

/* Run interactive REPL */
static int run_repl(void) {
    printf("MicroAiScript %s\n", AISCRIPT_VERSION);
    printf("Type '.exit' to quit\n");
#ifdef HAVE_READLINE
    printf("(Use Up/Down arrows for history)\n");
#endif
    printf("\n");
    
    as_interpreter_t* interp = as_interpreter_new();
    if (!interp) return 1;
    
#ifndef HAVE_READLINE
    char input[MAX_INPUT_SIZE];
#endif
    
    while (true) {
#ifdef HAVE_READLINE
        char* input = readline("> ");
        if (!input) {
            break;  /* EOF (Ctrl+D) */
        }
        
        /* Add to history if not empty */
        if (input[0] != '\0') {
            add_history(input);
        }
#else
        printf("> ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        /* Remove trailing newline */
        size_t len = strlen(input);
        if (len > 0 && input[len-1] == '\n') {
            input[len-1] = '\0';
        }
#endif
        
        /* Check for exit command */
        if (strcmp(input, ".exit") == 0 || strcmp(input, "exit") == 0) {
#ifdef HAVE_READLINE
            free(input);
#endif
            break;
        }
        
        /* Empty input */
        if (input[0] == '\0') {
#ifdef HAVE_READLINE
            free(input);
#endif
            continue;
        }
        
        /* Evaluate input */
        char* error = NULL;
#if AS_ENABLE_FATAL_SIGNAL_HANDLERS
        g_fatal_jmp_active = 1;
        if (sigsetjmp(g_fatal_jmp, 1) != 0) {
            g_fatal_jmp_active = 0;
            char fatal_msg[256];
            snprintf(fatal_msg, sizeof(fatal_msg), "%s (signal %d)", fatal_signal_name((int)g_fatal_sig), (int)g_fatal_sig);
            as_error_report((as_error_type_t)AS_ERROR_PYTHONIC_FATAL, fatal_msg);
            fflush(stderr);
            if (should_core_dump()) {
                trigger_core_dump();
            }
#ifdef HAVE_READLINE
            free(input);
#endif
            as_interpreter_free(interp);
            return 1;
        }
#endif
        as_value_t* result = as_eval(interp, input, &error);
#if AS_ENABLE_FATAL_SIGNAL_HANDLERS
        g_fatal_jmp_active = 0;
#endif
        
        if (error) {
            fprintf(stderr, "Error: %s\n", error);
            as_free(error);
        } else if (!result) {
            const char* message = as_interpreter_get_error(interp);
            if (message) {
                fprintf(stderr, "Runtime error: %s\n", message);
                as_interpreter_clear_error(interp);
            }
        } else if (result->type != AS_VALUE_NULL) {
            /* Check if the input was just a print/output statement to avoid double display */
            bool is_output_stmt = (strstr(input, "print(") != NULL || strstr(input, "<:") != NULL);
            
            /* Only display result if it's not from a print/output statement */
            if (!is_output_stmt) {
                char* result_str = as_value_to_string(result);
                printf("%s\n", result_str);
                as_free(result_str);
            }
        }
        
        if (result) {
            as_value_release(result);
        }
        
#ifdef HAVE_READLINE
        free(input);
#endif
    }
    
    as_interpreter_free(interp);
    printf("\nGoodbye!\n");
    return 0;
}

/* Main entry point */
int main(int argc, char** argv) {
    /* Initialize error system */
    as_error_init();

#if AS_ENABLE_FATAL_SIGNAL_HANDLERS
    setup_fatal_signal_handlers();
#endif
    
    /* Parse command line arguments */
    if (argc == 1) {
        /* No arguments - run REPL */
        int result = run_repl();
        as_error_cleanup();
        return result;
    }
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
        else if (strcmp(argv[i], "-e") == 0) {
            if (i + 1 < argc) {
                return run_code(argv[i + 1]);
            } else {
                fprintf(stderr, "Error: -e requires an argument\n");
                return 1;
            }
        }
        else {
            /* Assume it's a filename */
            return run_file(argv[i]);
        }
    }
    
    return 0;
}


