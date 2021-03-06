
/*
 * Copyright (C) 2013 Palmer Dabbelt
 *   <palmer@dabbelt.com>
 *
 * This file is part of pconfigure.
 * 
 * pconfigure is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * pconfigure is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with pconfigure.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _XOPEN_SOURCE 500

#include "chisel.h"
#include "ftwfn.h"
#include "funcs.h"
#include "../languagelist.h"
#include <ftw.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_TALLOC
#include <talloc.h>
#else
#include "extern/talloc.h"
#endif

#ifdef HAVE_CLANG
#include <clang-c/Index.h>
#else
#include "extern/clang.h"
#endif

/* FIXME: This allows me to access the global langauge list, it's a
 * huge hack! */
extern struct languagelist *ll;

/* Member functions. */
static struct language *language_chisel_search(struct language *l_uncast,
                                               struct language *parent,
                                               const char *path,
                                               struct context *c);
static const char *language_chisel_objname(struct language *l_uncast,
                                           void *context, struct context *c);
static void language_chisel_deps(struct language *l_uncast, struct context *c,
                                 void (*func) (void *, const char *, ...),
                                 void *arg);
static void language_chisel_build(struct language *l_uncast,
                                  struct context *c, void (*func) (bool,
                                                                   const char
                                                                   *, ...));
static void language_chisel_link(struct language *l_uncast, struct context *c,
                                 void (*func) (bool, const char *, ...),
                                 bool should_install);
static void language_chisel_slib(struct language *l_uncast, struct context *c,
                                 void (*func) (bool, const char *, ...));
static void language_chisel_extras(struct language *l_uncast,
                                   struct context *c, void *context,
                                   void (*func) (void *, const char *),
                                   void *arg);
static void language_chisel_quirks(struct language *l_uncast,
                                   struct context *c, struct makefile *mf);

/* Adds every file to the dependency list. */
struct ftw_args
{
    struct language_chisel *l;
    void *ctx;
    void (*func) (void *, const char *, ...);
    void *arg;
};

static int ftw_add(const char *fpath, const struct stat *sb,
                   int typeflag, struct FTW *ftwbuf, void *arg);

/* Checks if a given library is a "*.so" and eats it, otherwise passes
 * it along to the given function. */
struct eat_so_args
{
    void (*func) (bool, const char *, ...);
};
static int eat_so(const char *lib, void *args_uncast);

/* I think there probably should be a cleaner way to do this, but I'm
 * not entirely sure how to go about it. */
struct jarlib_args
{
    struct context *c;
    void (*func) (void *, const char *, ...);
    void *arg;
};
static int jarlib(const char *lib, void *args_uncast);

struct language *language_chisel_new(struct clopts *o, const char *name)
{
    struct language_chisel *l;

    if (strcmp(name, "chisel") != 0)
        return NULL;

    l = talloc(o, struct language_chisel);
    if (l == NULL)
        return NULL;

    language_init(&(l->l));
    l->l.name = talloc_strdup(l, "chisel");
    l->l.link_name = talloc_strdup(l, "c");
    l->l.compile_str = talloc_strdup(l, "ChC");
    l->l.compile_cmd = talloc_strdup(l, "${CXX}");
    l->l.link_str = talloc_strdup(l, "ChLD");
    l->l.link_cmd = talloc_strdup(l, "${CXX}");
    l->l.so_ext = talloc_strdup(l, "so");
    l->l.so_ext_canon = talloc_strdup(l, "so");
    l->l.a_ext = talloc_strdup(l, "a");
    l->l.a_ext_canon = talloc_strdup(l, "a");
    l->l.compiled = true;
    l->l.search = &language_chisel_search;
    l->l.objname = &language_chisel_objname;
    l->l.deps = &language_chisel_deps;
    l->l.build = &language_chisel_build;
    l->l.link = &language_chisel_link;
    l->l.slib = &language_chisel_slib;
    l->l.extras = &language_chisel_extras;
    l->l.quirks = &language_chisel_quirks;

    return &(l->l);
}

