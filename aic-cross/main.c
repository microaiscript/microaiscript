/* aic-cross - AiScript Cross-Compiler
 * Compiles .ais source files to .aic bytecode files
 * Similar to MicroPython's mpy-cross
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include "compiler.h"
#include "bytecode.h"
#include "parser.h"
#include "memory.h"

#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 0

/* Configuration */
typedef struct {
    const char* input_file;
    const char* output_file;
    as_opt_level_t opt_level;
    bool debug_info;
    bool verbose;
    bool disassemble;
    const char* target_arch;
} compile_config_t;

/* Print version information */
static void print_version(void) {
    printf("aic-cross version %d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
    printf("AiScript Bytecode Compiler\n");
}

/* Print usage information */
static void print_usage(const char* prog_name) {
    printf("Usage: %s [options] <input.ais>\n", prog_name);
    printf("\n");
    printf("Compile AiScript source (.ais) to bytecode (.aic)\n");
    printf("\n");
    printf("Options:\n");
    printf("  -o <file>      Output file (default: input.aic)\n");
    printf("  -O<level>      Optimization level: 0, 1, or 2 (default: 1)\n");
    printf("                   0: No optimization (fastest compilation)\n");
    printf("                   1: Basic optimization (default)\n");
    printf("                   2: Full optimization (smallest/fastest bytecode)\n");
    printf("  -g             Include debug information (line numbers)\n");
    printf("  -march=<arch>  Target architecture (esp32, arm, x86_64)\n");
    printf("  -v, --verbose  Show compilation details\n");
    printf("  -d             Disassemble bytecode after compilation\n");
    printf("  -h, --help     Show this help message\n");
    printf("  --version      Show version information\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s app.ais                  # Compile to app.aic\n", prog_name);
    printf("  %s -O2 -o out.aic in.ais    # Full optimization\n", prog_name);
    printf("  %s -g -v debug.ais          # With debug info and verbose\n", prog_name);
    printf("  %s -march=esp32 firmware.ais # For ESP32 target\n", prog_name);
    printf("\n");
}

/* Read entire file into memory */
static char* read_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return NULL;
    }
    
    /* Get file size */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size < 0) {
        fprintf(stderr, "Error: Cannot get size of '%s'\n", filename);
        fclose(f);
        return NULL;
    }
    
    /* Allocate and read */
    char* data = malloc(size + 1);
    if (!data) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(f);
        return NULL;
    }
    
    size_t read_size = fread(data, 1, size, f);
    fclose(f);
    
    if ((long)read_size != size) {
        fprintf(stderr, "Error: Failed to read '%s'\n", filename);
        free(data);
        return NULL;
    }
    
    data[size] = '\0';
    return data;
}

/* Get output filename from input filename */
static char* get_output_filename(const char* input_file) {
    size_t len = strlen(input_file);
    char* output = malloc(len + 5); /* Room for .aic + null */
    if (!output) return NULL;
    
    strcpy(output, input_file);
    
    /* Replace .ais with .aic, or append .aic */
    if (len >= 4 && strcmp(input_file + len - 4, ".ais") == 0) {
        strcpy(output + len - 4, ".aic");
    } else {
        strcat(output, ".aic");
    }
    
    return output;
}

/* Parse command-line arguments */
static bool parse_args(int argc, char** argv, compile_config_t* config) {
    /* Initialize defaults */
    config->input_file = NULL;
    config->output_file = NULL;
    config->opt_level = OPT_BASIC;
    config->debug_info = false;
    config->verbose = false;
    config->disassemble = false;
    config->target_arch = NULL;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return false;
        } else if (strcmp(argv[i], "--version") == 0) {
            print_version();
            return false;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            config->verbose = true;
        } else if (strcmp(argv[i], "-g") == 0) {
            config->debug_info = true;
        } else if (strcmp(argv[i], "-d") == 0) {
            config->disassemble = true;
        } else if (strcmp(argv[i], "-O0") == 0) {
            config->opt_level = OPT_NONE;
        } else if (strcmp(argv[i], "-O1") == 0) {
            config->opt_level = OPT_BASIC;
        } else if (strcmp(argv[i], "-O2") == 0) {
            config->opt_level = OPT_FULL;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -o requires an argument\n");
                return false;
            }
            config->output_file = argv[++i];
        } else if (strncmp(argv[i], "-march=", 7) == 0) {
            config->target_arch = argv[i] + 7;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            fprintf(stderr, "Use -h for help\n");
            return false;
        } else {
            /* Input file */
            if (config->input_file != NULL) {
                fprintf(stderr, "Error: Multiple input files specified\n");
                return false;
            }
            config->input_file = argv[i];
        }
    }
    
    /* Check required arguments */
    if (config->input_file == NULL) {
        fprintf(stderr, "Error: No input file specified\n");
        fprintf(stderr, "Use -h for help\n");
        return false;
    }
    
    /* Set output file if not specified */
    if (config->output_file == NULL) {
        char* output = get_output_filename(config->input_file);
        if (!output) {
            fprintf(stderr, "Error: Out of memory\n");
            return false;
        }
        config->output_file = output;
    }
    
    return true;
}

