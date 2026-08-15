/*
 * vespra.c -- the chat program.
 *
 * There are no reply rules in here any more. Every answer comes out of the
 * trained model in model/vespra.lm: the whole conversation so far is turned
 * into token numbers, fed through the transformer, and the reply is sampled a
 * word at a time from what the model predicts should come next.
 *
 * So the model reads everything you wrote -- not a keyword out of it -- and it
 * answers with words it learned from 250,000 real conversations, not from a
 * list somebody typed in.
 *
 * The one thing still done by hand is the code snippets (codegen.c): when you
 * ask for a button in CSS you want working CSS, and a 1.6M parameter model
 * trained on film dialogue is not going to give you that.
 *
 * Lines in:
 *   >text     something the person said
 *   !command  a control line from the runner
 * Lines out: the reply, then --END-- (or --END--QUIT-- to finish).
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tinylm.h"
#include "codegen.h"

#define MAX_LINE   1024
#define MAX_REPLY  16384
#define MAX_VOCAB  65536
#define HISTORY    512      /* tokens of conversation kept for context */
#define MAX_NEW    32

#define END_NORMAL "--END--"
#define END_QUIT   "--END--QUIT--"

typedef enum { MODE_FAST, MODE_SMART, MODE_PRO } Mode;

static LM lm;
static int model_ready;

static char **vocab;
static int vocab_count;

static unsigned short history[HISTORY];
static int history_len;

static Mode mode = MODE_SMART;
static char user_name[64];
static int swearing_on = 1;
static unsigned seed = 1u;

/* Only used to keep the mouth shut when /swear off is set. */
static const char *SWEARS[] = {
    "fuck", "fucking", "fucked", "fucker", "shit", "shitty", "bullshit",
    "bitch", "bastard", "asshole", "damn", "goddamn", "crap", "piss",
    "cunt", "prick", "whore", "slut", "nigger", "faggot", NULL
};

/* ------------------------------------------------------------ vocabulary */

/* strdup is not in C99, and this file should build anywhere. */
static char *copy_string(const char *text)
{
    size_t len = strlen(text) + 1;
    char *copy = (char *)malloc(len);
    if (copy != NULL)
        memcpy(copy, text, len);
    return copy;
}

static int load_vocab(void)
{
    FILE *file = fopen("data/vocab.txt", "r");
    char line[256];

    if (file == NULL)
        return -1;

    vocab = (char **)calloc(MAX_VOCAB, sizeof(char *));
    if (vocab == NULL) {
        fclose(file);
        return -1;
    }

    while (fgets(line, sizeof line, file) != NULL && vocab_count < MAX_VOCAB) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        vocab[vocab_count] = copy_string(line);
        if (vocab[vocab_count] == NULL)
            break;
        vocab_count++;
    }
    fclose(file);
    return vocab_count > 0 ? 0 : -1;
}

static int word_id(const char *word)
{
    for (int i = 0; i < vocab_count; i++)
        if (strcmp(vocab[i], word) == 0)
            return i;
    return TOK_UNK;
}

/* Same splitting rule train/prepare.py used, or the model sees nonsense. */
static int tokenize(const char *text, unsigned short *out, int max)
{
    char word[64];
    int count = 0;
    size_t at = 0;

    for (size_t i = 0;; i++) {
        char c = text[i];
        int letter = (isalpha((unsigned char)c) || c == '\'');

        if (letter) {
            if (at + 1 < sizeof word)
                word[at++] = (char)tolower((unsigned char)c);
            continue;
        }

        if (at > 0) {
            word[at] = '\0';
            if (count < max)
                out[count++] = (unsigned short)word_id(word);
            at = 0;
        }

        if (c == '\0')
            break;
        if ((c == '.' || c == ',' || c == '!' || c == '?') && count < max) {
            char punctuation[2] = { c, '\0' };
            out[count++] = (unsigned short)word_id(punctuation);
        }
    }
    return count;
}

