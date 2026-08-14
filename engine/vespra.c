/*
 * vespra.c -- the chat "model": a keyword-and-pattern conversationalist in C.
 *
 * How a turn works:
 *   1. the line is normalised (lower case, punctuation turned into clause marks)
 *   2. codegen.c gets first refusal, in case the user asked for a snippet
 *   3. otherwise the clause with the highest ranked keyword is chosen
 *   4. that keyword's decomposition patterns are matched, captures have their
 *      pronouns swapped, and a reassembly template is filled in
 *   5. if nothing matches we bring up a remembered "my ..." line, or fall back
 *   6. the answer is shaped by the current mode: clipped short for fast,
 *      left alone for smart, given a second thought for pro
 *
 * It talks to run.py over stdin/stdout. Lines coming in are either
 *
 *   >text     something the person said
 *   !command  a control line from the runner (see control() below)
 *
 * and a bare line with no prefix is treated as speech, so the program is still
 * usable on its own: ./build/vespra and type at it.
 *
 * Each reply is one or more lines followed by a sentinel, so multi line code
 * replies stay in one piece:
 *
 *   --END--        end of a normal turn
 *   --END--QUIT--  the conversation is over
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "script.h"
#include "codegen.h"

#define MAX_LINE   1024
#define MAX_REPLY  16384
#define MAX_WORDS  128
#define MAX_PAT    32
#define MAX_CAPS   10
#define MEM_SLOTS  6

#define END_NORMAL "--END--"
#define END_QUIT   "--END--QUIT--"

typedef enum { MODE_FAST, MODE_SMART, MODE_PRO } Mode;

/* Rotating reassembly cursors, one per rule, so replies cycle in order. */
static int reasmb_cursor[sizeof KEYWORDS / sizeof KEYWORDS[0]][MAX_RULES];
static int fallback_cursor;
static int farewell_cursor;
static int memory_cursor;
static int probe_cursor;
static int unmatched_turns;

/* Set by the runner: how hard to think, and who it is talking to. */
static Mode mode = MODE_SMART;
static char user_name[64];
static long turn_no;

/* Swearing: she matches your register, then settles back down. */
static int swearing_on = 1;
static int swear_level;      /* 0 polite, 1 amused, 2 joining in, 3 full tilt */
static int calm_turns;       /* clean turns since the last swear */
static int swear_cursor;
static int calm_cursor;

/* Remembered "my ..." lines, used when the user says something unrecognised. */
static char memory[MEM_SLOTS][MAX_LINE];
static int memory_count;

/* ------------------------------------------------------------------ text */

/*
 * Lower case the line, keep letters, digits and apostrophes, turn sentence
 * punctuation into '|' so clauses can be split, and squeeze everything else
 * into single spaces.
 */
static void normalize(const char *in, char *out, size_t outsz)
{
    size_t j = 0;
    int pending_space = 0;

    for (size_t i = 0; in[i] != '\0'; i++) {
        unsigned char c = (unsigned char)in[i];
        char keep = 0;

        if (isalnum(c))
            keep = (char)tolower(c);
        else if (c == '\'')
            keep = '\'';
        else if (c == '.' || c == ',' || c == '?' || c == '!' ||
                 c == ';' || c == ':')
            keep = '|';

        if (keep == 0) {
            if (j > 0)
                pending_space = 1;
            continue;
        }

        if (pending_space && keep != '|' && j + 1 < outsz)
            out[j++] = ' ';
        pending_space = 0;

        if (j + 1 < outsz)
            out[j++] = keep;
    }

    out[j] = '\0';
}

/*
 * Rewrite contractions ("i'm" -> "i am") so the script only needs one spelling
 * of everything. Clause marks and spacing are copied through untouched.
 */
