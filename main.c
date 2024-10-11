#include <ctype.h>
#define DS_IO_IMPLEMENTATION
#define DS_AP_IMPLEMENTATION
#define DS_SB_IMPLEMENTATION
#define DS_SS_IMPLEMENTATION
#define DS_HM_IMPLEMENTATION
#define DS_DA_IMPLEMENTATION
#include "ds.h"

typedef enum json_token_kind {
    JSON_TOKEN_LBRACE,
    JSON_TOKEN_RBRACE,
    JSON_TOKEN_LSQRLY,
    JSON_TOKEN_RSQRLY,
    JSON_TOKEN_COLON,
    JSON_TOKEN_COMMA,
    JSON_TOKEN_BOOLEAN,
    JSON_TOKEN_NUMBER,
    JSON_TOKEN_STRING,
    JSON_TOKEN_NULL,
    JSON_TOKEN_EOF,
    JSON_TOKEN_ILLEGAL,
} json_token_kind;

const char* json_token_kind_to_string(json_token_kind kind) {
    switch (kind) {
    case JSON_TOKEN_LBRACE: return "[";
    case JSON_TOKEN_RBRACE: return "]";
    case JSON_TOKEN_LSQRLY: return "{";
    case JSON_TOKEN_RSQRLY: return "}";
    case JSON_TOKEN_COLON: return ":";
    case JSON_TOKEN_COMMA: return ",";
    case JSON_TOKEN_BOOLEAN: return "boolean";
    case JSON_TOKEN_NUMBER: return "number";
    case JSON_TOKEN_STRING: return "string";
    case JSON_TOKEN_NULL: return "null";
    case JSON_TOKEN_EOF: return "<EOF>";
    case JSON_TOKEN_ILLEGAL: return "ILLEGAL";
    }
}

typedef struct json_token {
    json_token_kind kind;
    ds_string_slice value;
    unsigned int pos;
} json_token;

typedef struct json_lexer {
    const char *buffer;
    unsigned int buffer_len;
    unsigned int pos;
    unsigned int read_pos;
    char ch;
} json_lexer;

int json_lexer_init(json_lexer *lexer, const char *buffer, unsigned int buffer_len);
int json_lexer_peek(json_lexer *lexer, json_token *token);
int json_lexer_next(json_lexer *lexer, json_token *token);
int json_lexer_pos_to_lc(json_lexer *lexer, int pos, int *line, int *column);
void json_lexer_free(json_lexer *lexer);

static char json_lexer_peek_ch(json_lexer *lexer) {
    if (lexer->read_pos >= lexer->buffer_len) {
        return EOF;
    }

    return lexer->buffer[lexer->read_pos];
}

static char json_lexer_read(json_lexer *lexer) {
    lexer->ch = json_lexer_peek_ch(lexer);

    lexer->pos = lexer->read_pos;
    lexer->read_pos += 1;

    return lexer->ch;
}

static void json_lexer_skip_whitespace(json_lexer *lexer) {
    while (isspace(lexer->ch)) {
        json_lexer_read(lexer);
    }
}

int json_lexer_init(json_lexer *lexer, const char *buffer, unsigned int buffer_len) {
    lexer->buffer = buffer;
    lexer->buffer_len = buffer_len;
    lexer->pos = 0;
    lexer->read_pos = 0;
    lexer->ch = 0;

    json_lexer_read(lexer);

    return 0;
}

static int json_lexer_tokenize_string(json_lexer *lexer, json_token *token) {
    int result = 0;
    unsigned int position = lexer->pos;
    char *value = NULL;

    if (lexer->ch != '"') {
        DS_LOG_ERROR("Failed to parse string: expected '\"' but got '%c'", lexer->ch);
        return_defer(1);
    }

    json_lexer_read(lexer);

    ds_string_slice slice = { .str = (char *)lexer->buffer + lexer->pos, .len = 0 };
    while (lexer->ch != '"') {
        char ch = lexer->ch;
        slice.len += 1;
        json_lexer_read(lexer);

        if (ch == '\\' && lexer->ch == '"') {
            slice.len += 1;
            json_lexer_read(lexer);
        }
    }

    json_lexer_read(lexer);

    *token = (json_token){.kind = JSON_TOKEN_STRING, .value = slice, .pos = position };

defer:
    return result;
}