/* Main compilation function */
static int compile_file(const compile_config_t* config) {
    int exit_code = 0;
    
    /* Verbose: starting */
    if (config->verbose) {
        printf("Compiling: %s -> %s\n", config->input_file, config->output_file);
        printf("Optimization: -O%d\n", config->opt_level);
        if (config->debug_info) {
            printf("Debug info: enabled\n");
        }
        if (config->target_arch) {
            printf("Target: %s\n", config->target_arch);
        }
        printf("\n");
    }
    
    /* Read source file */
    char* source = read_file(config->input_file);
    if (!source) {
        return 2; /* File error */
    }
    
    if (config->verbose) {
        printf("Source size: %zu bytes\n", strlen(source));
    }
    
    /* Parse source */
    as_parser_t* parser = as_parser_new(source);
    if (!parser) {
        fprintf(stderr, "Error: Failed to create parser\n");
        free(source);
        return 3; /* Parse error */
    }
    
    size_t node_count;
    as_node_t** nodes = as_parser_parse(parser, &node_count);
    
    if (parser->error_message || !nodes) {
        fprintf(stderr, "Parse error: %s\n", parser->error_message ? parser->error_message : "Unknown error");
        as_parser_free(parser);
        free(source);
        return 3; /* Parse error */
    }
    
    if (config->verbose) {
        printf("Parsed: %zu top-level nodes\n", node_count);
    }
    
    /* Compile to bytecode */
    as_compiler_config_t compiler_config = {
        .opt_level = config->opt_level,
        .debug_info = config->debug_info,
        .inline_functions = (config->opt_level >= OPT_FULL),
        .const_propagation = (config->opt_level >= OPT_BASIC),
        .target_arch = config->target_arch
    };
    
    as_bytecode_t* bc = as_compile(nodes, node_count, &compiler_config);
    
    if (!bc) {
        fprintf(stderr, "Compilation error: Failed to generate bytecode\n");
        as_parser_free(parser);
        free(source);
        return 4; /* Compilation error */
    }
    
    if (config->verbose) {
        printf("\nGenerated bytecode:\n");
        printf("  Code size: %zu bytes\n", bc->code_size);
        printf("  Constants: %zu\n", bc->const_count);
        printf("  Symbols: %zu\n", bc->symbol_count);
        if (bc->native_count > 0) {
            printf("  Native refs: %zu\n", bc->native_count);
        }
        if (bc->debug_count > 0) {
            printf("  Debug entries: %zu\n", bc->debug_count);
        }
    }
    
    /* Write bytecode file */
    if (!as_bytecode_write_file(bc, config->output_file)) {
        fprintf(stderr, "Error: Failed to write '%s'\n", config->output_file);
        as_bytecode_free(bc);
        as_parser_free(parser);
        free(source);
        return 2; /* File error */
    }
    
    /* Get output file size */
    struct stat st;
    size_t output_size = 0;
    if (stat(config->output_file, &st) == 0) {
        output_size = st.st_size;
    }
    
    if (config->verbose) {
        printf("\nOutput: %s (%zu bytes", config->output_file, output_size);
        if (strlen(source) > 0) {
            int reduction = (int)(100.0 * (1.0 - (double)output_size / strlen(source)));
            if (reduction > 0) {
                printf(", %d%% smaller", reduction);
            }
        }
        printf(")\n");
    } else {
        /* Non-verbose: just show success */
        printf("Compiled: %s -> %s\n", config->input_file, config->output_file);
    }
    
    /* Disassemble if requested */
    if (config->disassemble) {
        printf("\nBytecode disassembly:\n");
        printf("====================\n");
        as_bytecode_disassemble(bc);
    }
    
    /* Clean up */
    as_bytecode_free(bc);
    
    /* Free nodes */
    for (size_t i = 0; i < node_count; i++) {
        as_node_free(nodes[i]);
    }
    free(nodes);
    
    as_parser_free(parser);
    free(source);
    
    return exit_code;
}

/* Main entry point */
int main(int argc, char** argv) {
    /* Parse command-line arguments */
    compile_config_t config;
    if (!parse_args(argc, argv, &config)) {
        return (argc > 1) ? 1 : 0; /* Return error only if args were provided */
    }
    
    /* Compile */
    int result = compile_file(&config);
    
    /* Free allocated output filename if it was generated */
    if (config.output_file != NULL && 
        config.output_file != argv[0] && /* Not from argv */
        strchr(config.output_file, '/') == NULL) { /* Not a path from argv */
        /* This was allocated by get_output_filename */
        free((char*)config.output_file);
    }
    
    return result;
}