struct language *language_chisel_search(struct language *l_uncast,
                                        struct language *parent,
                                        const char *path, struct context *c)
{
    struct language_chisel *l;

    if (c == NULL)
        return NULL;

    if (c->type != CONTEXT_TYPE_SOURCE)
        return NULL;

    if (c->parent == NULL)
        return NULL;

    /* Chisel doesn't build libraries whose names end in .jar, those
     * are scala libraries. */
    if (strcmp(c->parent->full_path + strlen(c->parent->full_path) - 4,
               ".jar") == 0)
        return NULL;

    /* Chisel doesn't build binaries whose names end in .flo, those
     * are DREAMER scripts. */
    if (strcmp(c->parent->full_path + strlen(c->parent->full_path) - 4,
               ".flo") == 0)
        return NULL;

    l = talloc_get_type(l_uncast, struct language_chisel);
    if (l == NULL)
        return NULL;

    if (strlen(path) <= 6)
        return NULL;

    if (strcmp(path + strlen(path) - 6, ".scala") != 0)
        return NULL;

    if (parent == NULL)
        return l_uncast;

    if (strcmp(parent->link_name, l_uncast->link_name) != 0)
        return NULL;

    return l_uncast;
}

const char *language_chisel_objname(struct language *l_uncast, void *context,
                                    struct context *c)
{
    char *o;
    const char *compileopts_hash, *langopts_hash;
    struct language_chisel *l;
    void *subcontext;

    l = talloc_get_type(l_uncast, struct language_chisel);
    if (l == NULL)
        return NULL;

    subcontext = talloc_new(NULL);
    compileopts_hash = stringlist_hashcode(c->compile_opts, subcontext);
    langopts_hash = stringlist_hashcode(l->l.compile_opts, subcontext);

    /* This should be checked higher up in the stack, but just make sure */
    assert(c->full_path[strlen(c->src_dir)] == '/');

    o = talloc_asprintf(context, "%s/%s/%s-%s-%s-chisel_c.o",
                        c->obj_dir,
                        c->full_path, compileopts_hash, langopts_hash,
                        c->shared_target ? "shared" : "static");

    TALLOC_FREE(subcontext);
    return o;
}

void language_chisel_deps(struct language *l_uncast, struct context *c,
                          void (*func) (void *, const char *, ...), void *arg)
{
    void *ctx;
    struct language_chisel *l;
    const char *dir;

    l = talloc_get_type(l_uncast, struct language_chisel);
    if (l == NULL)
        abort();

    ctx = talloc_new(l);

    dir = c->full_path + strlen(c->full_path) - 1;
    while (*dir != '/' && dir != c->full_path)
        dir--;

    dir = talloc_strndup(ctx, c->full_path, dir - c->full_path);

    /* FIXME: I can't properly handle Scala dependencies, so I just
     * build every Scala file in the same directory as the current
     * Scala file.  This is probably very bad... */
    {
        struct ftw_args fa;
        fa.l = l;
        fa.ctx = ctx;
        fa.func = func;
        fa.arg = arg;
        aftw(dir, &ftw_add, 16, FTW_DEPTH, &fa);
    }

    {
        struct jarlib_args jla;
        jla.func = func;
        jla.c = c;
        jla.arg = arg;
        stringlist_each(c->libraries, &jarlib, &jla);
    }

    TALLOC_FREE(ctx);

    return;
}