static void expand_contractions(const char *in, char *out, size_t outsz)
{
    char word[64];
    size_t j = 0, w = 0;

    for (size_t i = 0;; i++) {
        char c = in[i];

        if (isalnum((unsigned char)c) || c == '\'') {
            if (w + 1 < sizeof word)
                word[w++] = c;
            continue;
        }

        if (w > 0) {
            const char *use = word;
            size_t ul;

            word[w] = '\0';
            for (const Swap *s = CONTRACTIONS; s->from != NULL; s++) {
                if (strcmp(word, s->from) == 0) {
                    use = s->to;
                    break;
                }
            }
            ul = strlen(use);
            if (j + ul + 1 < outsz) {
                memcpy(out + j, use, ul);
                j += ul;
            }
            w = 0;
        }

        if (c == '\0')
            break;
        if (j + 1 < outsz)
            out[j++] = c;
    }

    out[j] = '\0';
}

/* Split on '|' in place. Empty clauses are dropped. */
static int split_clauses(char *s, char *clauses[], int maxc)
{
    int n = 0;
    char *start = s;

    for (char *p = s;; p++) {
        if (*p == '|' || *p == '\0') {
            int done = (*p == '\0');
            *p = '\0';
            while (*start == ' ')
                start++;
            if (*start != '\0' && n < maxc)
                clauses[n++] = start;
            if (done)
                break;
            start = p + 1;
        }
    }
    return n;
}

/* Split on spaces in place. */
static int split_words(char *s, char *words[], int maxw)
{
    int n = 0;

    for (char *p = s; *p != '\0';) {
        while (*p == ' ')
            p++;
        if (*p == '\0')
            break;
        if (n < maxw)
            words[n++] = p;
        while (*p != '\0' && *p != ' ')
            p++;
        if (*p == ' ')
            *p++ = '\0';
    }
    return n;
}

/* Tidy the finished sentence: no space before punctuation, capital first letter. */
static void tidy(char *s)
{
    size_t r = 0, w = 0;

    while (s[r] != '\0') {
        if (s[r] == ' ') {
            char next = s[r + 1];
            if (next == ' ') {          /* squeeze doubles */
                r++;
                continue;
            }
            if (next == '?' || next == '.' || next == ',' ||
                next == '!' || next == '\'') {
                r++;                     /* drop the space */
                continue;
            }
        }
        s[w++] = s[r++];
    }
    s[w] = '\0';

    if (s[0] != '\0')
        s[0] = (char)toupper((unsigned char)s[0]);
}

/* ------------------------------------------------------- pronoun swapping */

static const char *swap_word(const char *word)
{
    for (const Swap *s = SWAPS; s->from != NULL; s++)
        if (strcmp(word, s->from) == 0)
            return s->to;
    return word;
}

/* Copy words[from .. from+len) into out, flipping pronouns as we go. */
static void swapped_span(char *const *words, int from, int len,
                         char *out, size_t outsz)
{
    size_t j = 0;

    out[0] = '\0';
    for (int i = from; i < from + len; i++) {
        const char *w = swap_word(words[i]);
        size_t wl = strlen(w);

        if (j > 0 && j + 1 < outsz)
            out[j++] = ' ';
        if (j + wl + 1 >= outsz)
            break;
        memcpy(out + j, w, wl);
        j += wl;
    }
    out[j] = '\0';
}

/* --------------------------------------------------------- pattern match */

/*
 * Match a tokenised pattern against a tokenised clause. '*' matches zero or
 * more words; each '*' fills the next capture slot. Shortest match first,
 * with backtracking, which is what the original decomposition rules assume.
 */
static int match(char *const *pat, int np, char *const *words, int nw,
                 int pi, int wi, int *cap_at, int *cap_len, int star)
{
    if (pi == np)
        return wi == nw;

    if (strcmp(pat[pi], "*") == 0) {
        for (int k = wi; k <= nw; k++) {
            if (star < MAX_CAPS) {
                cap_at[star] = wi;
                cap_len[star] = k - wi;
            }
            if (match(pat, np, words, nw, pi + 1, k, cap_at, cap_len, star + 1))
                return 1;
        }
        return 0;
    }

    if (wi < nw && strcmp(pat[pi], words[wi]) == 0)
        return match(pat, np, words, nw, pi + 1, wi + 1, cap_at, cap_len, star);

    return 0;
}