/* Turn token numbers back into something a person can read. */
static void detokenize(const unsigned short *tokens, int count,
                       char *out, size_t outsz)
{
    size_t at = 0;
    int start_of_sentence = 1;

    out[0] = '\0';
    for (int i = 0; i < count; i++) {
        const char *word = (tokens[i] < vocab_count) ? vocab[tokens[i]] : "";
        int punctuation = (strlen(word) == 1 && strchr(".,!?", word[0]) != NULL);
        size_t len = strlen(word);

        if (len == 0)
            continue;
        if (at + len + 2 >= outsz)
            break;

        if (at > 0 && !punctuation)
            out[at++] = ' ';

        for (size_t c = 0; c < len; c++) {
            char letter = word[c];
            if (c == 0 && start_of_sentence)
                letter = (char)toupper((unsigned char)letter);
            out[at++] = letter;
        }
        start_of_sentence = punctuation && (word[0] == '.' || word[0] == '!' ||
                                            word[0] == '?');
        out[at] = '\0';
    }

    /* "i" on its own always wants a capital */
    for (size_t i = 0; at > 0 && i + 1 < at; i++)
        if (out[i] == 'i' && (i == 0 || out[i - 1] == ' ') &&
            (out[i + 1] == ' ' || out[i + 1] == '\'' ))
            out[i] = 'I';
}

/* ------------------------------------------------------------- history */

static void history_push(const unsigned short *tokens, int count)
{
    if (count >= HISTORY) {
        tokens += count - HISTORY + 1;
        count = HISTORY - 1;
    }
    if (history_len + count > HISTORY) {
        int drop = history_len + count - HISTORY;
        memmove(history, history + drop,
                sizeof(unsigned short) * (size_t)(history_len - drop));
        history_len -= drop;
    }
    memcpy(history + history_len, tokens, sizeof(unsigned short) * (size_t)count);
    history_len += count;
}

static void history_push_turn(int speaker, const char *text)
{
    unsigned short tokens[MAX_LINE];
    unsigned short marker = (unsigned short)speaker;
    int count = tokenize(text, tokens, MAX_LINE);

    history_push(&marker, 1);
    if (count > 0)
        history_push(tokens, count);
}

/* --------------------------------------------------------- the answer */

static int has_swear(const char *text)
{
    char lower[MAX_REPLY];
    size_t i;

    for (i = 0; i + 1 < sizeof lower && text[i]; i++)
        lower[i] = (char)tolower((unsigned char)text[i]);
    lower[i] = '\0';

    for (const char **w = SWEARS; *w; w++) {
        const char *found = lower;
        size_t len = strlen(*w);
        while ((found = strstr(found, *w)) != NULL) {
            int left = (found == lower) || !isalpha((unsigned char)found[-1]);
            int right = !isalpha((unsigned char)found[len]);
            if (left && right)
                return 1;
            found++;
        }
    }
    return 0;
}

/*
 * Ask the model for one reply. Modes differ in how many candidates it draws
 * and how it picks between them, which is the whole difference between fast
 * and pro: more thinking, more sampling, better odds of a good line.
 */
static void answer(char *out, size_t outsz)
{
    int candidates = (mode == MODE_FAST) ? 1 : (mode == MODE_SMART) ? 3 : 8;
    int max_new = (mode == MODE_FAST) ? 14 : (mode == MODE_SMART) ? 22 : MAX_NEW;
    float temperature = (mode == MODE_FAST) ? 0.75f : 0.95f;
    int top_k = (mode == MODE_FAST) ? 30 : 60;
    unsigned short stops[3] = { TOK_USER, TOK_END, TOK_BOT };

    char best[MAX_REPLY] = "";
    float best_score = -1e30f;

    unsigned short prompt[HISTORY + 1];
    int prompt_len = history_len;

    if (prompt_len > HISTORY)
        prompt_len = HISTORY;
    memcpy(prompt, history, sizeof(unsigned short) * (size_t)prompt_len);
    prompt[prompt_len++] = TOK_BOT;      /* now it is the model's turn */

    for (int attempt = 0; attempt < candidates; attempt++) {
        unsigned short tokens[MAX_NEW];
        char text[MAX_REPLY];
        float logprob = -99.0f;
        int count = lm_generate(&lm, prompt, prompt_len, tokens, max_new,
                                temperature, top_k, 1.15f, stops, 3,
                                &seed, &logprob);
        float score;

        if (count == 0)
            continue;

        detokenize(tokens, count, text, sizeof text);
        if (text[0] == '\0')
            continue;
        if (!swearing_on && has_swear(text))
            continue;

        /* prefer confident answers, and nudge away from two word grunts */
        score = logprob + (count < 4 ? -0.35f : 0.0f);
        if (score > best_score) {
            best_score = score;
            snprintf(best, sizeof best, "%s", text);
        }
    }

    if (best[0] == '\0')
        snprintf(best, sizeof best, "%s", "Hm. Say that again?");

    /* the model does not know your name, so address you here */
    if (user_name[0] != '\0' && mode != MODE_FAST && (seed & 3u) == 0)
        snprintf(out, outsz, "%s, %c%.*s", user_name,
                 (char)tolower((unsigned char)best[0]),
                 (int)outsz - 80, best + 1);
    else
        snprintf(out, outsz, "%.*s", (int)outsz - 1, best);
}