static int json_lexer_tokenize_ident(json_lexer *lexer, json_token *token) {
    int result = 0;
    unsigned int position = lexer->pos;
    char *value = NULL;

    if (!islower(lexer->ch)) {
        DS_LOG_ERROR("Failed to parse ident: expected islower but got '%c'", lexer->ch);
        return_defer(1);
    }

    ds_string_slice slice = { .str = (char *)lexer->buffer + lexer->pos, .len = 0 };
    while (islower(lexer->ch)) {
        slice.len += 1;
        json_lexer_read(lexer);
    }

    if (ds_string_slice_to_owned(&slice, &value) != 0) {
        DS_LOG_ERROR("Failed to allocate string");
        return_defer(1);
    }

    if (strcmp(value, "null") == 0) {
        *token = (json_token){.kind = JSON_TOKEN_NULL, .value = 0, .pos = position };
    } else if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) {
        *token = (json_token){.kind = JSON_TOKEN_BOOLEAN, .value = slice, .pos = position };
    } else {
        *token = (json_token){.kind = JSON_TOKEN_ILLEGAL, .value = slice, .pos = position };
    }

defer:
    DS_FREE(NULL, value);
    return result;
}

static int json_lexer_tokenize_number(json_lexer *lexer, json_token *token) {
    int result = 0;
    unsigned int position = lexer->pos;
    char *value = NULL;

    int found_dot = 0;

    if (!(isdigit(lexer->ch) || lexer->ch == '.' || lexer->ch == '-')) {
        DS_LOG_ERROR("Failed to parse number: expected digit, '.' or '-' but got '%c'", lexer->ch);
        return_defer(1);
    }

    ds_string_slice slice = { .str = (char *)lexer->buffer + lexer->pos, .len = 0 };

    if (lexer->ch == '-') {
        slice.len += 1;
        json_lexer_read(lexer);
    }

    while (isdigit(lexer->ch) || (lexer->ch == '.' && found_dot == 0)) {
        if (lexer->ch == '.') {
            found_dot = 1;
        }

        slice.len += 1;
        json_lexer_read(lexer);
    }

    *token = (json_token){.kind = JSON_TOKEN_NUMBER, .value = slice, .pos = position };

defer:
    return result;
}

int json_lexer_peek(json_lexer *lexer, json_token *token) {
    unsigned int pos = lexer->pos;
    unsigned int read_pos = lexer->read_pos;
    unsigned int ch = lexer->ch;

    int result = json_lexer_next(lexer, token);

    lexer->pos = pos;
    lexer->read_pos = read_pos;
    lexer->ch = ch;

    return result;
}

int json_lexer_next(json_lexer *lexer, json_token *token) {
    int result = 0;
    json_lexer_skip_whitespace(lexer);

    unsigned int position = lexer->pos;
    if (lexer->ch == EOF) {
        json_lexer_read(lexer);
        *token = (json_token){.kind = JSON_TOKEN_EOF, .value = 0, .pos = position };
        return_defer(0);
    } else if (lexer->ch == '{') {
        json_lexer_read(lexer);
        *token = (json_token){.kind = JSON_TOKEN_LSQRLY, .value = 0, .pos = position };
        return_defer(0);
    } else if (lexer->ch == '}') {
        json_lexer_read(lexer);
        *token = (json_token){.kind = JSON_TOKEN_RSQRLY, .value = 0, .pos = position };
        return_defer(0);
    } else if (lexer->ch == '[') {
        json_lexer_read(lexer);
        *token = (json_token){.kind = JSON_TOKEN_LBRACE, .value = 0, .pos = position };
        return_defer(0);
    } else if (lexer->ch == ']') {
        json_lexer_read(lexer);
        *token = (json_token){.kind = JSON_TOKEN_RBRACE, .value = 0, .pos = position };
        return_defer(0);
    } else if (lexer->ch == ':') {
        json_lexer_read(lexer);
        *token = (json_token){.kind = JSON_TOKEN_COLON, .value = 0, .pos = position };
        return_defer(0);
    } else if (lexer->ch == ',') {
        json_lexer_read(lexer);
        *token = (json_token){.kind = JSON_TOKEN_COMMA, .value = 0, .pos = position };
        return_defer(0);
    } else if (lexer->ch == '"') {
        return_defer(json_lexer_tokenize_string(lexer, token));
    } else if (islower(lexer->ch)) {
        return_defer(json_lexer_tokenize_ident(lexer, token));
    } else if (isdigit(lexer->ch) || lexer->ch == '.' || lexer->ch == '-') {
        return_defer(json_lexer_tokenize_number(lexer, token));
    } else {
        char *value = NULL;
        ds_string_slice slice = { .str = (char *)lexer->buffer + lexer->pos, .len = 1 };

        json_lexer_read(lexer);

        *token = (json_token){.kind = JSON_TOKEN_ILLEGAL, .value = slice, .pos = position };

        return_defer(0);
    }

defer:
    return result;
}