void language_chisel_build(struct language *l_uncast, struct context *c,
                           void (*func) (bool, const char *, ...))
{
    struct language_chisel *l;
    struct language *cxx_lang;
    void *context;
    const char *obj_path;
    const char *compile_str;
    char *compile_opt;
    char *design;
    const char *last_source;

    l = talloc_get_type(l_uncast, struct language_chisel);
    if (l == NULL)
        return;

    context = talloc_new(NULL);
    obj_path = language_objname(l_uncast, context, c);

    compile_str = l->l.compile_str;

    func(true, "echo -e \"%s\\t%s\"",
         compile_str, c->full_path + strlen(c->src_dir) + 1);

    func(false, "mkdir -p %s.d/gen >& /dev/null || true", obj_path);
    func(false, "mkdir -p %s.d/inc >& /dev/null || true", obj_path);

    /* Search for the design name, which is necessary because we can't
     * know this for a while. */
    design = NULL;

    {
        const char *found;
        found = stringlist_search_start(l->l.compile_opts, "-d", context);

        if (found != NULL)
            design = talloc_strdup(context, found + 2);
    }

    {
        const char *found;
        found = stringlist_search_start(c->compile_opts, "-d", context);

        if (found != NULL)
            design = talloc_strdup(context, found + 2);
    }

    /* Compile the scala sources */
    func(false, "pscalac `ppkg-config chisel --libs`\\");
    func(false, "\\ -L %s", c->lib_dir);
    last_source = c->full_path;

    {
        struct eat_so_args esa;
        esa.func = func;
        stringlist_each(c->libraries, &eat_so, &esa);
    }

    if (last_source == NULL) {
        fprintf(stderr, "chisel compiler called with no sources!\n");
    }
    func(false, "\\ $$(find `dirname %s` -iname *.scala)", last_source);
    func(false, "\\ -o %s.d/obj.jar\n", obj_path);

    /* Add a flag that tells Scala how to start up */
    func(false, "pscalald `ppkg-config chisel --libs`\\");
    func(false, "\\ -L %s", c->lib_dir);
    func_stringlist_each_cmd_lcont(c->libraries, func);
    func(false, "\\ -o %s.d/obj.bin %s.d/obj.jar\n", obj_path, obj_path);

    /* Actually run the resulting Chisel binary to produce some C++ code */
    func(false, "%s.d/obj.bin --targetDir %s.d/gen >& /dev/null"
         " || %s.d/obj.bin --targetDir %s.d/gen",
         obj_path, obj_path, obj_path, obj_path);

    /* We then need to copy the header file out.  This needs to be in
     * a different directory because otherwise the C++ dependency
     * tracker will pick it up. */
    func(false, "cp %s.d/gen/%s.h %s.d/inc/%s.h",
         obj_path, design, obj_path, design);

    /* Actually build the C++ code into a binary! */
    func(false, "%s\\", l->l.compile_cmd);
    func_stringlist_each_cmd_cont_nostart(l->l.compile_opts, func, "-d");
    func_stringlist_each_cmd_cont_nostart(c->compile_opts, func, "-d");
    func(false, "\\ -I%s", c->hdr_dir);
    if (c->shared_target)
        func(false, "\\ -fPIC");
    func(false, "\\ `ppkg-config chisel --cflags`");
    func(false, "\\ -c %s.d/gen/%s.cpp -o %s\n", obj_path, design, obj_path);

    /* In order to allow C++ code to build, I have to manually go
     * munge the include path inside C++.  This is super-nasty, but I
     * guess it's as good as it's going to get... :( */
    cxx_lang = languagelist_search(ll, l_uncast, "somefile.c++", NULL);
    if (cxx_lang == NULL)
        abort();
    compile_opt = talloc_asprintf(context, "-I%s.d/inc", obj_path);
    language_add_compileopt(cxx_lang, compile_opt);

    /* Additionally, it's necessary to make a dummy C++ file NOW.
     * This is necessary because otherwise GCC won't pick up the
     * dependencies. */
    {
        char *make_dummy;

        make_dummy = talloc_asprintf(context, "mkdir -p %s.d/inc/", obj_path);
        if (system(make_dummy) != 0)
            abort();

        if (obj_path == NULL) {
            fprintf(stderr, "Called chisel object\n");
            abort();
        }
        if (design == NULL) {
            fprintf(stderr, "Called chisel without -d\n");
            abort();
        }

        make_dummy = talloc_asprintf(context,
                                     "if test ! -e %s.d/inc/%s.h; then touch -t 197101010101 %s.d/inc/%s.h; fi",
                                     obj_path, design, obj_path, design);
        if (system(make_dummy) != 0)
            abort();
    }

    TALLOC_FREE(context);
}

void language_chisel_link(struct language *l_uncast, struct context *c,
                          void (*func) (bool, const char *, ...),
                          bool should_install)
{
}

void language_chisel_slib(struct language *l_uncast, struct context *c,
                          void (*func) (bool, const char *, ...))
{
}

