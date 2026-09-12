/*
 * train_lm.c -- teach the model to talk.
 *
 * Reads data/corpus.bin (token numbers produced by train/prepare.py), cuts it
 * into windows, and runs ordinary supervised training: predict the next token,
 * measure how wrong that was, push every weight a little way in the direction
 * that would have been less wrong. Thousands of times over.
 *
 *   ./build/train --minutes 30            train for half an hour, then stop
 *   ./build/train --minutes 30 --resume   carry on from the last checkpoint
 *
 * It saves model/vespra.lm every few hundred steps, so stopping early with
 * Ctrl-C costs you at most a couple of minutes of work, and training can be
 * picked up again later.
 */
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tinylm.h"

#define CORPUS   "data/corpus.bin"
#define VOCAB    "data/vocab.txt"
#define MODEL    "model/vespra.lm"

static volatile sig_atomic_t interrupted;

static void on_interrupt(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int count_vocab(void)
{
    FILE *file = fopen(VOCAB, "r");
    char line[256];
    int count = 0;

    if (file == NULL)
        return -1;
    while (fgets(line, sizeof line, file) != NULL)
        count++;
    fclose(file);
    return count;
}

static unsigned short *read_corpus(size_t *out_count)
{
    FILE *file = fopen(CORPUS, "rb");
    unsigned short *tokens;
    long size;

    if (file == NULL)
        return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);

    tokens = (unsigned short *)malloc((size_t)size);
    if (tokens == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(tokens, 1, (size_t)size, file) != (size_t)size) {
        free(tokens);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_count = (size_t)size / sizeof(unsigned short);
    return tokens;
}

static void spell_time(double seconds, char *out, size_t outsz)
{
    int whole = (int)(seconds + 0.5);
    if (whole < 60)
        snprintf(out, outsz, "%ds", whole);
    else if (whole < 3600)
        snprintf(out, outsz, "%dm %02ds", whole / 60, whole % 60);
    else
        snprintf(out, outsz, "%dh %02dm", whole / 3600, (whole % 3600) / 60);
}

int main(int argc, char **argv)
{
    double minutes = 20.0;
    int resume = 0;
    int dim = 192, layers = 6, heads = 6, context = 96, batch = 16;
    float lr = 6e-4f, weight_decay = 0.05f;
    int warmup = 100;

    LM lm;
    LMConfig config;
    unsigned short *corpus, *inputs, *targets;
    size_t corpus_count = 0;
    unsigned seed = 20260814u;
    double started, last_save = 0;
    float smoothed = 0.0f;
    long step = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--minutes") == 0 && i + 1 < argc)
            minutes = atof(argv[++i]);
        else if (strcmp(argv[i], "--resume") == 0)
            resume = 1;
        else if (strcmp(argv[i], "--dim") == 0 && i + 1 < argc)
            dim = atoi(argv[++i]);
        else if (strcmp(argv[i], "--layers") == 0 && i + 1 < argc)
            layers = atoi(argv[++i]);
        else if (strcmp(argv[i], "--heads") == 0 && i + 1 < argc)
            heads = atoi(argv[++i]);
        else if (strcmp(argv[i], "--context") == 0 && i + 1 < argc)
            context = atoi(argv[++i]);
        else if (strcmp(argv[i], "--batch") == 0 && i + 1 < argc)
            batch = atoi(argv[++i]);
        else if (strcmp(argv[i], "--lr") == 0 && i + 1 < argc)
            lr = (float)atof(argv[++i]);
    }

    if (!(minutes > 0.0)) {
        fprintf(stderr, "--minutes must be greater than 0 (got %g)\n", minutes);
        return 1;
    }

    signal(SIGINT, on_interrupt);
    signal(SIGTERM, on_interrupt);

    corpus = read_corpus(&corpus_count);
    if (corpus == NULL) {
        fprintf(stderr, "No training data. Run:  python3 train/prepare.py\n");
        return 1;
    }

    config.vocab = count_vocab();
    if (config.vocab <= 0) {
        fprintf(stderr, "No vocabulary. Run:  python3 train/prepare.py\n");
        return 1;
    }
    config.dim = dim;
    config.layers = layers;
    config.heads = heads;
    config.context = context;

    if (resume && lm_load(&lm, MODEL, batch) == 0) {
        printf("carrying on from %ld steps of training\n", lm.steps);
        config = lm.config;
    } else if (lm_init(&lm, config, batch, seed) != 0) {
        fprintf(stderr, "out of memory setting up the model\n");
        return 1;
    }

    printf("model    %d words, %d wide, %d layers, %d heads, %d token memory\n",
           config.vocab, config.dim, config.layers, config.heads, config.context);
    printf("weights  %.2fM\n", lm.params.count / 1e6);
    printf("corpus   %.2fM tokens\n", corpus_count / 1e6);
    printf("training for %.0f minutes -- Ctrl-C stops early and keeps the model\n\n",
           minutes);

    inputs = (unsigned short *)malloc(sizeof(unsigned short) *
                                      (size_t)batch * context);
    targets = (unsigned short *)malloc(sizeof(unsigned short) *
                                       (size_t)batch * context);
    if (inputs == NULL || targets == NULL)
        return 1;

    started = now();
    last_save = started;

    while (!interrupted) {
        double elapsed = now() - started;
        double fraction = elapsed / (minutes * 60.0);
        float rate;
        float loss;

        if (fraction >= 1.0)
            break;

        /* pick random windows out of the corpus */
        for (int b = 0; b < batch; b++) {
            size_t at;
            seed = seed * 1664525u + 1013904223u;
            at = (size_t)(seed % (unsigned)(corpus_count - context - 2));
            for (int t = 0; t < context; t++) {
                inputs[(size_t)b * context + t] = corpus[at + t];
                targets[(size_t)b * context + t] = corpus[at + t + 1];
            }
        }

        /* warm up, then cosine down: standard, and it matters a lot */
        if (step < warmup)
            rate = lr * (float)(step + 1) / (float)warmup;
        else
            rate = 0.1f * lr + 0.9f * lr *
                   0.5f * (1.0f + cosf(3.14159265f * (float)fraction));

        loss = lm_forward(&lm, inputs, targets, 1);
        lm_step(&lm, rate, weight_decay, 1.0f);

        smoothed = (step == 0) ? loss : 0.98f * smoothed + 0.02f * loss;
        step++;

        if (step % 10 == 0 || step == 1) {
            char spent[32], left[32];
            double per_step = elapsed / (double)step;
            spell_time(elapsed, spent, sizeof spent);
            spell_time(minutes * 60.0 - elapsed, left, sizeof left);
            printf("\rstep %-6ld loss %.3f  (perplexity %6.1f)  %s elapsed, "
                   "%s left, %.1f steps/s   ",
                   lm.steps, smoothed, expf(smoothed), spent, left,
                   1.0 / (per_step + 1e-9));
            fflush(stdout);
        }

        if (now() - last_save > 120.0) {     /* checkpoint every two minutes */
            lm_save(&lm, MODEL);
            last_save = now();
        }
    }

    printf("\n\n");
    if (interrupted)
        printf("stopped early -- saving what it has learned so far\n");

    if (lm_save(&lm, MODEL) != 0) {
        fprintf(stderr, "could not write %s (does model/ exist?)\n", MODEL);
        return 1;
    }

    printf("saved %s after %ld steps, %.2fM tokens seen\n",
           MODEL, lm.steps, lm.tokens_seen / 1e6);
    printf("final loss %.3f (perplexity %.1f)\n", smoothed, expf(smoothed));
    printf("\nTrain it more any time:  python3 run.py --train 30\n");

    free(inputs);
    free(targets);
    free(corpus);
    lm_free(&lm);
    return 0;
}
