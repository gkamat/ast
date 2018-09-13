/***********************************************************************
 *                                                                      *
 *               This software is part of the ast package               *
 *          Copyright (c) 1999-2011 AT&T Intellectual Property          *
 *                      and is licensed under the                       *
 *                 Eclipse Public License, Version 1.0                  *
 *                    by AT&T Intellectual Property                     *
 *                                                                      *
 *                A copy of the License is available at                 *
 *          http://www.eclipse.org/org/documents/epl-v10.html           *
 *         (with md5 checksum b35adb5213ca9657e911e9befb180842)         *
 *                                                                      *
 *              Information and Software Systems Research               *
 *                            AT&T Research                             *
 *                           Florham Park NJ                            *
 *                                                                      *
 *               Glenn Fowler <glenn.s.fowler@gmail.com>                *
 *                                                                      *
 ***********************************************************************/
/*
 * tmscan(3) tmfmt(3) tester
 *
 * testdate [-c] [-v] < testre.dat
 *
 *	-c	catch signals and non-terminating tmscan()
 *	-v	list each test line
 *
 * see comments in testdate.dat for description of format
 */
#include "config_ast.h"  // IWYU pragma: keep

#include <ctype.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ast.h"
#include "sfio.h"
#include "tm.h"

static struct {
    int errors;
    int lineno;
    int sig;
    int signals;
    int warnings;
    jmp_buf gotcha;
} state;

static void normal(char *s) {
    unsigned char *u = (unsigned char *)s;
    int c;

    if (!u)
        sfprintf(sfstdout, "NIL");
    else if (!*u)
        sfprintf(sfstdout, "NULL");
    else {
        for (;;) {
            switch (c = *u++) {
                case 0:
                    return;
                case '\n':
                    sfprintf(sfstdout, "\\n");
                    break;
                case '\r':
                    sfprintf(sfstdout, "\\r");
                    break;
                case '\t':
                    sfprintf(sfstdout, "\\t");
                    break;
                default:
                    if (isprint(c))
                        sfputc(sfstdout, c);
                    else
                        sfprintf(sfstdout, "\\x%02x", c);
                    break;
            }
        }
    }
}

static void report(char *comment, char *str, char *pat, char *rem, int flags) {
    state.errors++;
    sfprintf(sfstdout, "%d:\t", state.lineno);
    if (str) {
        normal(str);
        if (pat) {
            sfprintf(sfstdout, " vs ");
            normal(pat);
        }
    }
    if (flags & TM_PEDANTIC) sfprintf(sfstdout, " PEDANTIC");
    if (state.sig) {
        sfprintf(sfstdout, " %s", strsignal(state.sig));
        state.sig = 0;
    }
    if (rem && *rem) {
        if (*rem == '\n') {
            if (comment) {
                sfprintf(sfstdout, " %s", comment);
                comment = 0;
            }
            sfprintf(sfstdout, "%s", rem);
        } else {
            sfprintf(sfstdout, " at ");
            normal(rem);
        }
    }
    if (comment) sfprintf(sfstdout, " %s", comment);
    sfprintf(sfstdout, "\n");
}

static void bad(char *comment, char *str, char *pat) {
    sfprintf(sfstdout, "bad test case ");
    report(comment, str, pat, NULL, 0);
    exit(1);
}

static int hex(int c) { return isdigit(c) ? (c - '0') : (c - (isupper(c) ? 'A' : 'a') + 10); }

static void escape(char *s) {
    char *t;

    for (t = s; *s; s++, t++) {
        *t = *s;
        if (*s != '\\') continue;
        switch (*++s) {
            case 0:
                *++t = 0;
                break;
            case 'n':
                *t = '\n';
                break;
            case 'r':
                *t = '\r';
                break;
            case 't':
                *t = '\t';
                break;
            case 'x':
                if (!isxdigit(s[1]) || !isxdigit(s[2])) bad("bad \\x\n", NULL, NULL);
                *t = hex(*++s) << 4;
                *t |= hex(*++s);
                break;
            default:
                s--;
                break;
        }
    }
}

static void sigunblock(int s) {
#ifdef SIG_SETMASK
    int op;
    sigset_t mask;

    sigemptyset(&mask);
    if (s) {
        sigaddset(&mask, s);
        op = SIG_UNBLOCK;
    } else
        op = SIG_SETMASK;
    sigprocmask(op, &mask, NULL);
#else
#ifdef sigmask
    sigsetmask(s ? (sigsetmask(0L) & ~sigmask(s)) : 0L);
#endif
#endif
}

static void gotcha(int sig) {
    signal(sig, gotcha);
    alarm(0);
    state.signals++;
    state.sig = sig;
    sigunblock(sig);
    longjmp(state.gotcha, 1);
}