void language_chisel_extras(struct language *l_uncast, struct context *c,
                            void *context,
                            void (*func) (void *, const char *), void *arg)
{
    /* FIXME: Scala doesn't seem to support incremental compilation,
     * so we can't really handle the whole extras thing.  I just
     * depend on every chisel file, which is probably not the best. */
}

void language_chisel_quirks(struct language *l_uncast,
                            struct context *c, struct makefile *mf)
{
    struct language_chisel *l;
    void *context;
    const char *obj_path;
    char *design;
    const char *target_path;
    const char *source_path;

    l = talloc_get_type(l_uncast, struct language_chisel);
    if (l == NULL)
        return;

    context = talloc_new(NULL);
    obj_path = language_objname(l_uncast, context, c);

    /* Search for the design name, which is necessary because we can't
     * know this for a while. */
    design = NULL;

    {
        const char *found;
        found = stringlist_search_start(l->l.compile_opts, "-d", context);

        if (found != NULL)
            design = talloc_strdup(context, found + 2);
    }

    {
        const char *found;
        found = stringlist_search_start(c->compile_opts, "-d", context);

        if (found != NULL)
            design = talloc_strdup(context, found + 2);
    }

    target_path = talloc_asprintf(context, "%s.d/inc/%s.h", obj_path, design);
    source_path = talloc_asprintf(context, "%s.d/gen/%s.h", obj_path, design);

    /* We need a special test here because pconfigure won't do any
     * checking for quirks that have already been run. */
    if (stringlist_include(mf->targets, target_path))
        goto cleanup;

    if (stringlist_include(mf->targets, source_path))
        goto cleanup;

    /* Copy the generated header into the directory that's availiable
     * for C++ to use. */
    makefile_create_target(mf, target_path);

    makefile_start_deps(mf);
    makefile_add_dep(mf, source_path);
    makefile_end_deps(mf);

    makefile_start_cmds(mf);
    makefile_nam_cmd(mf, "echo -e \"ChQ\t%s [copy header]\"",
                     c->full_path + strlen(c->src_dir) + 1);
    makefile_add_cmd(mf, "cp %s %s", source_path, target_path);
    makefile_end_cmds(mf);

    /* This is just a dummy target: it tells make that building the
     * header depends on the chise object file.  Note that this is
     * kind of true: generating the object file generates the header,
     * but it's also a lie in that there's no extra work to be done
     * when generating the object to generate the header. */
    makefile_create_target(mf, source_path);

    makefile_start_deps(mf);
    makefile_add_dep(mf, obj_path);
    makefile_end_deps(mf);

    makefile_start_cmds(mf);
    makefile_nam_cmd(mf, "echo -e \"ChQ\t%s [gen header]\"",
                     c->full_path + strlen(c->src_dir) + 1);
    makefile_add_cmd(mf, "touch %s", source_path);
    makefile_end_cmds(mf);

  cleanup:
    TALLOC_FREE(context);
}

int ftw_add(const char *path, const struct stat *sb,
            int typeflag, struct FTW *ftwbuf, void *args_uncast)
{
    char *copy;
    void *ctx;
    void (*func) (void *, const char *, ...);
    void *arg;

    {
        struct ftw_args *args;
        args = args_uncast;
        ctx = args->ctx;
        func = args->func;
        arg = args->arg;
    }

    if (strcmp(path + strlen(path) - 6, ".scala") == 0) {
        copy = talloc_strdup(ctx, path);
        func(arg, "%s", copy);
    }

    return 0;
}

int eat_so(const char *lib, void *args_uncast)
{
    struct eat_so_args *args;
    args = args_uncast;

    if (strcmp(lib + strlen(lib) - 3, ".so") == 0)
        return 0;

    args->func(false, "\\ -l %s", lib);
    return 0;
}

int jarlib(const char *lib, void *args_uncast)
{
    struct context *c;
    char *str;
    struct jarlib_args *args;
    void *arg;

    args = args_uncast;
    c = args->c;
    arg = args->arg;

    str = talloc_asprintf(NULL, "%s/lib%s.jar", c->lib_dir, lib);
    args->func(arg, "%s", str);

    TALLOC_FREE(str);
    return 0;
}
