/*
 * AiScript - Scanner (Lexer) implementation
 */

#include "scanner.h"
#include "memory.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Character classification helpers */
static bool is_space(char c) {
    return c == ' ' || c == '\t';
}

static bool is_newline(char c) {
    return c == '\n' || c == '\r';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_identifier_part(char c) {
    return is_identifier_start(c) || is_digit(c);
}

/* Internal scanner functions */
static char peek_char(as_scanner_t* scanner) {
    if (scanner->pos >= strlen(scanner->source)) {
        return '\0';
    }
    return scanner->source[scanner->pos];
}

static char peek_next(as_scanner_t* scanner) {
    if (scanner->pos + 1 >= strlen(scanner->source)) {
        return '\0';
    }
    return scanner->source[scanner->pos + 1];
}

static void advance(as_scanner_t* scanner) {
    if (scanner->pos >= strlen(scanner->source)) return;
    
    char c = scanner->source[scanner->pos];
    scanner->pos++;
    
    if (c == '\n') {
        scanner->line++;
        scanner->column = 1;
    } else {
        scanner->column++;
    }
}

static void skip_whitespace(as_scanner_t* scanner) {
    while (is_space(peek_char(scanner))) {
        advance(scanner);
    }
}

static void skip_comment(as_scanner_t* scanner) {
    /* Single line comment */
    if (peek_char(scanner) == '/' && peek_next(scanner) == '/') {
        while (peek_char(scanner) != '\0' && !is_newline(peek_char(scanner))) {
            advance(scanner);
        }
    }
    /* Multi-line comment */
    else if (peek_char(scanner) == '/' && peek_next(scanner) == '*') {
        advance(scanner); /* / */
        advance(scanner); /* * */
        while (peek_char(scanner) != '\0') {
            if (peek_char(scanner) == '*' && peek_next(scanner) == '/') {
                advance(scanner); /* * */
                advance(scanner); /* / */
                break;
            }
            advance(scanner);
        }
    }
}

static as_token_t* read_number(as_scanner_t* scanner) {
    as_pos_t start_pos = {scanner->line, scanner->column};
    char buffer[256];
    size_t len = 0;
    
    /* Hex: 0x... (for peek/poke addresses in the host REPL) */
    if (peek_char(scanner) == '0' &&
        (peek_next(scanner) == 'x' || peek_next(scanner) == 'X')) {
        buffer[len++] = peek_char(scanner);
        advance(scanner);
        buffer[len++] = peek_char(scanner);
        advance(scanner);
        while (is_hex_digit(peek_char(scanner)) && len < sizeof(buffer) - 1) {
            buffer[len++] = peek_char(scanner);
            advance(scanner);
        }
        buffer[len] = '\0';
        return as_token_new(TOK_NUMBER, start_pos, buffer);
    }

    /* Read integer part */
    while (is_digit(peek_char(scanner)) && len < sizeof(buffer) - 1) {
        buffer[len++] = peek_char(scanner);
        advance(scanner);
    }
    
    /* Read decimal part */
    if (peek_char(scanner) == '.' && is_digit(peek_next(scanner))) {
        buffer[len++] = peek_char(scanner);
        advance(scanner);
        while (is_digit(peek_char(scanner)) && len < sizeof(buffer) - 1) {
            buffer[len++] = peek_char(scanner);
            advance(scanner);
        }
    }
    
    buffer[len] = '\0';
    return as_token_new(TOK_NUMBER, start_pos, buffer);
}

static as_token_t* read_string(as_scanner_t* scanner) {
    as_pos_t start_pos = {scanner->line, scanner->column};
    char quote = peek_char(scanner);
    advance(scanner); /* Skip opening quote */
    
    char buffer[1024];
    size_t len = 0;
    
    while (peek_char(scanner) != quote && peek_char(scanner) != '\0' && len < sizeof(buffer) - 1) {
        if (peek_char(scanner) == '\\') {
            advance(scanner);
            char escaped = peek_char(scanner);
            switch (escaped) {
                case 'n': buffer[len++] = '\n'; break;
                case 't': buffer[len++] = '\t'; break;
                case 'r': buffer[len++] = '\r'; break;
                case '\\': buffer[len++] = '\\'; break;
                case '"': buffer[len++] = '"'; break;
                case '\'': buffer[len++] = '\''; break;
                default: buffer[len++] = escaped; break;
            }
            advance(scanner);
        } else {
            buffer[len++] = peek_char(scanner);
            advance(scanner);
        }
    }
    
    if (peek_char(scanner) == quote) {
        advance(scanner); /* Skip closing quote */
    }
    
    buffer[len] = '\0';
    return as_token_new(TOK_STRING, start_pos, buffer);
}

/* Forward declaration for recursive call */
static as_token_t* read_next_token(as_scanner_t* scanner);

static as_token_t* read_template(as_scanner_t* scanner) {
    as_pos_t start_pos = {scanner->line, scanner->column};
    advance(scanner); /* Skip opening backtick */
    
    /* Array to hold child tokens */
    as_token_t** children = NULL;
    size_t children_count = 0;
    size_t children_capacity = 0;
    
    char buffer[1024];
    size_t len = 0;
    
    typedef enum { STATE_STRING, STATE_ESCAPE, STATE_EXPR, STATE_FINISH } template_state_t;
    template_state_t state = STATE_STRING;
    int expr_bracket_depth = 0;
    
    /* For collecting tokens in expression parts */
    as_token_t** expr_tokens = NULL;
    size_t expr_token_count = 0;
    size_t expr_token_capacity = 0;
    
    while (state != STATE_FINISH) {
        char c = peek_char(scanner);
        
        switch (state) {
            case STATE_STRING: {
                /* Check for EOF */
                if (c == '\0') {
                    fprintf(stderr, "Syntax error: Unterminated template string\n");
                    state = STATE_FINISH;
                    break;
                }
                
                /* Check for escape */
                if (c == '\\') {
                    advance(scanner);
                    state = STATE_ESCAPE;
                    break;
                }
                
                /* Check for template end */
                if (c == '`') {
                    advance(scanner);
                    if (len > 0) {
                        /* Create TemplateStringElement token */
                        buffer[len] = '\0';
                        as_token_t* str_elem = as_token_new(TOK_TEMPLATE_STRING_ELEMENT, start_pos, buffer);
                        
                        /* Add to children */
                        AS_ARRAY_GROW(children, children_count, children_capacity, as_token_t*);
                        children[children_count++] = str_elem;
                    }
                    state = STATE_FINISH;
                    break;
                }
                
                /* Check for expression start */
                if (c == '{') {
                    advance(scanner);
                    if (len > 0) {
                        /* Create TemplateStringElement token */
                        buffer[len] = '\0';
                        as_token_t* str_elem = as_token_new(TOK_TEMPLATE_STRING_ELEMENT, start_pos, buffer);
                        
                        /* Add to children */
                        AS_ARRAY_GROW(children, children_count, children_capacity, as_token_t*);
                        children[children_count++] = str_elem;
                        len = 0;
                    }
                    
                    /* Reset expression token collection */
                    expr_tokens = NULL;
                    expr_token_count = 0;
                    expr_token_capacity = 0;
                    expr_bracket_depth = 0;
                    state = STATE_EXPR;
                    break;
                }
                
                /* Regular character */
                if (len < sizeof(buffer) - 1) {
                    buffer[len++] = c;
                }
                advance(scanner);
                break;
            }
            
            case STATE_ESCAPE: {
                /* Check for EOF */
                if (c == '\0') {
                    fprintf(stderr, "Syntax error: Unterminated template string\n");
                    state = STATE_FINISH;
                    break;
                }
                
                /* Add escaped character */
                if (len < sizeof(buffer) - 1) {
                    buffer[len++] = c;
                }
                advance(scanner);
                state = STATE_STRING;
                break;
            }
            
            case STATE_EXPR: {
                /* Check for EOF */
                if (c == '\0') {
                    fprintf(stderr, "Syntax error: Unterminated template expression\n");
                    state = STATE_FINISH;
                    break;
                }
                
                /* Skip whitespace */
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    advance(scanner);
                    continue;
                }
                
                /* Track bracket depth */
                if (c == '{') {
                    expr_bracket_depth++;
                }
                
                /* Check for expression end */
                if (c == '}') {
                    if (expr_bracket_depth == 0) {
                        /* Expression finished */
                        advance(scanner);
                        
                        /* Create TemplateExprElement token */
                        as_token_t* expr_elem = as_token_new(TOK_TEMPLATE_EXPR_ELEMENT, start_pos, NULL);
                        expr_elem->children = expr_tokens;
                        expr_elem->children_count = expr_token_count;
                        
                        /* Add EOF token to mark end */
                        AS_ARRAY_GROW(expr_elem->children, expr_elem->children_count, 
                                     expr_token_capacity, as_token_t*);
                        expr_elem->children[expr_elem->children_count++] = 
                            as_token_new(TOK_EOF, (as_pos_t){scanner->line, scanner->column}, NULL);
                        
                        /* Add to children */
                        AS_ARRAY_GROW(children, children_count, children_capacity, as_token_t*);
                        children[children_count++] = expr_elem;
                        
                        state = STATE_STRING;
                        break;
                    }
                    expr_bracket_depth--;
                }
                
                /* Read token and add to expression */
                as_token_t* token = read_next_token(scanner);
                AS_ARRAY_GROW(expr_tokens, expr_token_count, expr_token_capacity, as_token_t*);
                expr_tokens[expr_token_count++] = token;
                break;
            }
            
            case STATE_FINISH:
                break;
        }
    }
    
    /* Create Template token with children */
    as_token_t* template_token = as_token_new(TOK_TEMPLATE, start_pos, NULL);
    template_token->children = children;
    template_token->children_count = children_count;
    
    return template_token;
}