int json_lexer_pos_to_lc(json_lexer *lexer, int pos, int *line, int *column) {
    int result = 0;
    int n = (pos > lexer->buffer_len) ? lexer->buffer_len : pos;

    *line = 1;
    *column = 1;

    for (int i = 0; i < n; i++) {
        if (lexer->buffer[i] == '\n') {
            *line += 1;
            *column = 0;
        } else {
            *column += 1;
        }
    }

defer:
    return result;
}

void json_lexer_free(json_lexer *lexer) {
    lexer->buffer = NULL;
    lexer->buffer_len = 0;
    lexer->pos = 0;
    lexer->read_pos = 0;
    lexer->ch = 0;
}

typedef enum {
    JSON_OBJECT_STRING,
    JSON_OBJECT_NUMBER,
    JSON_OBJECT_BOOLEAN,
    JSON_OBJECT_NULL,
    JSON_OBJECT_ARRAY,
    JSON_OBJECT_MAP
} json_object_kind;

typedef struct json_object {
    json_object_kind kind;
    union {
        char *string;
        double number;
        bool boolean;
        ds_dynamic_array array; /* json_object */
        ds_hashmap map; /* <char* , json_object> */
    };
} json_object;

#define JSON_OBJECT_DUMP_INDENT 2

#define MAX_CAPACITY 100

static unsigned int json_object_hash(const void *key) {
    unsigned int hash = 0;
    char *name = (char *)key;
    for (unsigned int i = 0; i < strlen(name); i++) {
        hash = 31 * hash + name[i];
    }
    return hash % MAX_CAPACITY;
}

static int json_object_compare(const void *k1, const void *k2) {
    return strcmp((char *)k1, (char *)k2);
}

int json_object_load(char *buffer, unsigned int buffer_len, json_object *object);
int json_object_debug(json_object *object);
int json_object_free(json_object *object);

static int json_object_debug_indent(json_object *object, int indent) {
    int result = 0;

    switch (object->kind) {
    case JSON_OBJECT_STRING:
        printf("%*s[STRING]: \'%s\'\n", indent, "", object->string);
        break;
    case JSON_OBJECT_NUMBER:
        printf("%*s[NUMBER]: %f\n", indent, "", object->number);
        break;
    case JSON_OBJECT_BOOLEAN:
        printf("%*s[BOOLEAN]: %s\n", indent, "", object->boolean == true ? "true" : "false");
        break;
    case JSON_OBJECT_NULL:
        printf("%*s[NULL]\n", indent, "");
        break;
    case JSON_OBJECT_ARRAY:
        printf("%*s[ARRAY]: [\n", indent, "");
        for (int i = 0; i < object->array.count; i++) {
            json_object item = {0};
            if (ds_dynamic_array_get(&object->array, i, &item) != 0) {
                DS_LOG_ERROR("Failed to get item from array");
                return_defer(1);
            }

            if (json_object_debug_indent(&item, indent + JSON_OBJECT_DUMP_INDENT) != 0) {
                return_defer(1);
            }
        }
        printf("%*s]\n", indent, "");
        break;
    case JSON_OBJECT_MAP:
        printf("%*s[MAP]: {\n", indent, "");
        for (int i = 0; i < object->map.capacity; i++) {
            ds_dynamic_array bucket = object->map.buckets[i];

            for (int j = 0; j < bucket.count; j++) {
                ds_hashmap_kv kv = {0};
                if (ds_dynamic_array_get(&bucket, j, &kv) != 0) {
                    DS_LOG_ERROR("Failed to get item from array");
                    return_defer(1);
                }

                printf("%*s[KEY]: \'%s\'\n", indent, "", (char *)kv.key);
                if (json_object_debug_indent((json_object*)kv.value, indent + JSON_OBJECT_DUMP_INDENT) != 0) {
                    return_defer(1);
                }
            }
        }
        printf("%*s}\n", indent, "");
        break;
    }

defer:
    return result;
}