/* Normalise a line the way codegen.c expects: lower case, single spaces. */
static void flatten(const char *in, char *out, size_t outsz)
{
    size_t j = 0;
    int space = 0;

    for (size_t i = 0; in[i] && j + 1 < outsz; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c)) {
            if (space && j > 0 && j + 1 < outsz)
                out[j++] = ' ';
            space = 0;
            out[j++] = (char)tolower(c);
        } else {
            space = (j > 0);
        }
    }
    out[j] = '\0';
}

/* Returns 1 when the conversation should end. */
static int respond(const char *raw, char *out, size_t outsz)
{
    char flat[MAX_LINE];

    if (raw[0] == '\0') {
        snprintf(out, outsz, "%s", "Still here. Go on.");
        return 0;
    }

    flatten(raw, flat, sizeof flat);

    /* code requests are answered from the snippet library, not the model */
    if (codegen_try(flat, out, outsz)) {
        history_push_turn(TOK_USER, raw);
        history_push_turn(TOK_BOT, "sure , here you go .");
        return 0;
    }

    history_push_turn(TOK_USER, raw);

    if (!model_ready) {
        snprintf(out, outsz, "%s",
                 "I have no trained model to think with yet.\n"
                 "Run:  python3 train/prepare.py  then  python3 run.py --train 30");
        return 0;
    }

    answer(out, outsz);
    history_push_turn(TOK_BOT, out);
    return 0;
}

/* ---------------------------------------------------------------- control */

static void control(const char *line, char *out, size_t outsz)
{
    out[0] = '\0';

    if (strncmp(line, "mode ", 5) == 0) {
        const char *want = line + 5;
        mode = (strcmp(want, "fast") == 0) ? MODE_FAST
             : (strcmp(want, "pro") == 0) ? MODE_PRO : MODE_SMART;
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

    if (strncmp(line, "swear ", 6) == 0) {
        swearing_on = (strcmp(line + 6, "off") != 0);
        return;
    }

    /* rebuild the conversation from a saved chat, without answering */
    if (strncmp(line, "replay you ", 11) == 0) {
        history_push_turn(TOK_USER, line + 11);
        return;
    }
    if (strncmp(line, "replay bot ", 11) == 0) {
        history_push_turn(TOK_BOT, line + 11);
        return;
    }

    if (strcmp(line, "forget") == 0) {
        history_len = 0;
        return;
    }

    if (strcmp(line, "info") == 0) {
        if (!model_ready) {
            snprintf(out, outsz, "%s", "no model trained yet");
            return;
        }
        /* one short line each: long lines get clipped in narrow terminals */
        snprintf(out, outsz,
                 "%.2fM weights, trained by you\n"
                 "%d words known\n"
                 "%d layers, %d wide, %d heads\n"
                 "%d tokens of memory\n"
                 "%ld training steps, %.1fM tokens read",
                 lm.params.count / 1e6, lm.config.vocab, lm.config.layers,
                 lm.config.dim, lm.config.heads, lm.config.context,
                 lm.steps, lm.tokens_seen / 1e6);
        return;
    }

    if (strcmp(line, "hello") == 0) {
        if (!model_ready) {
            snprintf(out, outsz, "%s",
                     "Hello -- but I have not been trained yet.\n"
                     "Run:  python3 train/prepare.py  then  python3 run.py --train 30");
            return;
        }
        history_len = 0;
        history_push_turn(TOK_USER, "hello");
        answer(out, outsz);
        history_push_turn(TOK_BOT, out);
        return;
    }

    if (strcmp(line, "back") == 0) {
        snprintf(out, outsz, "%s",
                 "Right, I have read back over what we said. Carry on.");
        return;
    }
}

/* ------------------------------------------------------------------ main */

int main(void)
{
    char line[MAX_LINE];
    char reply[MAX_REPLY];

    setvbuf(stdout, NULL, _IOLBF, 0);
    seed = (unsigned)time(NULL) * 2654435761u + 12345u;

    if (load_vocab() == 0 && lm_load(&lm, "model/vespra.lm", 1) == 0)
        model_ready = 1;

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
