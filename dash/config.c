/*
 * INI-style config loader for the dashboard. Pure: no SDL, no global
 * state, no I/O beyond reading the chosen config file. The format is
 * documented at the top of dash/apcommander.default.conf.
 */
#include "config.h"

#include "compat.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TILE_ARGS 31  /* + argv[0] + NULL = 33 entries in the argv array */

/*
 * Raw fields from one [section] of the config file, before we resolve
 * the exec path and tokenize args. Kept on the parser stack only —
 * never escapes this translation unit.
 */
typedef struct TileSpec {
    char title[128];
    char desc[192];
    char exec[PATH_MAX];
    char args[1024];
    char hotkey;
} TileSpec;

/* ------------------------------------------------------------------------- */
/* Small string helpers                                                      */
/* ------------------------------------------------------------------------- */

static char *trim(char *s)
{
    char *e;

    while (*s == ' ' || *s == '\t')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' ||
                     e[-1] == '\n'))
        *--e = '\0';
    return s;
}

static char *xstrdup(const char *s)
{
    size_t n;
    char *o;

    if (!s)
        return NULL;
    n = strlen(s);
    o = malloc(n + 1);
    if (!o)
        return NULL;
    memcpy(o, s, n + 1);
    return o;
}

/*
 * Whitespace-split tokenizer with double-quote support. Writes up to
 * `max` tokens (each strdup'd) into `out` and returns the token count.
 * No escape sequences inside quotes — kept deliberately small.
 */
static int tokenize_args(const char *line, char **out, int max)
{
    int n = 0;
    const char *p = line ? line : "";
    char buf[1024];

    while (*p && n < max) {
        size_t blen = 0;

        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && blen < sizeof(buf) - 1)
                buf[blen++] = *p++;
            if (*p == '"')
                p++;
        } else {
            while (*p && *p != ' ' && *p != '\t' && blen < sizeof(buf) - 1)
                buf[blen++] = *p++;
        }
        buf[blen] = '\0';
        out[n] = xstrdup(buf);
        if (!out[n])
            break;
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------------- */
/* Path resolution                                                           */
/* ------------------------------------------------------------------------- */

/*
 * Resolves a config `exec` value to an absolute path:
 *   /abs/path        → use as-is
 *   ~/foo            → $HOME/foo (or $USERPROFILE on Windows)
 *   contains '/'     → relative to app_dir
 *   bare name        → app_dir/name first, then $PATH
 *
 * Writes the result to `out`. Returns 0 if the resolved path is
 * executable, -1 otherwise (out is still set to the best guess so the
 * missing-tile UI has something to display).
 */
static int resolve_exec(const char *app_dir, const char *exec, char *out,
                        size_t outsz)
{
    if (!exec || !exec[0] || !out || outsz == 0)
        return -1;

    if (exec[0] == '~' && exec[1] == '/') {
        char home[PATH_MAX];

        if (compat_home_dir(home, sizeof(home)) == 0) {
            snprintf(out, outsz, "%s%s", home, exec + 1);
            return compat_can_execute(out) ? 0 : -1;
        }
    }
    if (exec[0] == '/') {
        snprintf(out, outsz, "%s", exec);
        return compat_can_execute(out) ? 0 : -1;
    }
    if (strchr(exec, '/')) {
        snprintf(out, outsz, "%s/%s", app_dir, exec);
        return compat_can_execute(out) ? 0 : -1;
    }

    /* Bare name: try app_dir first (sibling tool), then $PATH. */
    snprintf(out, outsz, "%s/%s%s", app_dir, exec, compat_exe_suffix());
    if (compat_can_execute(out))
        return 0;
    if (compat_path_lookup(exec, out, outsz) == 0)
        return 0;
    /* Couldn't resolve — leave the app_dir guess so the user sees the
     * intended location. */
    snprintf(out, outsz, "%s/%s%s", app_dir, exec, compat_exe_suffix());
    return -1;
}

/* ------------------------------------------------------------------------- */
/* Spec → Tile                                                               */
/* ------------------------------------------------------------------------- */

/*
 * Builds a Tile from a parsed TileSpec: resolves the exec path,
 * tokenizes the args, copies title/desc. Returns 0 on success and
 * leaves the Tile partially populated on failure (so missing-binary
 * tiles still appear with their title visible).
 *
 * On allocation failure the Tile is fully cleaned up and zeroed before
 * returning -1.
 */