int json_object_debug(json_object *object) {
    return json_object_debug_indent(object, 0);
}

int json_object_free(json_object *object) {
    int result = 0;

    switch (object->kind) {
    case JSON_OBJECT_STRING:
        DS_FREE(NULL, object->string);
        break;
    case JSON_OBJECT_NUMBER:
        break;
    case JSON_OBJECT_BOOLEAN:
        break;
    case JSON_OBJECT_NULL:
        break;
    case JSON_OBJECT_ARRAY:
        for (int i = 0; i < object->array.count; i++) {
            json_object *item = NULL;
            if (ds_dynamic_array_get_ref(&object->array, i, (void **)&item) != 0) {
                DS_LOG_ERROR("Failed to get item from array");
                return_defer(1);
            }

            if (json_object_free(item) != 0) {
                DS_LOG_ERROR("Failed to free json object");
                return_defer(1);
            }
        }
        ds_dynamic_array_free(&object->array);
        break;
    case JSON_OBJECT_MAP:
        for (int i = 0; i < object->map.capacity; i++) {
            ds_dynamic_array bucket = object->map.buckets[i];

            for (int j = 0; j < bucket.count; j++) {
                ds_hashmap_kv kv = {0};
                if (ds_dynamic_array_get(&bucket, j, &kv) != 0) {
                    DS_LOG_ERROR("Failed to get item from array");
                    return_defer(1);
                }

                DS_FREE(NULL, kv.key);
                if (json_object_free(kv.value) != 0) {
                    DS_LOG_ERROR("Failed to free json object");
                    return_defer(1);
                }
                DS_FREE(NULL, kv.value);
            }
        }
        ds_hashmap_free(&object->map);
        break;
    }

defer:
    return result;
}

typedef struct json_parser {
    json_lexer lexer;
} json_parser;

int json_parser_init(json_parser *parser, json_lexer lexer);
int json_parser_parse(json_parser *parser, json_object *object);
void json_parser_free(json_parser *parser);

int json_parser_init(json_parser *parser, json_lexer lexer) {
    parser->lexer = lexer;

    return 0;
}

static int json_parser_parse_object(json_parser *parser, json_object *object);
static int json_parser_parse_map(json_parser *parser, json_object *object);
static int json_parser_parse_array(json_parser *parser, json_object *object);

static int json_parser_parse_object(json_parser *parser, json_object *object) {
    int result = 0;
    json_token token = {0};

    if (json_lexer_next(&parser->lexer, &token) != 0) {
        DS_LOG_ERROR("Failed to get the next token");
        return_defer(1);
    }

    if (token.kind == JSON_TOKEN_LSQRLY) {
        result = json_parser_parse_map(parser, object);
    } else if (token.kind == JSON_TOKEN_LBRACE) {
        result = json_parser_parse_array(parser, object);
    } else if (token.kind == JSON_TOKEN_STRING) {
        object->kind = JSON_OBJECT_STRING;
        if (ds_string_slice_to_owned(&token.value, &object->string) != 0) {
            DS_LOG_ERROR("Failed to allocate string");
            return_defer(1);
        }
    } else if (token.kind == JSON_TOKEN_NUMBER) {
        char *value = NULL;
        object->kind = JSON_OBJECT_NUMBER;
        if (ds_string_slice_to_owned(&token.value, &value) != 0) {
            DS_LOG_ERROR("Failed to allocate string");
            return_defer(1);
        }
        object->number = atof(value);
        DS_FREE(NULL, value);
    } else if (token.kind == JSON_TOKEN_BOOLEAN) {
        char *value = NULL;
        object->kind = JSON_OBJECT_BOOLEAN;
        if (ds_string_slice_to_owned(&token.value, &value) != 0) {
            DS_LOG_ERROR("Failed to allocate string");
            return_defer(1);
        }
        object->boolean = (strcmp(value, "true") == 0) ? true : false;
        DS_FREE(NULL, value);
    } else if (token.kind == JSON_TOKEN_NULL) {
        object->kind = JSON_OBJECT_NULL;
    } else {
        int line, column;
        json_lexer_pos_to_lc(&parser->lexer, token.pos, &line, &column);
        DS_LOG_ERROR("Expected a json object but found %s at %d:%d", json_token_kind_to_string(token.kind), line, column);
        return_defer(1);
    }

defer:
    return result;
}

