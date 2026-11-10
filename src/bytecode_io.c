/* Bytecode file I/O for AiScript C
 * Handles reading and writing .aic bytecode files
 */

#include "bytecode.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Helper macros for endianness conversion */
#define WRITE_U32(buf, val) do { \
    (buf)[0] = ((val) >> 24) & 0xFF; \
    (buf)[1] = ((val) >> 16) & 0xFF; \
    (buf)[2] = ((val) >> 8) & 0xFF; \
    (buf)[3] = (val) & 0xFF; \
} while(0)

#define WRITE_U16(buf, val) do { \
    (buf)[0] = ((val) >> 8) & 0xFF; \
    (buf)[1] = (val) & 0xFF; \
} while(0)

#define READ_U32(buf) \
    (((uint32_t)(buf)[0] << 24) | \
     ((uint32_t)(buf)[1] << 16) | \
     ((uint32_t)(buf)[2] << 8) | \
     ((uint32_t)(buf)[3]))

#define READ_U16(buf) \
    (((uint16_t)(buf)[0] << 8) | \
     ((uint16_t)(buf)[1]))

/* Write constant to buffer */
static size_t write_constant(uint8_t* buf, as_constant_t* c) {
    size_t offset = 0;
    
    /* Type byte */
    buf[offset++] = (uint8_t)c->type;
    
    switch (c->type) {
        case CONST_NULL:
            /* No additional data */
            break;
            
        case CONST_BOOL:
            buf[offset++] = c->value.bool_val ? 1 : 0;
            break;
            
        case CONST_NUM: {
            /* Write double as 8 bytes */
            uint64_t bits;
            memcpy(&bits, &c->value.num_val, sizeof(double));
            for (int i = 7; i >= 0; i--) {
                buf[offset++] = (bits >> (i * 8)) & 0xFF;
            }
            break;
        }
            
        case CONST_STR: {
            /* Write length (4 bytes) + string data */
            WRITE_U32(buf + offset, c->value.str.length);
            offset += 4;
            memcpy(buf + offset, c->value.str.data, c->value.str.length);
            offset += c->value.str.length;
            /* Null terminator */
            buf[offset++] = 0;
            break;
        }
            
        case CONST_FUNC_CODE: {
            /* Write code size (4 bytes) + code */
            WRITE_U32(buf + offset, c->value.func.code_size);
            offset += 4;
            memcpy(buf + offset, c->value.func.code, c->value.func.code_size);
            offset += c->value.func.code_size;
            break;
        }
    }
    
    return offset;
}

/* Read constant from buffer */
static size_t read_constant(const uint8_t* buf, as_constant_t** c_out) {
    size_t offset = 0;
    
    as_constant_t* c = as_calloc(1, sizeof(as_constant_t));
    
    /* Type byte */
    c->type = (as_const_type_t)buf[offset++];
    
    switch (c->type) {
        case CONST_NULL:
            /* No additional data */
            break;
            
        case CONST_BOOL:
            c->value.bool_val = buf[offset++] != 0;
            break;
            
        case CONST_NUM: {
            /* Read double from 8 bytes */
            uint64_t bits = 0;
            for (int i = 7; i >= 0; i--) {
                bits |= ((uint64_t)buf[offset++]) << (i * 8);
            }
            memcpy(&c->value.num_val, &bits, sizeof(double));
            break;
        }
            
        case CONST_STR: {
            /* Read length + string data */
            c->value.str.length = READ_U32(buf + offset);
            offset += 4;
            c->value.str.data = as_malloc(c->value.str.length + 1);
            memcpy(c->value.str.data, buf + offset, c->value.str.length);
            c->value.str.data[c->value.str.length] = '\0';
            offset += c->value.str.length + 1; /* Skip null terminator */
            break;
        }
            
        case CONST_FUNC_CODE: {
            /* Read code size + code */
            c->value.func.code_size = READ_U32(buf + offset);
            offset += 4;
            c->value.func.code = as_malloc(c->value.func.code_size);
            memcpy(c->value.func.code, buf + offset, c->value.func.code_size);
            offset += c->value.func.code_size;
            break;
        }
            
        default:
            fprintf(stderr, "Unknown constant type: %d\n", c->type);
            as_free(c);
            return 0;
    }
    
    *c_out = c;
    return offset;
}

/* Calculate size needed for constant */
static size_t constant_size(as_constant_t* c) {
    size_t size = 1; /* Type byte */
    
    switch (c->type) {
        case CONST_NULL:
            break;
        case CONST_BOOL:
            size += 1;
            break;
        case CONST_NUM:
            size += 8;
            break;
        case CONST_STR:
            size += 4 + c->value.str.length + 1; /* length + data + null */
            break;
        case CONST_FUNC_CODE:
            size += 4 + c->value.func.code_size; /* size + code */
            break;
    }
    
    return size;
}