static int finalize_tile(Tile *t, const TileSpec *spec, const char *app_dir)
{
    char *arg_tokens[MAX_TILE_ARGS];
    int arg_count;
    int i;

    memset(t, 0, sizeof(*t));
    t->title = xstrdup(spec->title);
    t->desc = spec->desc[0] ? xstrdup(spec->desc) : NULL;
    t->hotkey = spec->hotkey;

    if (resolve_exec(app_dir, spec->exec, t->exe_path,
                     sizeof(t->exe_path)) != 0)
        t->missing = 1;

    arg_count = tokenize_args(spec->args, arg_tokens, MAX_TILE_ARGS);

    /* argv layout: [exe_path, parsed args..., NULL]. argv[0] is the path
     * by convention (what the child sees as argv[0]). */
    t->argv = calloc((size_t)arg_count + 2, sizeof(char *));
    if (!t->argv)
        goto fail;
    t->argv[0] = xstrdup(t->exe_path);
    if (!t->argv[0])
        goto fail;
    for (i = 0; i < arg_count; i++)
        t->argv[i + 1] = arg_tokens[i];
    t->argv[arg_count + 1] = NULL;
    t->argc = arg_count + 1;
    return 0;

fail:
    /* Free any tokens not yet adopted by t->argv. tokenize_args
     * leaves successful tokens in arg_tokens[0..arg_count); on calloc
     * failure none of them are inside argv yet, so we free them all. */
    for (i = 0; i < arg_count; i++)
        free(arg_tokens[i]);
    if (t->argv) {
        free(t->argv[0]);
        free(t->argv);
    }
    free(t->title);
    free(t->desc);
    memset(t, 0, sizeof(*t));
    return -1;
}

/* ------------------------------------------------------------------------- */
/* INI parser                                                                */
/* ------------------------------------------------------------------------- */

/* snprintf wrapper that warns if the value didn't fit. INI value fields
 * have hard size limits (TileSpec is on the stack); silent truncation
 * here would land paths/args wrong without any feedback. */
static void copy_field(char *dst, size_t dstsz, const char *val,
                       const char *path, int lineno, const char *key)
{
    int w = snprintf(dst, dstsz, "%s", val);

    if (w < 0 || (size_t)w >= dstsz) {
        fprintf(stderr,
                "config: %s:%d: %s= truncated to %zu chars (was %zu)\n",
                path, lineno, key, dstsz - 1, val ? strlen(val) : 0u);
    }
}

/*
 * Reads an INI-style config file into TileSpec entries. Returns the
 * number of entries on success, -1 on file open failure (errmsg set).
 *
 * Format:
 *   - Lines starting with '#' or ';' (after whitespace) are comments.
 *   - "[section]" begins a new tile entry. The section name itself is
 *     not displayed; it acts as a record separator only.
 *   - "key = value" inside a section. Recognised keys: title, desc,
 *     exec, args, hotkey. Unknown keys are ignored.
 *   - Values run to the end of the line; no inline-comment stripping.
 *
 * A section is committed only if both `title` and `exec` are present.
 */
static int parse_ini(const char *path, TileSpec *out, int max,
                     char *errmsg, size_t errsz)
{
    FILE *fp;
    char line[2048];
    TileSpec cur;
    int in_section = 0;
    int n = 0;
    int lineno = 0;

    if (errmsg && errsz)
        errmsg[0] = '\0';
    fp = fopen(path, "r");
    if (!fp) {
        if (errmsg)
            snprintf(errmsg, errsz, "open %s: %s", path, strerror(errno));
        return -1;
    }
    memset(&cur, 0, sizeof(cur));

    while (fgets(line, sizeof(line), fp)) {
        char *s;
        char *eq;
        char *key;
        char *val;
        char *ke;

        lineno++;
        s = trim(line);

        if (!*s || *s == '#' || *s == ';')
            continue;

        if (*s == '[') {
            if (in_section && cur.title[0] && cur.exec[0] && n < max)
                out[n++] = cur;
            memset(&cur, 0, sizeof(cur));
            in_section = 1;
            continue;
        }
        if (!in_section)
            continue;

        eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = '\0';
        key = s;
        val = eq + 1;

        ke = key + strlen(key);
        while (ke > key && (ke[-1] == ' ' || ke[-1] == '\t'))
            *--ke = '\0';
        while (*val == ' ' || *val == '\t')
            val++;

        if (strcmp(key, "title") == 0) {
            copy_field(cur.title, sizeof(cur.title), val, path, lineno, key);
        } else if (strcmp(key, "desc") == 0) {
            copy_field(cur.desc, sizeof(cur.desc), val, path, lineno, key);
        } else if (strcmp(key, "exec") == 0) {
            copy_field(cur.exec, sizeof(cur.exec), val, path, lineno, key);
        } else if (strcmp(key, "args") == 0) {
            copy_field(cur.args, sizeof(cur.args), val, path, lineno, key);
        } else if (strcmp(key, "hotkey") == 0 && val[0]) {
            unsigned char c = (unsigned char)val[0];
            if (isprint(c))
                cur.hotkey = (char)c;
        }
    }

    if (in_section && cur.title[0] && cur.exec[0] && n < max)
        out[n++] = cur;

    fclose(fp);
    return n;
}