static as_token_t* read_identifier(as_scanner_t* scanner) {
    as_pos_t start_pos = {scanner->line, scanner->column};
    char buffer[256];
    size_t len = 0;
    
    while (is_identifier_part(peek_char(scanner)) && len < sizeof(buffer) - 1) {
        buffer[len++] = peek_char(scanner);
        advance(scanner);
    }
    
    buffer[len] = '\0';
    
    /* Check for keywords */
    if (strcmp(buffer, "null") == 0) return as_token_new(TOK_NULL, start_pos, NULL);
    if (strcmp(buffer, "true") == 0) return as_token_new(TOK_TRUE, start_pos, NULL);
    if (strcmp(buffer, "false") == 0) return as_token_new(TOK_FALSE, start_pos, NULL);
    if (strcmp(buffer, "each") == 0) return as_token_new(TOK_EACH, start_pos, NULL);
    if (strcmp(buffer, "for") == 0) return as_token_new(TOK_FOR, start_pos, NULL);
    if (strcmp(buffer, "loop") == 0) return as_token_new(TOK_LOOP, start_pos, NULL);
    if (strcmp(buffer, "do") == 0) return as_token_new(TOK_DO, start_pos, NULL);
    if (strcmp(buffer, "while") == 0) return as_token_new(TOK_WHILE, start_pos, NULL);
    if (strcmp(buffer, "break") == 0) return as_token_new(TOK_BREAK, start_pos, NULL);
    if (strcmp(buffer, "continue") == 0) return as_token_new(TOK_CONTINUE, start_pos, NULL);
    if (strcmp(buffer, "match") == 0) return as_token_new(TOK_MATCH, start_pos, NULL);
    if (strcmp(buffer, "case") == 0) return as_token_new(TOK_CASE, start_pos, NULL);
    if (strcmp(buffer, "default") == 0) return as_token_new(TOK_DEFAULT, start_pos, NULL);
    if (strcmp(buffer, "if") == 0) return as_token_new(TOK_IF, start_pos, NULL);
    if (strcmp(buffer, "elif") == 0) return as_token_new(TOK_ELIF, start_pos, NULL);
    if (strcmp(buffer, "else") == 0) return as_token_new(TOK_ELSE, start_pos, NULL);
    if (strcmp(buffer, "return") == 0) return as_token_new(TOK_RETURN, start_pos, NULL);
    if (strcmp(buffer, "eval") == 0) return as_token_new(TOK_EVAL, start_pos, NULL);
    if (strcmp(buffer, "var") == 0) return as_token_new(TOK_VAR, start_pos, NULL);
    if (strcmp(buffer, "let") == 0) return as_token_new(TOK_LET, start_pos, NULL);
        if (strcmp(buffer, "exists") == 0) return as_token_new(TOK_EXISTS, start_pos, NULL);
        if (strcmp(buffer, "from") == 0) return as_token_new(TOK_FROM, start_pos, NULL);
        if (strcmp(buffer, "import") == 0) return as_token_new(TOK_IMPORT, start_pos, NULL);
        if (strcmp(buffer, "as") == 0) return as_token_new(TOK_AS, start_pos, NULL);
    
    return as_token_new(TOK_IDENTIFIER, start_pos, buffer);
}

