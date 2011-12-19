
/*
 * Copyright (C) 2011 Daniel Dabbelt
 *   <palmem@comcast.net>
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

#ifndef PCONFIGURE_CONTEXT_H
#define PCONFIGURE_CONTEXT_H

struct context;

#include "clopts.h"
#include "language.h"
#include "languagelist.h"
#include "makefile.h"

/* Contexts can have multiple types */
enum context_type
{
    CONTEXT_TYPE_NONE = 0,
    CONTEXT_TYPE_DEFAULTS,
    CONTEXT_TYPE_BINARY,
    CONTEXT_TYPE_SOURCE
};

struct context
{
    enum context_type type;

    /* When a source is used, this in the context it will check in order to
     * ensure that dependencies are not added twice. */
    struct context *parent;

    /* These should be the same globally */
    struct makefile *mf;
    struct languagelist *ll;

    /* Each context has a language associated with it, the reason for this is
     * to assure that all sources are compatible with one another. */
    struct language *language;

    /* This is the full path (include the bin/ or src/) of this context */
    const char *full_path;

    /* These paths can be changed on a per context basis */
    const char *bin_dir;
    const char *obj_dir;
    const char *src_dir;
    const char *prefix;

    /* Options specific to this context */
    struct stringlist *compile_opts;
    struct stringlist *link_opts;
};

extern struct context *context_new_defaults(struct clopts *o, void *context,
                                            struct makefile *mf,
                                            struct languagelist *ll);

extern struct context *context_new_binary(struct context *parent,
                                          void *context,
                                          const char *called_path);
extern struct context *context_new_source(struct context *parent,
                                          void *context,
                                          const char *called_path);

extern int context_set_prefix(struct context *c, const char *opt);

extern int context_add_compileopt(struct context *c, const char *opt);
extern int context_add_linkopt(struct context *c, const char *opt);

#endif