int main(int argc, char **argv) {
    UNUSED(argc);
    int i;
    long flags;
    char *p;
    char *s;
    char *e;
    char *f;
    char *str;
    char *fmt;
    char *ans;
    char *field[6];
    time_t t_str;
    time_t t_now;
    time_t t_ans;

    int catch = 0;
    int testno = 0;
    int verbose = 0;

    sfprintf(sfstdout, "TEST	tmscan");
    while ((p = *++argv) && *p == '-')
        for (;;) {
            switch (*++p) {
                case 0:
                    break;
                case 'c':
                    catch = 1;
                    sfprintf(sfstdout, ", catch");
                    continue;
                case 'v':
                    verbose = 1;
                    sfprintf(sfstdout, ", verbose");
                    continue;
                default:
                    sfprintf(sfstdout, ", invalid option %c", *p);
                    continue;
            }
            break;
        }
    if (p) sfprintf(sfstdout, ", argument(s) ignored");
    sfprintf(sfstdout, "\n");
    if (catch) {
        signal(SIGALRM, gotcha);
        signal(SIGBUS, gotcha);
        signal(SIGSEGV, gotcha);
    }
    t_now = time(NULL);
    while ((p = sfgetr(sfstdin, '\n', 1))) {
        state.lineno++;

        /* parse: */

        if (*p == 0 || *p == '#') continue;
        if (*p == ':') {
            while (*++p == ' ') {
                ;  // empty body
            }
            sfprintf(sfstdout, "NOTE	%s\n", p);
            continue;
        }
        i = 0;
        field[i++] = p;
        for (;;) {
            switch (*p++) {
                case 0:
                    p--;
                    goto checkfield;
                case '\t':
                    *(p - 1) = 0;
                checkfield:
                    s = field[i - 1];
                    if (!strcmp(s, "NIL"))
                        field[i - 1] = 0;
                    else if (!strcmp(s, "NULL"))
                        *s = 0;
                    while (*p == '\t') p++;
                    if (!*p) break;
                    if (i >= elementsof(field)) bad("too many fields\n", NULL, NULL);
                    field[i++] = p;
                    /*FALLTHROUGH*/
                default:
                    continue;
            }
            break;
        }

        /* interpret: */

        if (i < 3) bad("too few fields\n", NULL, NULL);
        while (i < elementsof(field)) field[i++] = 0;
        str = field[0];
        if (str) escape(str);
        fmt = field[1];
        if (fmt) escape(fmt);
        ans = field[2];
        if (!ans) bad("NIL answer", NULL, NULL);
        if (str) {
            if (!strcmp(str, "SET")) {
                if (!fmt) bad("NIL SET variable", NULL, NULL);
                if (!strcmp(fmt, "NOW")) {
                    t_now = tmdate(ans, &e, &t_now);
                    if (*e) bad("invalid NOW", ans, NULL);
                    sfprintf(sfstdout, "NOTE	base date is %s\n", fmttime(NULL, t_now));
                } else
                    bad("unknown SET variable", fmt, NULL);
                continue;
            }
            if (!strcmp(str, "FMT")) {
                str = 0;
                if (!fmt) {
                    bad("NIL format", NULL, NULL);
                    continue;
                }
                t_now = tmdate(fmt, &e, &t_now);
                if (*e) bad("invalid NOW", fmt, NULL);
                fmt = ans;
                if (fmt) escape(fmt);
                ans = field[3];
                if (!ans) bad("NIL answer", NULL, NULL);
            }
        }
        flags = 0;
        sfsync(sfstdout);
        if (verbose)
            sfprintf(sfstdout, "%d: str=`%s' fmt=`%s' ans=`%s'\n", state.lineno, str, fmt, ans);
        if (!str) {
            testno++;
            s = fmttime(fmt, t_now);
            escape(ans);
            if (strcmp(s, ans)) report("FAILED", s, ans, NULL, 0);
            continue;
        }
        t_ans = tmdate(ans, &e, &t_now);
        if (*e) report("answer FAILED", e, NULL, NULL, 0);
        s = fmttime("%k", t_ans);
        if (strcmp(ans, s)) {
            testno++;
            report("FAILED", s, ans, NULL, 0);
        } else
            for (;;) {
                testno++;
                t_str = tmscan(str, &e, fmt, &f, &t_now, flags);
                if (*e) {
                    report("subject FAILED", str, fmt, e, flags);
                    break;
                } else if (*f) {
                    report("format FAILED", str, fmt, f, flags);
                    break;
                } else if (t_str != t_ans) {
                    int n;
                    char tmp[128];

                    n = sfsprintf(tmp, sizeof(tmp), "\n\t[%s] expecting", fmttime(NULL, t_str));
                    sfsprintf(tmp + n, sizeof(tmp) - n, " [%s]", fmttime(NULL, t_ans));
                    report("FAILED", str, fmt, tmp, flags);
                    break;
                }
                if (flags & TM_PEDANTIC) break;
                flags |= TM_PEDANTIC;
            }
    }
    sfprintf(sfstdout, "TEST	tmscan, %d test%s", testno, testno == 1 ? "" : "s");
    if (state.warnings)
        sfprintf(sfstdout, ", %d warning%s", state.warnings, state.warnings == 1 ? "" : "s");
    if (state.signals) sfprintf(sfstdout, ", %d signals", state.signals);
    sfprintf(sfstdout, ", %d error%s\n", state.errors, state.errors == 1 ? "" : "s");
    return 0;
}