/* Fill %1..%9 in a template from the captured (and swapped) spans. */
static void reassemble(const char *tmpl, char *const *words,
                       const int *cap_at, const int *cap_len, int ncaps,
                       char *out, size_t outsz)
{
    size_t j = 0;

    for (const char *p = tmpl; *p != '\0' && j + 1 < outsz; p++) {
        if (*p == '%' && p[1] >= '1' && p[1] <= '9') {
            int idx = p[1] - '1';
            char piece[MAX_LINE] = "";

            if (idx < ncaps)
                swapped_span(words, cap_at[idx], cap_len[idx],
                             piece, sizeof piece);

            for (const char *q = piece; *q != '\0' && j + 1 < outsz; q++)
                out[j++] = *q;
            p++;
            continue;
        }
        out[j++] = *p;
    }
    out[j] = '\0';
    tidy(out);
}

static int count_stars(char *const *pat, int np)
{
    int n = 0;
    for (int i = 0; i < np; i++)
        if (strcmp(pat[i], "*") == 0)
            n++;
    return n;
}

/* Rotate through a rule's reassembly list. */
static const char *next_reasmb(int kw_index, int rule_index, const Rule *rule)
{
    int count = 0;
    int pick;

    while (count < MAX_REASMB && rule->reasmb[count] != NULL)
        count++;
    if (count == 0)
        return NULL;

    pick = reasmb_cursor[kw_index][rule_index] % count;
    reasmb_cursor[kw_index][rule_index] = (pick + 1) % count;
    return rule->reasmb[pick];
}

/* ----------------------------------------------------------- the memory */

/* If the clause is about "my something", keep it to bring up later. */
static void remember(char *const *words, int nw)
{
    char pat_buf[] = "* my *";
    char *pat[MAX_PAT];
    int cap_at[MAX_CAPS] = { 0 }, cap_len[MAX_CAPS] = { 0 };
    int np;
    const char *tmpl;
    int count = 0;

    np = split_words(pat_buf, pat, MAX_PAT);
    if (!match(pat, np, words, nw, 0, 0, cap_at, cap_len, 0))
        return;
    if (cap_len[1] == 0)
        return;

    /* "my name is Cam" is not the kind of thing worth bringing up later */
    if (strcmp(words[cap_at[1]], "name") == 0)
        return;

    while (MEMORY_TEMPLATES[count] != NULL)
        count++;
    tmpl = MEMORY_TEMPLATES[memory_cursor % count];
    memory_cursor = (memory_cursor + 1) % count;

    if (memory_count == MEM_SLOTS) {
        for (int i = 1; i < MEM_SLOTS; i++)
            memcpy(memory[i - 1], memory[i], MAX_LINE);
        memory_count--;
    }

    reassemble(tmpl, words, cap_at, cap_len, 2,
               memory[memory_count], MAX_LINE);
    memory_count++;
}

static int recall(char *out, size_t outsz)
{
    if (memory_count == 0)
        return 0;

    snprintf(out, outsz, "%s", memory[0]);
    for (int i = 1; i < memory_count; i++)
        memcpy(memory[i - 1], memory[i], MAX_LINE);
    memory_count--;
    return 1;
}

/* -------------------------------------------------------------- the turn */

/*
 * A farewell only counts when the clause is a farewell -- "goodbye", "ok bye
 * now" -- not when the word merely turns up, as in "I said goodbye to my
 * mother". So: short clauses, or a clause that opens with the word.
 */
static int is_farewell(char *const *words, int nw)
{
    static const char *bye[] = {
        "bye", "goodbye", "quit", "exit", "farewell", "goodnight", NULL
    };

    if (nw == 0 || nw > 4)
        return 0;

    for (int i = 0; i < nw; i++)
        for (const char **b = bye; *b != NULL; b++)
            if (strcmp(words[i], *b) == 0)
                return (i == 0) || (nw <= 3);
    return 0;
}

