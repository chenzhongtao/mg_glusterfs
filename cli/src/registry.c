/*
   Copyright (c) 2010-2012 Red Hat, Inc. <http://www.redhat.com>
   This file is part of GlusterFS.

   This file is licensed to you under your choice of the GNU Lesser
   General Public License, version 3 or any later version (LGPLv3 or
   later), or the GNU General Public License, version 2 (GPLv2), in all
   cases as published by the Free Software Foundation.
*/

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cli.h"
#include "cli-cmd.h"


static int
__is_spc (int ch)
{
        if (ch == ' ')
                return 1;
        return 0;
}


static int
__is_div (int ch)
{
        switch (ch) {
        case '(':
        case ')':
        case '<':
        case '>':
        case '[':
        case ']':
        case '{':
        case '}':
        case '|':
                return 1;
        }

        return 0;
}


static int
__is_word (const char *word)
{
        return (!__is_div (*word) && !__is_spc (*word));
}


int
counter_char (int ch)
{
        switch (ch) {
        case '(':
                return ')';
        case '<':
                return '>';
        case '[':
                return ']';
        case '{':
                return '}';
        }

        return -1;
}


const char *
__is_template_balanced (const char *template)
{
        const char *trav = NULL;
        int   ch = 0;

        trav = template;

        while (*trav) {
                ch = *trav;

                switch (ch) {
                case '<':
                case '(':
                case '[':
                        trav = __is_template_balanced (trav+1);
                        if (!trav)
                                return NULL;
                        if (*trav != counter_char (ch))
                                return NULL;
                        break;
                case '>':
                case ')':
                case ']':
                        return trav;
                }

                trav++;
        }

        return trav;
}


int
is_template_balanced (const char *template)
{
        const char *trav = NULL;

        trav = __is_template_balanced (template);
        if (!trav || *trav)
                return -1;

        return 0;
}


int
cli_cmd_token_count (const char *template)
{
        int         count = 0;
        const char *trav = NULL;
        int         is_alnum = 0;

        for (trav = template; *trav; trav++) {
                switch (*trav) {
                case '<':
                case '>':
                case '(':
                case ')':
                case '[':
                case ']':
                case '{':
                case '}':
                case '|':
                        count++;
                        /* fall through */
                case ' ':
                        is_alnum = 0;
                        break;
                default:
                        if (!is_alnum) {
                                is_alnum = 1;
                                count++;
                        }
                }
        }

        return count + 1;
}


void
cli_cmd_tokens_destroy (char **tokens)
{
        char **tokenp = NULL;

        if (!tokens)
                return;

        tokenp = tokens;
        while (*tokenp) {
                free (*tokenp);
                tokenp++;
        }

        free (tokens);
}


int
cli_cmd_tokens_fill (char **tokens, const char *template)
{
        const char  *trav = NULL;
        char       **tokenp = NULL;
        char        *token = NULL;
        int          ret = 0;
        int          ch = 0;

        tokenp = tokens;

        for (trav = template; *trav; trav++) {
                ch = *trav;

                if (__is_spc (ch))
                        continue;

                if (__is_div (ch)) {
                        token = calloc (2, 1);
                        if (!token)
                                return -1;
                        token[0] = ch;

                        *tokenp = token;
                        tokenp++;

                        continue;
                }

                token = strdup (trav);
                *tokenp = token;
                tokenp++;

                for (token++; *token; token++) {
                        if (__is_spc (*token) || __is_div (*token)) {
                                *token = 0;
                                break;
                        }
                        trav++;
                }
        }

        return ret;
}


char **
cli_cmd_tokenize (const char *template)
{
        char **tokens = NULL;
        int    ret = 0;
        int    count = 0;

        //判断{},(),<>是否成对存在
        ret = is_template_balanced (template);
        if (ret)
                return NULL;
        //template "volume create <NEW-VOLNAME> [stripe <COUNT>] [replica <COUNT>] [disperse [<COUNT>]] [redundancy <COUNT>] [transport <tcp|rdma|tcp,rdma>] <NEW-BRICK>... [force]"
        //count 49
        count = cli_cmd_token_count (template);
        if (count <= 0)
                return NULL;

        tokens = calloc (count + 1, sizeof (char *));
        if (!tokens)
                return NULL;
        //tokens
        /* display *tokens@48                                                                                                                                                                   
         *tokens@48 = {0x68a410 "volume", 0x68aca0 "create", 0x68a9c0 "<", 0x68a920 "NEW-VOLNAME", 0x689380 ">", 0x68a350 "[", 
          0x68ad50 "stripe", 0x68a370 "<", 0x68ade0 "COUNT", 0x68a390 ">", 0x68ae70 "]", 0x68ae90 "[", 0x68a7a0 "replica", 0x68aeb0 "<", 
          0x68aed0 "COUNT", 0x68af50 ">", 0x68af70 "]", 0x68af90 "[", 0x68afb0 "disperse", 0x68b020 "[", 0x68b040 "<", 0x68b060 "COUNT", 
          0x68b0c0 ">", 0x68b0e0 "]", 0x68b100 "]", 0x68b120 "[", 0x68b140 "redundancy", 0x68b1a0 "<", 0x68b1c0 "COUNT", 0x68b210 ">", 
          0x68b230 "]", 0x68b250 "[", 0x68b270 "transport", 0x68b2b0 "<", 0x68b2d0 "tcp", 0x68b310 "|", 0x68b330 "rdma", 0x68b360 "|", 
          0x68b380 "tcp,rdma", 0x68b3b0 ">", 0x68b3d0 "]", 0x68b3f0 "<", 0x68b410 "NEW-BRICK", 0x68b430 ">", 0x68b450 "...", 0x68b470 "[", 
          0x68b490 "force", 0x68b4b0 "]"}*/
        ret = cli_cmd_tokens_fill (tokens, template);
        if (ret)
                goto err;

        return tokens;
err:
        cli_cmd_tokens_destroy (tokens);
        return NULL;
}

