/* vi:set ts=4 sw=4 expandtab:
 *
 * Copyright 2016, Chris Leishman (http://github.com/cleishm)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "../../config.h"
#include "cypher-parser.h"
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>


const char *shortopts = "ahv";

#define COLORIZE_OPT 1004
#define OUTPUT_WIDTH_OPT 1005
#define VERSION_OPT 1006

static struct option longopts[] =
    { { "ast", no_argument, NULL, 'a' },
      { "colorize", no_argument, NULL, COLORIZE_OPT },
      { "help", no_argument, NULL, 'h' },
      { "output-width", required_argument, NULL, OUTPUT_WIDTH_OPT },
      { "version", no_argument, NULL, VERSION_OPT },
      { NULL, 0, NULL, 0 } };

static void usage(FILE *s, const char *prog_name)
{
    fprintf(s,
"usage: %s [OPTIONS]\n"
"options:\n"
" --ast, -a           Dump the AST to stdout.\n"
" --colorize          Colorize output using ANSI escape sequences.\n"
" --help, -h          Output this usage information.\n"
" --output-width <n>  Attempt to limit output to the specified width.\n"
" --version           Output the version of cypher-lint and libcypher-parser\n"
"\n",
        prog_name);
}


struct lint_config
{
    unsigned int width;
    bool dump_ast;
    bool colorize_output;
    bool colorize_errors;
};


static int process(FILE *stream, struct lint_config *config);


int main(int argc, char *argv[])
{
    char *prog_name = basename(argv[0]);
    if (prog_name == NULL)
    {
        perror("unexpected error");
        exit(EXIT_FAILURE);
    }

    struct lint_config config;
    memset(&config, 0, sizeof(config));

    if (isatty(fileno(stdout)))
    {
        config.colorize_output = true;
    }
    if (isatty(fileno(stderr)))
    {
        config.colorize_errors = true;
    }

    int result = EXIT_FAILURE;

    int c;
    while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) >= 0)
    {
        switch (c)
        {
        case 'a':
            config.dump_ast = true;
            break;
        case COLORIZE_OPT:
            config.colorize_output = true;
            config.colorize_errors = true;
            break;
        case 'h':
            usage(stdout, prog_name);
            result = EXIT_SUCCESS;
            goto cleanup;
        case OUTPUT_WIDTH_OPT:
            config.width = atoi(optarg);
            break;
        case VERSION_OPT:
            fprintf(stdout, "neo4j-lint: %s\n", PACKAGE_VERSION);
            fprintf(stdout, "libcypher-parser: %s\n",
                    libcypher_parser_version());
            result = EXIT_SUCCESS;
            goto cleanup;
        default:
            usage(stderr, prog_name);
            goto cleanup;
        }
    }
    argc -= optind;
    argv += optind;

    if (process(stdin, &config))
    {
        goto cleanup;
    }

    result = EXIT_SUCCESS;

cleanup:
    return result;
}


int process(FILE *stream, struct lint_config *config)
{
    cypher_parser_config_t *cp_config = cypher_parser_new_config();
    if (cp_config == NULL)
    {
        return -1;
    }

    const struct cypher_parser_colorization *error_colorization =
        cypher_parser_no_colorization;
    if (config->colorize_errors)
    {
        error_colorization = cypher_parser_ansi_colorization;
        cypher_parser_config_set_error_colorization(cp_config,
                error_colorization);
    }

    int err = -1;

    cypher_parse_result_t *result = cypher_fparse(stream, NULL, cp_config, 0);
    if (result == NULL)
    {
        // TODO: report error
        perror("cypher_fparse");
        goto cleanup;
    }

    const cypher_parse_error_t *error;
    for (unsigned int i = 0;
            (error = cypher_parse_result_error(result, i)) != NULL; ++i)
    {
        struct cypher_input_position pos =
                cypher_parse_error_position(error);
        const char *msg = cypher_parse_error_message(error);
        const char *context = cypher_parse_error_context(error);
        unsigned int offset = cypher_parse_error_context_offset(error);
        fprintf(stderr, "%s %s(line %u, column %u, offset %zu)%s%s\n", msg,
                error_colorization->error_message[0],
                pos.line, pos.column, pos.offset,
                error_colorization->error_message[1],
                (context == NULL)? "" : ":");
        fprintf(stderr, "%s\n%*.*s^\n", context, offset, offset, " ");
    }

    const struct cypher_parser_colorization *output_colorization =
        config->colorize_output? cypher_parser_ansi_colorization : NULL;
    if (config->dump_ast && cypher_parse_result_fprint(result, stdout,
            config->width, output_colorization, 0) < 0)
    {
        perror("cypher_parse_result_fprint");
        goto cleanup;
    }

    err = (cypher_parse_result_ndirectives(result) > 0)? 0 : 1;

    int errsv;
cleanup:
    errsv = errno;
    cypher_parser_config_free(cp_config);
    cypher_parse_result_free(result);
    errno = errsv;
    return err;
}