static as_token_t* read_next_token(as_scanner_t* scanner) {
    /* Skip whitespace, comments, and '\' + newline line continuations */
    int continued = 0;
    while (true) {
        skip_whitespace(scanner);

        if (peek_char(scanner) == '/' && (peek_next(scanner) == '/' || peek_next(scanner) == '*')) {
            skip_comment(scanner);
            continue;
        }

        /* `\<newline>` continues an expression across a line break */
        if (peek_char(scanner) == '\\') {
            size_t i = scanner->pos + 1;
            const char* src = scanner->source;
            while (src[i] == ' ' || src[i] == '\t') {
                i++;
            }
            if (src[i] == '\n' || src[i] == '\r') {
                advance(scanner); /* backslash */
                skip_whitespace(scanner);
                if (is_newline(peek_char(scanner))) {
                    advance(scanner);
                    if (peek_char(scanner) == '\n' && scanner->source[scanner->pos - 1] == '\r') {
                        advance(scanner);
                    }
                }
                continued = 1;
                continue;
            }
        }

        /* After `\`, keep skipping newlines (and comments via the loop) so
         * `2 \ \n // c \n * 3` tokenizes as `2 * 3`. */
        if (continued && is_newline(peek_char(scanner))) {
            while (is_newline(peek_char(scanner))) {
                advance(scanner);
            }
            continue;
        }
        break;
    }
    
    as_pos_t pos = {scanner->line, scanner->column};
    char c = peek_char(scanner);
    
    if (c == '\0') {
        return as_token_new(TOK_EOF, pos, NULL);
    }
    
    if (is_newline(c)) {
        advance(scanner);
        /* Skip multiple newlines */
        while (is_newline(peek_char(scanner))) {
            advance(scanner);
        }
        return as_token_new(TOK_NEWLINE, pos, NULL);
    }
    
    if (is_digit(c)) {
        return read_number(scanner);
    }
    
    if (c == '"' || c == '\'') {
        return read_string(scanner);
    }
    
    if (c == '`') {
        return read_template(scanner);
    }
    
    if (is_identifier_start(c)) {
        return read_identifier(scanner);
    }
    
    /* Two-character operators */
    advance(scanner);
    char next = peek_char(scanner);
    
    switch (c) {
        case '!':
            if (next == '=') { advance(scanner); return as_token_new(TOK_NOT_EQ, pos, NULL); }
            return as_token_new(TOK_NOT, pos, NULL);
        case '&':
            if (next == '&') { advance(scanner); return as_token_new(TOK_AND, pos, NULL); }
            break;
        case '|':
            if (next == '|') { advance(scanner); return as_token_new(TOK_OR2, pos, NULL); }
            return as_token_new(TOK_OR, pos, NULL);
        case '=':
            if (next == '=') { advance(scanner); return as_token_new(TOK_EQ2, pos, NULL); }
            if (next == '>') { advance(scanner); return as_token_new(TOK_ARROW, pos, NULL); }
            return as_token_new(TOK_EQ, pos, NULL);
        case '<':
            if (next == '=') { advance(scanner); return as_token_new(TOK_LT_EQ, pos, NULL); }
            if (next == ':') { advance(scanner); return as_token_new(TOK_OUT, pos, NULL); }
            return as_token_new(TOK_LT, pos, NULL);
        case '>':
            if (next == '=') { advance(scanner); return as_token_new(TOK_GT_EQ, pos, NULL); }
            return as_token_new(TOK_GT, pos, NULL);
        case '+':
            if (next == '=') { advance(scanner); return as_token_new(TOK_PLUS_EQ, pos, NULL); }
            return as_token_new(TOK_PLUS, pos, NULL);
        case '-':
            if (next == '=') { advance(scanner); return as_token_new(TOK_MINUS_EQ, pos, NULL); }
            return as_token_new(TOK_MINUS, pos, NULL);
        case ':':
            if (next == ':') { advance(scanner); return as_token_new(TOK_COLON2, pos, NULL); }
            return as_token_new(TOK_COLON, pos, NULL);
        case '#':
            if (next == '[') { advance(scanner); return as_token_new(TOK_SHARP_BRACKET, pos, NULL); }
            if (next == '#' && peek_next(scanner) == '#') {
                advance(scanner);
                advance(scanner);
                return as_token_new(TOK_SHARP3, pos, NULL);
            }
            return as_token_new(TOK_SHARP, pos, NULL);
            
        /* Single-character tokens */
        case '%': return as_token_new(TOK_PERCENT, pos, NULL);
        case '(': return as_token_new(TOK_LPAREN, pos, NULL);
        case ')': return as_token_new(TOK_RPAREN, pos, NULL);
        case '*': return as_token_new(TOK_ASTERISK, pos, NULL);
        case ',': return as_token_new(TOK_COMMA, pos, NULL);
        case '.': return as_token_new(TOK_DOT, pos, NULL);
        case '/': return as_token_new(TOK_SLASH, pos, NULL);
        case ';': return as_token_new(TOK_SEMICOLON, pos, NULL);
        case '?': return as_token_new(TOK_QUESTION, pos, NULL);
        case '@': return as_token_new(TOK_AT, pos, NULL);
        case '[': return as_token_new(TOK_LBRACKET, pos, NULL);
        case '\\': return as_token_new(TOK_BACKSLASH, pos, NULL);
        case ']': return as_token_new(TOK_RBRACKET, pos, NULL);
        case '^': return as_token_new(TOK_HAT, pos, NULL);
        case '{': return as_token_new(TOK_LBRACE, pos, NULL);
        case '}': return as_token_new(TOK_RBRACE, pos, NULL);
    }
    
    return as_token_new(TOK_EOF, pos, NULL);
}