void *
cli_getunamb (const char *tok, void **choices, cli_selector_t sel)
{
        void  **wcon = NULL;
        char      *w = NULL;
        unsigned  mn = 0;
        void    *ret = NULL;

        if (!choices || !tok || !*tok)
                return NULL;

        for (wcon = choices; *wcon; wcon++) {
                w = strtail ((char *)sel (*wcon), tok);
                if (!w)
                        /* no match */
                        continue;
                if (!*w)
                        /* exact match */
                        return *wcon;

                ret = *wcon;
                mn++;
        }

#ifdef FORCE_MATCH_EXACT
        return NULL;
#else
        return (mn == 1) ? ret : NULL;
#endif
}

static const char *
sel_cmd_word (void *wcon)
{
        return ((struct cli_cmd_word *)wcon)->word;
}

struct cli_cmd_word *
cli_cmd_nextword (struct cli_cmd_word *word, const char *token)
{
        return (struct cli_cmd_word *)cli_getunamb (token,
                                                    (void **)word->nextwords,
                                                    sel_cmd_word);
}


struct cli_cmd_word *
cli_cmd_newword (struct cli_cmd_word *word, const char *token)
{
        struct cli_cmd_word **nextwords = NULL;
        struct cli_cmd_word  *nextword = NULL;

        nextwords = realloc (word->nextwords,
                             (word->nextwords_cnt + 2) * sizeof (*nextwords));
        if (!nextwords)
                return NULL;

        word->nextwords = nextwords;

        nextword = calloc (1, sizeof (*nextword));
        if (!nextword)
                return NULL;

        nextword->word = strdup (token);
        if (!nextword->word) {
                free (nextword);
                return NULL;
        }

        nextword->tree = word->tree;
        nextwords[word->nextwords_cnt++] = nextword;
        nextwords[word->nextwords_cnt] = NULL;

        return nextword;
}


int
cli_cmd_ingest (struct cli_cmd_tree *tree, char **tokens, cli_cmd_cbk_t *cbkfn,
                const char *desc, const char *pattern)
{
        int                    ret = 0;
        char                 **tokenp = NULL;
        char                  *token = NULL;
        struct cli_cmd_word   *word = NULL;
        struct cli_cmd_word   *next = NULL;

        word = &tree->root;

        for (tokenp = tokens; (token = *tokenp); tokenp++) {
                if (!__is_word (token))
                        break;

                next = cli_cmd_nextword (word, token);
                if (!next)
                        next = cli_cmd_newword (word, token);

                word = next;
                if (!word)
                        break;
        }

        if (!word)
                return -1;

        if (word->cbkfn) {
                /* warning - command already registered */
        }

        word->cbkfn = cbkfn;
        word->desc  = desc;
        word->pattern = pattern;

        /* end of static strings in command template */

        /* TODO: autocompletion beyond this point is just "nice to have" */

        return ret;
}


int
cli_cmd_register (struct cli_cmd_tree *tree, struct cli_cmd *cmd)
{
        char **tokens = NULL;
        int    ret = 0;

        GF_ASSERT (cmd);

        if (cmd->reg_cbk)
                cmd->reg_cbk (cmd);

        if (cmd->disable) {
                ret = 0;
                goto out;
        }

        //pattern 转tokens
        tokens = cli_cmd_tokenize (cmd->pattern);
        if (!tokens) {
                ret = -1;
                goto out;
        }



        /*
        (gdb) print tree.root.tree.root.nextwords[0].word                                                                              
        $98 = 0x68aa50 "volume"
        (gdb) print tree.root.tree.root.nextwords[1].word                                                                              
        $99 = 0x68bc10 "peer"
        (gdb) print tree.root.tree.root.nextwords[0].nextwords[0].word                                                                
        $100 = 0x68aae0 "info"
        (gdb) print tree.root.tree.root.nextwords[0].nextwords[1].word                                                                 
        $101 = 0x68b520 "create"*/
        ret = cli_cmd_ingest (tree, tokens, cmd->cbk, cmd->desc, cmd->pattern);
        if (ret) {
                ret = -1;
                goto out;
        }

        ret = 0;

out:
        if (tokens)
                cli_cmd_tokens_destroy (tokens);

        gf_log ("cli", GF_LOG_DEBUG, "Returning %d", ret);
        return ret;
}

