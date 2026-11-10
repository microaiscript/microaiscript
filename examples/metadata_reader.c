/* Example: Reading metadata from AiScript files */
#include "../include/aiscript.h"
#include "../include/node.h"
#include "../include/parser.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <script.ais>\n", argv[0]);
        return 1;
    }
    
    /* Read script file */
    FILE* f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file %s\n", argv[1]);
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* source = malloc(size + 1);
    fread(source, 1, size, f);
    source[size] = '\0';
    fclose(f);
    
    /* Parse script */
    as_parser_t* parser = as_parser_new(source);
    size_t node_count;
    as_node_t** nodes = as_parser_parse(parser, &node_count);
    
    if (!nodes) {
        fprintf(stderr, "Parse error\n");
        free(source);
        as_parser_free(parser);
        return 1;
    }
    
    /* Extract metadata */
    size_t meta_count = as_get_metadata(nodes, node_count, NULL);
    
    if (meta_count == 0) {
        printf("No metadata found in script.\n");
    } else {
        printf("Found %zu metadata entries:\n\n", meta_count);
        
        as_node_t** metadata = malloc(meta_count * sizeof(as_node_t*));
        as_get_metadata(nodes, node_count, metadata);
        
        for (size_t i = 0; i < meta_count; i++) {
            as_node_meta_t* meta = (as_node_meta_t*)metadata[i];
            printf("  %s: ", meta->name);
            
            /* Print value based on type */
            switch (meta->value->type) {
                case NODE_STR: {
                    as_node_str_t* str = (as_node_str_t*)meta->value;
                    printf("\"%s\"\n", str->value);
                    break;
                }
                case NODE_NUM: {
                    as_node_num_t* num = (as_node_num_t*)meta->value;
                    printf("%g\n", num->value);
                    break;
                }
                case NODE_BOOL: {
                    as_node_bool_t* b = (as_node_bool_t*)meta->value;
                    printf("%s\n", b->value ? "true" : "false");
                    break;
                }
                case NODE_ARR: {
                    as_node_arr_t* arr = (as_node_arr_t*)meta->value;
                    printf("[array with %zu elements]\n", arr->element_count);
                    break;
                }
                default:
                    printf("[complex value]\n");
                    break;
            }
        }
        
        free(metadata);
    }
    
    /* Cleanup */
    for (size_t i = 0; i < node_count; i++) {
        as_node_free(nodes[i]);
    }
    free(nodes);
    as_parser_free(parser);
    free(source);
    
    return 0;
}