static int json_parser_parse_map(json_parser *parser, json_object *object) {
    int result = 0;
    json_token token = {0};

    object->kind = JSON_OBJECT_MAP;
    ds_hashmap_init(&object->map, MAX_CAPACITY, json_object_hash, json_object_compare);

    if (json_lexer_next(&parser->lexer, &token) != 0) {
        DS_LOG_ERROR("Failed to get the next token");
        return_defer(1);
    }

    if (token.kind == JSON_TOKEN_RSQRLY) {
        return_defer(0);
    }

    while (token.kind != JSON_TOKEN_RSQRLY) {
        ds_hashmap_kv kv = {0};

        if (token.kind != JSON_TOKEN_STRING) {
            int line, column;
            json_lexer_pos_to_lc(&parser->lexer, token.pos, &line, &column);
            DS_LOG_ERROR("Expected a string but found %s at %d:%d", json_token_kind_to_string(token.kind), line, column);
            return_defer(1);
        }

        if (ds_string_slice_to_owned(&token.value, (char **)&kv.key) != 0) {
            DS_LOG_ERROR("Failed to allocate string");
            return_defer(1);
        }

        if (json_lexer_next(&parser->lexer, &token) != 0) {
            DS_LOG_ERROR("Failed to get the next token");
            return_defer(1);
        }

        if (token.kind != JSON_TOKEN_COLON) {
            int line, column;
            json_lexer_pos_to_lc(&parser->lexer, token.pos, &line, &column);
            DS_LOG_ERROR("Expected a colon but found %s at %d:%d", json_token_kind_to_string(token.kind), line, column);
            return_defer(1);
        }

        kv.value = DS_MALLOC(NULL, sizeof(json_object));
        if (kv.value == NULL) {
            DS_LOG_ERROR("Failed to allocate value for map");
            return_defer(1);
        }

        if (json_parser_parse_object(parser, (json_object *)kv.value) != 0) {
            DS_LOG_ERROR("Failed to parse map value");
            return_defer(1);
        }

        if (ds_hashmap_insert(&object->map, &kv) != 0) {
            DS_LOG_ERROR("Failed to insert item to map");
            return_defer(1);
        }

        if (json_lexer_next(&parser->lexer, &token) != 0) {
            DS_LOG_ERROR("Failed to get the next token");
            return_defer(1);
        }

        if (token.kind == JSON_TOKEN_RSQRLY) {
            break;
        }

        if (token.kind != JSON_TOKEN_COMMA) {
            int line, column;
            json_lexer_pos_to_lc(&parser->lexer, token.pos, &line, &column);
            DS_LOG_ERROR("Expected a comma but found %s at %d:%d", json_token_kind_to_string(token.kind), line, column);
            return_defer(1);
        }

        if (json_lexer_next(&parser->lexer, &token) != 0) {
            DS_LOG_ERROR("Failed to get the next token");
            return_defer(1);
        }
    }

defer:
    return result;
}

