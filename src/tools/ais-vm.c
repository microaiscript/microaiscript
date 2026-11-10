/* ais-vm - AiScript Bytecode Virtual Machine
 * Executes compiled .aic bytecode files
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "bytecode.h"

static void print_usage(const char* prog) {
    printf("Usage: %s [options] <bytecode.aic>\n", prog);
    printf("\n");
    printf("Execute AiScript bytecode files\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -v, --verbose  Show execution details\n");
    printf("  --version      Show version information\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s program.aic          # Execute bytecode\n", prog);
    printf("  %s -v program.aic       # Execute with verbose output\n", prog);
    printf("\n");
}

static void print_version(void) {
    printf("ais-vm version 1.0.0\n");
    printf("AiScript Bytecode Virtual Machine\n");
}

int main(int argc, char** argv) {
    bool verbose = false;
    const char* filename = NULL;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            fprintf(stderr, "Use -h for help\n");
            return 1;
        } else {
            if (filename != NULL) {
                fprintf(stderr, "Error: Multiple input files specified\n");
                return 1;
            }
            filename = argv[i];
        }
    }
    
    if (filename == NULL) {
        fprintf(stderr, "Error: No input file specified\n");
        fprintf(stderr, "Use -h for help\n");
        return 1;
    }
    
    if (verbose) {
        printf("=== AiScript VM ===\n");
        printf("Loading: %s\n\n", filename);
    }
    
    /* Create VM */
    as_vm_t* vm = as_vm_new();
    if (!vm) {
        fprintf(stderr, "Error: Failed to create VM\n");
        return 1;
    }
    
    /* Execute bytecode file */
    as_vm_result_t result = as_vm_execute_file(vm, filename);
    
    if (result == VM_ERROR) {
        fprintf(stderr, "\nVM Error: %s\n", vm->error ? vm->error : "Unknown error");
        as_vm_free(vm);
        return 1;
    }
    
    if (verbose) {
        printf("\n=== Execution Complete ===\n");
        printf("Stack size: %zu\n", vm->stack_top);
        printf("Globals: %zu\n", vm->globals_count);
    }
    
    /* Cleanup */
    as_vm_free(vm);
    
    return 0;
}

