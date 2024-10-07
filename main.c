#include <ctype.h>
#define DS_IO_IMPLEMENTATION
#define DS_AP_IMPLEMENTATION
#define DS_SB_IMPLEMENTATION
#define DS_SS_IMPLEMENTATION
#include "ds.h"

typedef int bool;

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

typedef struct json_token {
    json_token_kind kind;
    const char *value;
    unsigned int pos;
} json_token;

typedef struct json_lexer {
    const char *buffer;
    unsigned int buffer_len;
    unsigned int pos;
    unsigned int read_pos;
    char ch;
} json_lexer;

static char json_lexer_peek(json_lexer *lexer) {
    if (lexer->read_pos >= lexer->buffer_len) {
        return EOF;
    }

    return lexer->buffer[lexer->read_pos];
}

static char json_lexer_read(json_lexer *lexer) {
    lexer->ch = json_lexer_peek(lexer);

    lexer->pos = lexer->read_pos;
    lexer->read_pos += 1;

    return lexer->ch;
}

static void json_lexer_skip_whitespace(json_lexer *lexer) {
    while (isspace(lexer->ch)) {
        json_lexer_read(lexer);
    }
}

int json_lexer_init(json_lexer *lexer, const char *buffer, unsigned int buffer_len);
json_token json_lexer_next(json_lexer *lexer);
void json_lexer_free(json_lexer *lexer);

int json_lexer_init(json_lexer *lexer, const char *buffer, unsigned int buffer_len) {
    lexer->buffer = buffer;
    lexer->buffer_len = buffer_len;
    lexer->pos = 0;
    lexer->read_pos = 0;
    lexer->ch = 0;

    json_lexer_read(lexer);

    return 0;
}

static json_token json_lexer_literal_to_token(char *literal) {
    if (strcmp(literal, "null") == 0) {
        return (json_token){.kind = JSON_TOKEN_NULL, .value = NULL };
    } else if (strcmp(literal, "true") == 0 || strcmp(literal, "false") == 0) {
        return (json_token){.kind = JSON_TOKEN_BOOLEAN, .value = literal };
    } else {
        return (json_token){.kind = JSON_TOKEN_ILLEGAL, .value = literal };
    }
}

json_token json_lexer_next(json_lexer *lexer) {
    json_lexer_skip_whitespace(lexer);

    unsigned int position = lexer->pos;
    if (lexer->ch == EOF) {
        json_lexer_read(lexer);
        return (json_token){.kind = JSON_TOKEN_EOF, .value = NULL, .pos = position };
    } else if (lexer->ch == '{') {
        json_lexer_read(lexer);
        return (json_token){.kind = JSON_TOKEN_LSQRLY, .value = NULL, .pos = position };
    } else if (lexer->ch == '}') {
        json_lexer_read(lexer);
        return (json_token){.kind = JSON_TOKEN_RSQRLY, .value = NULL, .pos = position };
    } else if (lexer->ch == '[') {
        json_lexer_read(lexer);
        return (json_token){.kind = JSON_TOKEN_LBRACE, .value = NULL, .pos = position };
    } else if (lexer->ch == ']') {
        json_lexer_read(lexer);
        return (json_token){.kind = JSON_TOKEN_RBRACE, .value = NULL, .pos = position };
    } else if (lexer->ch == ':') {
        json_lexer_read(lexer);
        return (json_token){.kind = JSON_TOKEN_COLON, .value = NULL, .pos = position };
    } else if (lexer->ch == ',') {
        json_lexer_read(lexer);
        return (json_token){.kind = JSON_TOKEN_COMMA, .value = NULL, .pos = position };
    } else if (islower(lexer->ch)) {
        char *value = NULL;
        ds_string_slice slice = { .str = (char *)lexer->buffer + lexer->pos, .len = 0 };
        while (islower(lexer->ch)) {
            slice.len += 1;
            json_lexer_read(lexer);
        }
        // TODO: can fail
        ds_string_slice_to_owned(&slice, &value);

        json_token t = json_lexer_literal_to_token(value);
        t.pos = position;

        return t;
    } else {
        char *value = NULL;
        ds_string_slice slice = { .str = (char *)lexer->buffer + lexer->pos, .len = 1 };
        // TODO: can fail
        ds_string_slice_to_owned(&slice, &value);
        json_lexer_read(lexer);
        return (json_token){.kind = JSON_TOKEN_ILLEGAL, .value = value, .pos = position };
    }
}

void json_lexer_free(json_lexer *lexer) {
    lexer->buffer = NULL;
    lexer->buffer_len = 0;
    lexer->pos = 0;
    lexer->read_pos = 0;
    lexer->ch = 0;
}

int main(int argc, char **argv) {
    char *filename = NULL;
    char *buffer = NULL;
    int buffer_len;
    json_lexer lexer = {0};
    json_token token = {0};
    int result = 0;
    ds_argparse_parser parser = {0};
    ds_argparse_parser_init(&parser, "json-c", "json parser in c" , "0.1");

    if (ds_argparse_add_argument(&parser, (ds_argparse_options){
        .short_name = 'i',
        .long_name = "input",
        .description = "the input file",
        .type = ARGUMENT_TYPE_POSITIONAL,
        .required = 0,
    }) != 0) {
        DS_LOG_ERROR("Could not add argument `input`");
        return_defer(1);
    }

    if (ds_argparse_parse(&parser, argc, argv) != 0) {
        DS_LOG_ERROR("Could not parse arguments");
        return_defer(1);
    }

    filename = ds_argparse_get_value(&parser, "input");
    buffer_len = ds_io_read_file(filename, &buffer);
    if (buffer_len < 0) {
        DS_LOG_ERROR("Could not read from file: %s", (filename == NULL) ? "stdin" : filename);
        return_defer(-1);
    }

    json_lexer_init(&lexer, buffer, buffer_len);
    do {
        token = json_lexer_next(&lexer);
        switch (token.kind) {
        case JSON_TOKEN_LBRACE: printf("T[\n");break;
        case JSON_TOKEN_RBRACE:printf("T]\n");break;
        case JSON_TOKEN_LSQRLY:printf("T{\n");break;
        case JSON_TOKEN_RSQRLY:printf("T}\n");break;
        case JSON_TOKEN_COLON:printf("T:\n");break;
        case JSON_TOKEN_COMMA:printf("T,\n");break;
        case JSON_TOKEN_BOOLEAN:printf("Tboolean: %s\n", token.value);break;
        case JSON_TOKEN_NUMBER:printf("Tnumber\n");break;
        case JSON_TOKEN_STRING:printf("Tstring\n");break;
        case JSON_TOKEN_NULL:printf("Tnull\n");break;
        case JSON_TOKEN_EOF:printf("Teof\n");break;
        case JSON_TOKEN_ILLEGAL:printf("Tillegal: %s\n", token.value);break;
          break;
        }
    } while (token.kind != JSON_TOKEN_EOF);

defer:
    json_lexer_free(&lexer);
    if (buffer != NULL) {
        DS_FREE(NULL, buffer);
    }
    ds_argparse_parser_free(&parser);
    return result;
}