/* ------------------------------------------------------------------------- */
/* Search chain                                                              */
/* ------------------------------------------------------------------------- */

/*
 * Tries to load `path` into `specs`. On success, copies `path` into
 * `chosen_path` and returns the spec count. On failure, prints the
 * parser's error to stderr (so the user can see why a configured path
 * was skipped) and returns -1.
 *
 * `path` may be NULL or empty — in that case this is a no-op returning
 * -1, so callers can pass through env/argv values without checking.
 */
static int try_load(const char *path, TileSpec *specs, int max,
                    char *chosen_path, size_t chosen_path_sz,
                    int verbose_on_fail)
{
    char errmsg[256];
    int n;

    if (!path || !path[0])
        return -1;
    n = parse_ini(path, specs, max, errmsg, sizeof(errmsg));
    if (n >= 0) {
        snprintf(chosen_path, chosen_path_sz, "%s", path);
        return n;
    }
    if (verbose_on_fail)
        fprintf(stderr, "config: %s\n", errmsg);
    return -1;
}

/* Resolves the search chain to a list of TileSpecs (still raw, not yet
 * resolved into Tiles). Returns -1 if no config could be opened. */
static int find_specs(const char *cli_path, TileSpec *specs, int max,
                      char *chosen_path, size_t chosen_path_sz)
{
    char path[PATH_MAX];
    char app_dir[PATH_MAX - 64];
    const char *xdg;
    int n;

    chosen_path[0] = '\0';

    /* 1. argv[1] / explicit CLI override. */
    n = try_load(cli_path, specs, max, chosen_path, chosen_path_sz, 1);
    if (n >= 0)
        return n;

    /* 2. $APCOMMANDER_CONFIG. */
    n = try_load(getenv("APCOMMANDER_CONFIG"), specs, max,
                 chosen_path, chosen_path_sz, 1);
    if (n >= 0)
        return n;

    /* 3. $XDG_CONFIG_HOME/apcommander/tiles.conf or ~/.config/...
     * The HOME buffer is intentionally smaller than `path` so the
     * "/.config/apcommander/tiles.conf" suffix is guaranteed to fit
     * after concatenation (no compiler truncation warning, no need to
     * runtime-check the snprintf return).
     *
     * The XDG Base Directory spec requires XDG_CONFIG_HOME to be an
     * absolute path, and tells implementations to ignore it otherwise.
     * We follow that — a relative value would resolve against the
     * dashboard's CWD, which a malicious parent process could place
     * us in. */
    xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] == '/') {
        snprintf(path, sizeof(path), "%s/apcommander/tiles.conf", xdg);
    } else {
        char home[PATH_MAX - 64];
        if (compat_home_dir(home, sizeof(home)) == 0)
            snprintf(path, sizeof(path),
                     "%s/.config/apcommander/tiles.conf", home);
        else
            path[0] = '\0';
    }
    n = try_load(path, specs, max, chosen_path, chosen_path_sz, 0);
    if (n >= 0)
        return n;

    /* 4 & 5. <app_dir>/apcommander.conf, then <app_dir>/...default.conf */
    if (compat_app_dir(app_dir, sizeof(app_dir)) == 0) {
        snprintf(path, sizeof(path), "%s/apcommander.conf", app_dir);
        n = try_load(path, specs, max, chosen_path, chosen_path_sz, 0);
        if (n >= 0)
            return n;
        snprintf(path, sizeof(path), "%s/apcommander.default.conf", app_dir);
        n = try_load(path, specs, max, chosen_path, chosen_path_sz, 0);
        if (n >= 0)
            return n;
    }

    return -1;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

int config_load(const char *cli_path, Tile *tiles, int max,
                char *chosen_path, size_t chosen_path_sz)
{
    TileSpec *specs;
    char app_dir[PATH_MAX - 64];
    int spec_count;
    int tile_count = 0;
    int i;

    if (!tiles || max <= 0 || !chosen_path || chosen_path_sz == 0)
        return -1;
    chosen_path[0] = '\0';

    specs = calloc((size_t)max, sizeof(*specs));
    if (!specs)
        return -1;

    spec_count = find_specs(cli_path, specs, max, chosen_path,
                            chosen_path_sz);
    if (spec_count < 0) {
        free(specs);
        return -1;
    }

    if (compat_app_dir(app_dir, sizeof(app_dir)) != 0)
        snprintf(app_dir, sizeof(app_dir), ".");

    for (i = 0; i < spec_count && tile_count < max; i++) {
        if (finalize_tile(&tiles[tile_count], &specs[i], app_dir) == 0)
            tile_count++;
    }

    free(specs);
    return tile_count;
}