/* Write bytecode to memory buffer */
bool as_bytecode_write_memory(as_bytecode_t* bc, uint8_t** data_out, size_t* size_out) {
    if (!bc) return false;
    
    /* Calculate total size */
    size_t header_size = sizeof(as_bytecode_header_t);
    size_t code_size = bc->code_size;
    size_t const_size = 0;
    
    for (size_t i = 0; i < bc->const_count; i++) {
        const_size += constant_size(bc->constants[i]);
    }
    
    size_t native_size = 0;
    for (size_t i = 0; i < bc->native_count; i++) {
        native_size += strlen(bc->natives[i].name) + 1;
        native_size += strlen(bc->natives[i].module) + 1;
        native_size += 4; /* hash */
    }
    
    size_t symbol_size = 4; /* 4 bytes for symbol_count */
    for (size_t i = 0; i < bc->symbol_count; i++) {
        symbol_size += strlen(bc->symbols[i]) + 1;
    }
    
    size_t debug_size = bc->debug_count * sizeof(as_debug_info_t);
    
    size_t total_size = header_size + code_size + const_size + native_size + 
                        symbol_size + debug_size;
    
    /* Allocate buffer */
    uint8_t* buf = as_malloc(total_size);
    if (!buf) return false;
    
    size_t offset = 0;
    
    /* Write header (temporarily with zeros for checksum) */
    bc->header.code_size = code_size;
    bc->header.const_count = bc->const_count;
    bc->header.native_count = bc->native_count;
    bc->header.debug_count = bc->debug_count;
    bc->header.checksum = 0;
    
    WRITE_U32(buf + offset, bc->header.magic); offset += 4;
    WRITE_U16(buf + offset, bc->header.version_major); offset += 2;
    WRITE_U16(buf + offset, bc->header.version_minor); offset += 2;
    WRITE_U32(buf + offset, bc->header.flags); offset += 4;
    WRITE_U32(buf + offset, bc->header.code_size); offset += 4;
    WRITE_U32(buf + offset, bc->header.const_count); offset += 4;
    WRITE_U32(buf + offset, bc->header.native_count); offset += 4;
    WRITE_U32(buf + offset, bc->header.debug_count); offset += 4;
    offset += 4; /* Skip checksum for now */
    offset += 12; /* Skip reserved */
    
    /* Write code */
    memcpy(buf + offset, bc->code, code_size);
    offset += code_size;
    
    /* Write constants */
    for (size_t i = 0; i < bc->const_count; i++) {
        offset += write_constant(buf + offset, bc->constants[i]);
    }
    
    /* Write symbols */
    WRITE_U32(buf + offset, bc->symbol_count); offset += 4;
    for (size_t i = 0; i < bc->symbol_count; i++) {
        size_t len = strlen(bc->symbols[i]);
        memcpy(buf + offset, bc->symbols[i], len + 1);
        offset += len + 1;
    }
    
    /* Write native references */
    for (size_t i = 0; i < bc->native_count; i++) {
        size_t name_len = strlen(bc->natives[i].name);
        memcpy(buf + offset, bc->natives[i].name, name_len + 1);
        offset += name_len + 1;
        
        size_t module_len = strlen(bc->natives[i].module);
        memcpy(buf + offset, bc->natives[i].module, module_len + 1);
        offset += module_len + 1;
        
        WRITE_U32(buf + offset, bc->natives[i].symbol_hash);
        offset += 4;
    }
    
    /* Write debug info */
    for (size_t i = 0; i < bc->debug_count; i++) {
        WRITE_U32(buf + offset, bc->debug_info[i].line); offset += 4;
        WRITE_U32(buf + offset, bc->debug_info[i].column); offset += 4;
        WRITE_U32(buf + offset, bc->debug_info[i].pc); offset += 4;
    }
    
    /* Calculate and write checksum (skip first 32 bytes of header) */
    uint32_t checksum = 0;
    for (size_t i = 32; i < offset; i++) {
        checksum = (checksum << 5) + checksum + buf[i];
    }
    WRITE_U32(buf + 32, checksum);
    
    *data_out = buf;
    *size_out = offset;
    
    return true;
}

/* Write bytecode to file */
bool as_bytecode_write_file(as_bytecode_t* bc, const char* filename) {
    uint8_t* data;
    size_t size;
    
    if (!as_bytecode_write_memory(bc, &data, &size)) {
        return false;
    }
    
    FILE* f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Cannot open %s for writing\n", filename);
        as_free(data);
        return false;
    }
    
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    as_free(data);
    
    if (written != size) {
        fprintf(stderr, "Write error: wrote %zu of %zu bytes\n", written, size);
        return false;
    }
    
    return true;
}