static int json_parser_parse_array(json_parser *parser, json_object *object) {
    int result = 0;
    json_token token = {0};

    object->kind = JSON_OBJECT_ARRAY;
    ds_dynamic_array_init(&object->array, sizeof(json_object));

    if (json_lexer_peek(&parser->lexer, &token) != 0) {
        DS_LOG_ERROR("Failed to get the next token");
        return_defer(1);
    }

    if (token.kind == JSON_TOKEN_RBRACE) {
        return_defer(0);
    }

    while (token.kind != JSON_TOKEN_RBRACE) {
        json_object item = {0};
        if (json_parser_parse_object(parser, &item) != 0) {
            DS_LOG_ERROR("Failed to parse array item");
            return_defer(1);
        }

        if (ds_dynamic_array_append(&object->array, &item) != 0) {
            DS_LOG_ERROR("Failed to add item to array");
            return_defer(1);
        }

        if (json_lexer_next(&parser->lexer, &token) != 0) {
            DS_LOG_ERROR("Failed to get the next token");
            return_defer(1);
        }

        if (token.kind == JSON_TOKEN_RBRACE) {
            break;
        }

        if (token.kind != JSON_TOKEN_COMMA) {
            int line, column;
            json_lexer_pos_to_lc(&parser->lexer, token.pos, &line, &column);
            DS_LOG_ERROR("Expected a comma but found %s at %d:%d", json_token_kind_to_string(token.kind), line, column);
            return_defer(1);
        }
    }

defer:
    return result;
}

int json_parser_parse(json_parser *parser, json_object *object) {
    int result = 0;
    json_token token = {0};

    if (json_parser_parse_object(parser, object) != 0) {
        DS_LOG_ERROR("Failed to parse json object");
        return_defer(1);
    }

    if (json_lexer_next(&parser->lexer, &token) != 0) {
        DS_LOG_ERROR("Failed to get the next token");
        return_defer(1);
    }

    if (token.kind != JSON_TOKEN_EOF) {
        int line, column;
        json_lexer_pos_to_lc(&parser->lexer, token.pos, &line, &column);
        DS_LOG_ERROR("Expected end of file but found %s at %d:%d", json_token_kind_to_string(token.kind), line, column);
        return_defer(1);
    }

defer:
    return result;
}

void json_parser_free(json_parser *parser) { }

int json_object_load(char *buffer, unsigned int buffer_len, json_object *object) {
    int result = 0;
    json_lexer lexer = {0};
    json_parser parser = {0};

    json_lexer_init(&lexer, buffer, buffer_len);
    json_parser_init(&parser, lexer);

    if (json_parser_parse(&parser, object) != 0) {
        DS_LOG_ERROR("Failed to parse json");
        return_defer(1);
    }

defer:
    json_parser_free(&parser);
    json_lexer_free(&lexer);
    return result;
}

static int argparse(int argc, char **argv, char **filename) {
    int result = 0;
    ds_argparse_parser argparser = {0};

    ds_argparse_parser_init(&argparser, "json-c", "json parser in c" , "0.1");

    if (ds_argparse_add_argument(&argparser, (ds_argparse_options){
        .short_name = 'i',
        .long_name = "input",
        .description = "the input file",
        .type = ARGUMENT_TYPE_POSITIONAL,
        .required = 0,
    }) != 0) {
        DS_LOG_ERROR("Failed to add argument `input`");
        return_defer(1);
    }

    if (ds_argparse_parse(&argparser, argc, argv) != 0) {
        DS_LOG_ERROR("Failed to parse arguments");
        return_defer(1);
    }

    *filename = ds_argparse_get_value(&argparser, "input");

defer:
    ds_argparse_parser_free(&argparser);
    return result;
}

int main(int argc, char **argv) {
    int result = 0;
    char *filename = NULL;
    char *buffer = NULL;
    int buffer_len;
    json_object object = {0};

    if (argparse(argc, argv, &filename) != 0) {
        DS_LOG_ERROR("Failed to parse arguments");
        return_defer(1);
    }

    buffer_len = ds_io_read(filename, &buffer, "r");
    if (buffer_len < 0) {
        DS_LOG_ERROR("Failed to read from file: %s", (filename == NULL) ? "stdin" : filename);
        return_defer(-1);
    }

    if (json_object_load(buffer, buffer_len, &object) != 0) {
        DS_LOG_ERROR("Failed to parse json");
        return_defer(1);
    }

    if (json_object_debug(&object) != 0) {
        DS_LOG_ERROR("Failed to dump json");
        return_defer(1);
    }

defer:
    json_object_free(&object);
    if (buffer != NULL) {
        DS_FREE(NULL, buffer);
    }
    return result;
}