/* Try every rule of one keyword against one clause. */
static int apply_keyword(int kw_index, char *const *words, int nw,
                         char *out, size_t outsz)
{
    const Keyword *kw = &KEYWORDS[kw_index];

    for (int r = 0; r < MAX_RULES && kw->rules[r].decomp != NULL; r++) {
        char pat_buf[MAX_LINE];
        char *pat[MAX_PAT];
        int cap_at[MAX_CAPS] = { 0 }, cap_len[MAX_CAPS] = { 0 };
        int np, nstars;
        const char *tmpl;

        snprintf(pat_buf, sizeof pat_buf, "%s", kw->rules[r].decomp);
        np = split_words(pat_buf, pat, MAX_PAT);
        nstars = count_stars(pat, np);

        if (!match(pat, np, words, nw, 0, 0, cap_at, cap_len, 0))
            continue;

        tmpl = next_reasmb(kw_index, r, &kw->rules[r]);
        if (tmpl == NULL)
            continue;

        reassemble(tmpl, words, cap_at, cap_len, nstars, out, outsz);
        return 1;
    }
    return 0;
}

/*
 * Rank every keyword present in a clause, then try them from the top down.
 * Returns 1 and fills `out` if some rule produced a reply.
 */
static int answer_clause(char *clause, char *out, size_t outsz)
{
    char work[MAX_LINE];
    char *words[MAX_WORDS];
    int nw;
    int order[64];
    int found = 0;
    int nkw = (int)(sizeof KEYWORDS / sizeof KEYWORDS[0]) - 1;

    snprintf(work, sizeof work, "%s", clause);
    nw = split_words(work, words, MAX_WORDS);
    if (nw == 0)
        return 0;

    /* Collect keywords in the order they appear in the sentence. */
    for (int i = 0; i < nw && found < 64; i++) {
        for (int k = 0; k < nkw; k++) {
            if (strcmp(words[i], KEYWORDS[k].word) != 0)
                continue;
            for (int seen = 0; seen < found; seen++)
                if (order[seen] == k)
                    goto next_word;
            order[found++] = k;
            break;
        }
    next_word:
        continue;
    }
    if (found == 0)
        return 0;

    /* Highest rank first; stable, so equal ranks keep sentence order. */
    for (int i = 1; i < found; i++) {
        int cur = order[i];
        int j = i - 1;
        while (j >= 0 && KEYWORDS[order[j]].rank < KEYWORDS[cur].rank) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = cur;
    }

    remember(words, nw);

    for (int i = 0; i < found; i++)
        if (apply_keyword(order[i], words, nw, out, outsz))
            return 1;

    return 0;
}

static void fallback(char *out, size_t outsz)
{
    const char **list = (mode == MODE_FAST) ? FAST_FALLBACKS : FALLBACKS;
    int count = 0;

    if (mode != MODE_FAST && unmatched_turns % 2 == 1 && recall(out, outsz))
        return;

    while (list[count] != NULL)
        count++;
    snprintf(out, outsz, "%s", list[fallback_cursor % count]);
    fallback_cursor = (fallback_cursor + 1) % count;
}

/* ---------------------------------------------------------- the swearing */

/* Whole word search in an already normalised line. */
static int has_word(const char *hay, const char *word)
{
    size_t len = strlen(word);
    const char *p = hay;

    while ((p = strstr(p, word)) != NULL) {
        int left_ok = (p == hay) || (p[-1] == ' ') || (p[-1] == '|');
        char after = p[len];
        int right_ok = (after == '\0' || after == ' ' || after == '|');
        if (left_ok && right_ok)
            return 1;
        p += 1;
    }
    return 0;
}

static int contains_swear(const char *line)
{
    for (const char **w = SWEAR_WORDS; *w != NULL; w++)
        if (has_word(line, *w))
            return 1;
    return 0;
}

/*
 * Swear at her and she swears back, a bit more each time: amused first, then
 * joining in, then matching you properly. Stop, and after a few clean turns she
 * cools off again.
 */
static void swear_reply(char *out, size_t outsz)
{
    const char **list;
    int count = 0;

    if (swear_level < 3)
        swear_level++;

    list = (swear_level == 1) ? SWEAR_MILD
         : (swear_level == 2) ? SWEAR_BACK
                              : SWEAR_HARD;

    while (list[count] != NULL)
        count++;
    snprintf(out, outsz, "%s", list[swear_cursor % count]);
    swear_cursor = (swear_cursor + 1) % count;
}