/* Token-list mode helpers (used for template expressions) */
static as_token_t* token_list_at(as_scanner_t* scanner, size_t offset) {
    size_t idx = scanner->token_list_pos + offset;
    if (idx < scanner->token_list_count) {
        return scanner->token_list[idx];
    }
    if (scanner->token_list_count > 0) {
        as_token_t* last = scanner->token_list[scanner->token_list_count - 1];
        if (last->kind == TOK_EOF) {
            return last;
        }
    }
    return as_token_new(TOK_EOF, (as_pos_t){scanner->line, scanner->column}, NULL);
}

/* Public API */
as_scanner_t* as_scanner_new(const char* source) {
    as_scanner_t* scanner = as_malloc(sizeof(as_scanner_t));
    scanner->source = source;
    scanner->pos = 0;
    scanner->line = 1;
    scanner->column = 1;
    scanner->current_token = read_next_token(scanner);
    scanner->lookahead_token = NULL;
    scanner->token_list = NULL;
    scanner->token_list_count = 0;
    scanner->token_list_pos = 0;
    return scanner;
}

as_scanner_t* as_scanner_from_token_list(as_token_t** tokens, size_t count) {
    as_scanner_t* scanner = as_malloc(sizeof(as_scanner_t));
    memset(scanner, 0, sizeof(as_scanner_t));
    scanner->source = "";
    scanner->token_list = tokens;
    scanner->token_list_count = count;
    scanner->token_list_pos = 0;
    if (count > 0) {
        scanner->current_token = tokens[0];
        scanner->line = tokens[0]->pos.line;
        scanner->column = tokens[0]->pos.column;
    } else {
        scanner->current_token = as_token_new(TOK_EOF, (as_pos_t){1, 1}, NULL);
    }
    return scanner;
}

