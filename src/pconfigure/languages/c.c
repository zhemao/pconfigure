#include "c.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <clang-c/Index.h>

/* Helper functions (not specific to one of the functions below) */
static char *source2object(const char *source, struct context *c)
{
    char *hash;                 /* A hash of the compile options for this obj */
    char *object;
    const char *cur;            /* The character to process, in the input */
    char *dest;                 /* The current place to write to in the output */
    char *start;                /* Make sure not to backup past here */
    int count;

    hash = "hash--code";

    object = malloc(strlen(c->obj_dir)  /* Starts with the object directory */
                    + strlen(source) * 2        /* Potentially double ("/" to "__") */
                    + 1         /* Add one for the / after the obj_dir */
                    + 1         /* Add one for the '\0' at the end */
                    + 2         /* Add 2 for the "__" before the hash */
                    + 2         /* Add 2 for the ".o" at the end */
                    + strlen(hash)      /* Include the hash */
        );
    assert(object != NULL);
    object[0] = '\0';

    strcat(object, c->obj_dir);
    strcat(object, "/");
    start = object + strlen(object);

    /* Converts /'s into __'s */
    count = 0;
    dest = start;
    for (cur = source; *cur != '\0'; cur++)
    {
        switch (*cur)
        {
        case '/':
            *dest = '_';
            dest++;
            *dest = '_';
            break;
        default:
            *dest = *cur;
        }

        dest++;
        *dest = '\0';
    }

    /* Tags this object with the hash code for the compile opts */
    strcat(object, "__");
    strcat(object, hash);
    strcat(object, ".o");

    printf("object: '%s'\n", object);

    return object;
}

/* Polymorphic functions */
static int lf_match(struct language *lang, const char *filename)
{
    return 1;
}

/* This helper writes every included file out to the makefile as a dependency */
struct adddeps_help_iv_mfputs_cd
{
    struct makefile *mf;
};
static void lf_adddeps_help_iv_mfputs(CXFile included_file,
                                      CXSourceLocation * inclusion_stack,
                                      unsigned include_len,
                                      CXClientData client_data)
{
    CXString filename;
    const char *filename_cstr;
    struct adddeps_help_iv_mfputs_cd *args;

    args = (struct adddeps_help_iv_mfputs_cd *)client_data;
    assert(args != NULL);

    filename = clang_getFileName(included_file);

    filename_cstr = clang_getCString(filename);
    makefile_add_dep(args->mf, filename_cstr);
    clang_disposeString(filename);
    filename_cstr = NULL;
}

static int lf_adddeps(struct language *lang, struct target *src,
                      struct makefile *mf, struct context *c)
{
    int clang_argc;
    char **clang_argv;
    CXIndex index;
    CXTranslationUnit tu;
    struct adddeps_help_iv_mfputs_cd mfputs_args;
    char *objfile;

    assert(lang != NULL);
    assert(src != NULL);
    assert(mf != NULL);

    printf("clang parsing '%s'\n", src->source);

    /* Figures out the object name for this input source */
    objfile = source2object(src->source, c);

    /* If the file already exists, then skip the rest of this parsing, just
     * add it to the list of files to build */
    string_list_addifnew(src->parent->deps, objfile);
    if (string_list_addifnew(mf->targets, objfile) == 1)
    {
        printf("duplicate found\n");
        return 0;
    }

    /* Adds this object to the source */
    makefile_add_target(mf, objfile);

    /* TODO: make libclang listen to all the compileopts, not just the 
     * filename of the input file */
    clang_argc = 1;
    clang_argv = malloc(sizeof(char *));
    assert(clang_argv != NULL);
    clang_argv[0] = strdup(src->source);

    /* Starts at the beginning of the input file, and checks every included
     * file for matches */
    index = clang_createIndex(0, 0);
    tu = clang_parseTranslationUnit(index, 0, (const char *const *)clang_argv,
                                    clang_argc, 0, 0, CXTranslationUnit_None);

    /* Adds every file included by this file into the makefile */
    mfputs_args.mf = mf;
    clang_getInclusions(tu, &lf_adddeps_help_iv_mfputs, &mfputs_args);

    /* Ends the list of dependencies of this file */
    makefile_end_deps(mf);

    /* Makes a new dependency */
    makefile_start_cmd(mf);
    fprintf(makefile_cmd_fd(mf),
            "@echo \"CC\t%s\" ; mkdir -p `dirname \"%s\"` ; %s ",
            src->source, objfile, src->lang->compiler);
    string_list_fserialize(c->compile_opts, makefile_cmd_fd(mf), " ");
    fprintf(makefile_cmd_fd(mf), " -c \"%s\" -o \"%s\"",
            src->source, objfile);
    makefile_end_cmd(mf);

    /* Cleanup code for libclang */
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);

    free(clang_argv[0]);
    free(clang_argv);

    /* Removes the extra copy of the object file that is lying around */
    free(objfile);

    return 0;
}

struct language *language_c_boot(void)
{
    struct language_c *out;

    out = malloc(sizeof(*out));
    if (out == NULL)
        return NULL;
    language_init((struct language *)out);

    /* TODO: change this to "c", here for compatibility */
    out->lang.name = strdup("gcc");
    out->lang.compiler = strdup("gcc");
    out->lang.linker = strdup("gcc");

    out->lang.match = &lf_match;
    out->lang.adddeps = &lf_adddeps;

    return (struct language *)out;
}