/* Put the "right, calmed down now" line in front of an ordinary reply. */
static void prepend_calm(char *s, size_t sz)
{
    char joined[MAX_REPLY];
    int count = 0;

    while (SWEAR_CALM[count] != NULL)
        count++;

    snprintf(joined, sizeof joined, "%s\n%.*s", SWEAR_CALM[calm_cursor % count],
             (int)sizeof joined - 80, s);
    calm_cursor = (calm_cursor + 1) % count;
    snprintf(s, sz, "%.*s", (int)sz - 1, joined);
}

/* ------------------------------------------------------------- the modes */

/* Keep only the first sentence, for fast mode. */
static void clip_to_sentence(char *s)
{
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (s[i] == '.' || s[i] == '?' || s[i] == '!') {
            s[i + 1] = '\0';
            return;
        }
        if (s[i] == '\n') {
            s[i] = '\0';
            return;
        }
    }
}

/* "How long have you been sad?" -> "Cam, how long have you been sad?" */
static void address_by_name(char *s, size_t sz)
{
    char body[MAX_REPLY];
    char joined[MAX_REPLY + sizeof user_name + 4];
    int keep_capital;

    if (user_name[0] == '\0' || s[0] == '\0')
        return;

    keep_capital = (s[0] == 'I' && (s[1] == ' ' || s[1] == '\'' || s[1] == '\0'));

    snprintf(body, sizeof body, "%s", s);
    if (!keep_capital)
        body[0] = (char)tolower((unsigned char)body[0]);

    snprintf(joined, sizeof joined, "%s, %s", user_name, body);
    snprintf(s, sz, "%.*s", (int)sz - 1, joined);   /* clipping here is fine */
}

/* Pro mode says the ordinary thing, then keeps thinking about it. */
static void add_second_thought(char *s, size_t sz)
{
    char tail[MAX_LINE] = "";
    int count = 0;

    while (PRO_PROBES[count] != NULL)
        count++;

    if (turn_no % 4 == 3 && recall(tail, sizeof tail)) {
        /* every so often, bring back something they said earlier instead */
        size_t n = strlen(s);
        snprintf(s + n, sz - n, "\n%s", tail);
        return;
    }

    {
        size_t n = strlen(s);
        snprintf(s + n, sz - n, "\n%s", PRO_PROBES[probe_cursor % count]);
        probe_cursor = (probe_cursor + 1) % count;
    }
}

/* Apply whatever the current mode does to a finished prose reply. */
static void shape_reply(char *s, size_t sz)
{
    switch (mode) {
    case MODE_FAST:
        clip_to_sentence(s);
        break;
    case MODE_SMART:
        if (turn_no % 3 == 2)
            address_by_name(s, sz);
        break;
    case MODE_PRO:
        if (turn_no % 2 == 1)
            address_by_name(s, sz);
        add_second_thought(s, sz);
        break;
    }
}

/*
 * Produce the reply for one raw input line.
 * Returns 1 when the conversation should end.
 */
static int respond(const char *raw, char *out, size_t outsz)
{
    char rough[MAX_LINE];
    char norm[MAX_LINE];
    char flat[MAX_LINE];
    char clause_buf[MAX_LINE];
    char *clauses[8];
    int nclauses;
    int cooled_off = 0;

    turn_no++;
    normalize(raw, rough, sizeof rough);
    expand_contractions(rough, norm, sizeof norm);

    if (norm[0] == '\0') {
        snprintf(out, outsz, "%s",
                 "I am listening. Say whatever comes to mind.");
        return 0;
    }

    /* One clause-mark-free copy, for the searches that do not care about them */
    {
        char *tmp;

        snprintf(flat, sizeof flat, "%s", norm);
        for (tmp = flat; *tmp != '\0'; tmp++)
            if (*tmp == '|')
                *tmp = ' ';
    }

    /* Was that a request for code? */
    if (codegen_try(flat, out, outsz))
        return 0;

    /* Are we swearing at each other? */
    if (swearing_on && contains_swear(flat)) {
        calm_turns = 0;
        swear_reply(out, outsz);
        shape_reply(out, outsz);
        return 0;
    }

    if (swear_level > 0 && ++calm_turns >= 4) {
        swear_level = 0;
        calm_turns = 0;
        cooled_off = 1;
    }

    snprintf(clause_buf, sizeof clause_buf, "%s", norm);
    nclauses = split_clauses(clause_buf, clauses, 8);

    for (int i = 0; i < nclauses; i++) {
        char words_buf[MAX_LINE];
        char *words[MAX_WORDS];
        int nw;

        snprintf(words_buf, sizeof words_buf, "%s", clauses[i]);
        nw = split_words(words_buf, words, MAX_WORDS);

        if (is_farewell(words, nw)) {
            int count = 0;
            while (FAREWELLS[count] != NULL)
                count++;
            snprintf(out, outsz, "%s", FAREWELLS[farewell_cursor % count]);
            farewell_cursor = (farewell_cursor + 1) % count;
            return 1;
        }
    }

    for (int i = 0; i < nclauses; i++) {
        if (answer_clause(clauses[i], out, outsz)) {
            unmatched_turns = 0;
            shape_reply(out, outsz);
            if (cooled_off)
                prepend_calm(out, outsz);
            return 0;
        }
    }

    unmatched_turns++;
    fallback(out, outsz);
    shape_reply(out, outsz);
    if (cooled_off)
        prepend_calm(out, outsz);
    return 0;
}