void as_scanner_free(as_scanner_t* scanner) {
    if (!scanner) return;
    if (!scanner->token_list) {
        if (scanner->current_token) as_token_free(scanner->current_token);
        if (scanner->lookahead_token) as_token_free(scanner->lookahead_token);
    }
    as_free(scanner);
}

as_token_t* as_scanner_peek(as_scanner_t* scanner) {
    if (scanner->token_list) {
        return token_list_at(scanner, 0);
    }
    return scanner->current_token;
}

as_token_t* as_scanner_lookahead(as_scanner_t* scanner, int offset) {
    if (scanner->token_list) {
        if (offset < 0) offset = 0;
        return token_list_at(scanner, (size_t)offset);
    }
    if (offset == 0) return scanner->current_token;
    if (offset == 1 && scanner->lookahead_token) return scanner->lookahead_token;
    
    /* For simplicity, only support lookahead of 1 */
    if (offset == 1) {
        scanner->lookahead_token = read_next_token(scanner);
        return scanner->lookahead_token;
    }
    
    return scanner->current_token;
}

void as_scanner_next(as_scanner_t* scanner) {
    if (scanner->token_list) {
        if (scanner->token_list_pos + 1 < scanner->token_list_count) {
            scanner->token_list_pos++;
        } else if (scanner->token_list_pos < scanner->token_list_count) {
            scanner->token_list_pos = scanner->token_list_count - 1;
        }
        scanner->current_token = token_list_at(scanner, 0);
        scanner->lookahead_token = NULL;
        return;
    }
    if (scanner->current_token) {
        as_token_free(scanner->current_token);
    }
    
    if (scanner->lookahead_token) {
        scanner->current_token = scanner->lookahead_token;
        scanner->lookahead_token = NULL;
    } else {
        scanner->current_token = read_next_token(scanner);
    }
}

bool as_scanner_is(as_scanner_t* scanner, as_token_kind_t kind) {
    return scanner->current_token && scanner->current_token->kind == kind;
}

bool as_scanner_expect(as_scanner_t* scanner, as_token_kind_t kind, char** error) {
    if (!as_scanner_is(scanner, kind)) {
        if (error) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "Expected %s but got %s",
                    as_token_kind_to_string(kind),
                    as_token_kind_to_string(scanner->current_token->kind));
            *error = as_strdup(buffer);
        }
        return false;
    }
    return true;
}

as_pos_t as_scanner_pos(as_scanner_t* scanner) {
    if (scanner->current_token) {
        return scanner->current_token->pos;
    }
    as_pos_t pos = {scanner->line, scanner->column};
    return pos;
}

