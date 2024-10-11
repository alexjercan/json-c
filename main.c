#define DS_IO_IMPLEMENTATION
#define DS_AP_IMPLEMENTATION
#define DS_JS_IMPLEMENTATION
#include "ds.h"

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
    char *string = NULL;
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

    if (json_object_dump(&object, &string) != 0) {
        DS_LOG_ERROR("Failed to dump json");
        return_defer(1);
    }

    printf("%s", string);

defer:
    json_object_free(&object);
    if (buffer != NULL) {
        DS_FREE(NULL, buffer);
    }
    if (string != NULL) {
        DS_FREE(NULL, string);
    }
    return result;
}