/* Read bytecode from memory buffer */
as_bytecode_t* as_bytecode_read_memory(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(as_bytecode_header_t)) {
        fprintf(stderr, "Invalid bytecode data\n");
        return NULL;
    }
    
    as_bytecode_t* bc = as_bytecode_new();
    if (!bc) return NULL;
    
    size_t offset = 0;
    
    /* Read header */
    bc->header.magic = READ_U32(data + offset); offset += 4;
    bc->header.version_major = READ_U16(data + offset); offset += 2;
    bc->header.version_minor = READ_U16(data + offset); offset += 2;
    bc->header.flags = READ_U32(data + offset); offset += 4;
    bc->header.code_size = READ_U32(data + offset); offset += 4;
    bc->header.const_count = READ_U32(data + offset); offset += 4;
    bc->header.native_count = READ_U32(data + offset); offset += 4;
    bc->header.debug_count = READ_U32(data + offset); offset += 4;
    bc->header.checksum = READ_U32(data + offset); offset += 4;
    offset += 12; /* Skip reserved */
    
    /* Validate magic */
    if (bc->header.magic != AS_BYTECODE_MAGIC) {
        fprintf(stderr, "Invalid bytecode magic: 0x%08X\n", bc->header.magic);
        as_bytecode_free(bc);
        return NULL;
    }
    
    /* Validate version */
    if (bc->header.version_major != AS_BYTECODE_VERSION_MAJOR) {
        fprintf(stderr, "Incompatible bytecode version: %u.%u\n",
                bc->header.version_major, bc->header.version_minor);
        as_bytecode_free(bc);
        return NULL;
    }
    
    /* Read code */
    bc->code_size = bc->header.code_size;
    bc->code = as_malloc(bc->code_size);
    memcpy(bc->code, data + offset, bc->code_size);
    offset += bc->code_size;
    
    /* Read constants */
    bc->const_count = bc->header.const_count;
    if (bc->const_count > 0) {
        bc->constants = as_calloc(bc->const_count, sizeof(as_constant_t*));
        for (size_t i = 0; i < bc->const_count; i++) {
            size_t consumed = read_constant(data + offset, &bc->constants[i]);
            if (consumed == 0) {
                fprintf(stderr, "Error reading constant %zu\n", i);
                as_bytecode_free(bc);
                return NULL;
            }
            offset += consumed;
        }
    }
    
    /* Read symbols */
    bc->symbol_count = READ_U32(data + offset); offset += 4;
    if (bc->symbol_count > 0) {
        bc->symbols = as_calloc(bc->symbol_count, sizeof(char*));
        for (size_t i = 0; i < bc->symbol_count; i++) {
            size_t len = strlen((const char*)(data + offset));
            bc->symbols[i] = as_strdup((const char*)(data + offset));
            offset += len + 1;
        }
    }
    
    /* Read native references */
    bc->native_count = bc->header.native_count;
    if (bc->native_count > 0) {
        bc->natives = as_calloc(bc->native_count, sizeof(as_native_ref_t));
        for (size_t i = 0; i < bc->native_count; i++) {
            size_t name_len = strlen((const char*)(data + offset));
            bc->natives[i].name = as_strdup((const char*)(data + offset));
            offset += name_len + 1;
            
            size_t module_len = strlen((const char*)(data + offset));
            bc->natives[i].module = as_strdup((const char*)(data + offset));
            offset += module_len + 1;
            
            bc->natives[i].symbol_hash = READ_U32(data + offset);
            offset += 4;
        }
    }
    
    /* Read debug info */
    bc->debug_count = bc->header.debug_count;
    if (bc->debug_count > 0) {
        bc->debug_info = as_calloc(bc->debug_count, sizeof(as_debug_info_t));
        for (size_t i = 0; i < bc->debug_count; i++) {
            bc->debug_info[i].line = READ_U32(data + offset); offset += 4;
            bc->debug_info[i].column = READ_U32(data + offset); offset += 4;
            bc->debug_info[i].pc = READ_U32(data + offset); offset += 4;
        }
    }
    
    /* Validate */
    if (!as_bytecode_validate(bc)) {
        as_bytecode_free(bc);
        return NULL;
    }
    
    return bc;
}

/* Read bytecode from file */
as_bytecode_t* as_bytecode_read_file(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open %s for reading\n", filename);
        return NULL;
    }
    
    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size < 0) {
        fprintf(stderr, "Cannot get file size for %s\n", filename);
        fclose(f);
        return NULL;
    }
    
    /* Read entire file */
    uint8_t* data = as_malloc(file_size);
    size_t read_size = fread(data, 1, file_size, f);
    fclose(f);
    
    if ((long)read_size != file_size) {
        fprintf(stderr, "Read error: read %zu of %ld bytes\n", read_size, file_size);
        as_free(data);
        return NULL;
    }
    
    /* Parse bytecode */
    as_bytecode_t* bc = as_bytecode_read_memory(data, file_size);
    as_free(data);
    
    return bc;
}