/* ---------------------------------------------------------------- control */

/*
 * Lines from the runner that are settings rather than speech:
 *
 *   !mode fast|smart|pro   how much effort to put into an answer
 *   !name <who>            what to call the person
 *   !replay <text>         feed a line from a saved chat back in, silently,
 *                          so reopening a conversation restores the memory
 *   !hello                 print the greeting again
 *   !back                  print the "we were talking before" line
 *
 * Anything unknown is ignored. Control lines answer with the sentinel only,
 * so the runner can read them and throw the empty reply away.
 */
static void control(const char *line, char *out, size_t outsz)
{
    out[0] = '\0';

    if (strncmp(line, "mode ", 5) == 0) {
        const char *want = line + 5;
        if (strcmp(want, "fast") == 0)
            mode = MODE_FAST;
        else if (strcmp(want, "pro") == 0)
            mode = MODE_PRO;
        else
            mode = MODE_SMART;
        return;
    }

    if (strncmp(line, "name ", 5) == 0) {
        snprintf(user_name, sizeof user_name, "%.*s",
                 (int)sizeof user_name - 1, line + 5);
        return;
    }

    if (strcmp(line, "name") == 0) {
        user_name[0] = '\0';
        return;
    }

    if (strncmp(line, "replay ", 7) == 0) {
        char scratch[MAX_REPLY];
        Mode saved = mode;

        /* Replaying rebuilds the memory and the reply rotations without
           printing anything, and without pro mode's extra thoughts. */
        mode = MODE_SMART;
        respond(line + 7, scratch, sizeof scratch);
        mode = saved;
        return;
    }

    if (strncmp(line, "swear ", 6) == 0) {
        swearing_on = (strcmp(line + 6, "off") != 0);
        if (!swearing_on) {
            swear_level = 0;
            calm_turns = 0;
        }
        return;
    }

    if (strcmp(line, "hello") == 0) {
        snprintf(out, outsz, "%s", GREETING);
        return;
    }

    if (strcmp(line, "back") == 0) {
        snprintf(out, outsz, "%s", WELCOME_BACK);
        return;
    }
}

/* ------------------------------------------------------------------ main */

int main(void)
{
    char line[MAX_LINE];
    char reply[MAX_REPLY];

    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("%s\n%s\n", GREETING, END_NORMAL);
    fflush(stdout);

    while (fgets(line, sizeof line, stdin) != NULL) {
        size_t len = strlen(line);
        int done;

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        reply[0] = '\0';

        if (line[0] == '!') {
            control(line + 1, reply, sizeof reply);
            if (reply[0] != '\0')
                printf("%s\n", reply);
            printf("%s\n", END_NORMAL);
            fflush(stdout);
            continue;
        }

        done = respond(line[0] == '>' ? line + 1 : line, reply, sizeof reply);

        printf("%s\n%s\n", reply, done ? END_QUIT : END_NORMAL);
        fflush(stdout);

        if (done)
            return 0;
    }

    return 0;
}
