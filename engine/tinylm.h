/*
 * tinylm.h -- a small GPT-style transformer, written from scratch.
 *
 * Same shape as the language models everyone talks about, just tiny: token
 * embeddings, learned positions, a stack of pre-norm blocks (causal multi-head
 * self attention, then a feed forward layer), a final layer norm, and output
 * weights tied to the input embeddings. Trained by ordinary backpropagation
 * with AdamW.
 *
 * No libraries, no downloaded weights: it starts from random numbers and
 * learns from data/corpus.bin.
 */
#ifndef TINYLM_H
#define TINYLM_H

#include <stddef.h>
#include <stdio.h>

#define LM_MAGIC 0x314D4C56u    /* "VLM1" */

/* Token numbers 0..3 are fixed by train/prepare.py. */
#define TOK_UNK  0
#define TOK_USER 1
#define TOK_BOT  2
#define TOK_END  3

typedef struct {
    int vocab;      /* how many words it knows            */
    int dim;        /* width of the model                 */
    int layers;     /* how many transformer blocks        */
    int heads;      /* attention heads per block          */
    int context;    /* how many tokens it can look back at */
} LMConfig;

/*
 * Every weight in one flat array, so saving, loading and the optimiser can all
 * treat the model as a single vector. `p` points into `flat`.
 */
typedef struct {
    float *flat;
    size_t count;

    float *tok_emb;      /* vocab x dim, also the output weights (tied) */
    float *pos_emb;      /* context x dim                               */

    float *ln1_scale;    /* layers x dim                                */
    float *ln1_bias;
    float *wq, *wk, *wv, *wo;   /* layers x dim x dim                   */
    float *ln2_scale, *ln2_bias;
    float *w1;           /* layers x dim x (4*dim)                      */
    float *b1;           /* layers x (4*dim)                            */
    float *w2;           /* layers x (4*dim) x dim                      */
    float *b2;           /* layers x dim                                */

    float *lnf_scale, *lnf_bias;
} LMParams;

typedef struct {
    LMConfig config;
    LMParams params;
    LMParams grads;

    float *adam_m, *adam_v;
    long steps;          /* optimiser steps taken so far */
    double tokens_seen;

    /* activations, sized for one batch */
    int batch;
    float *acts;
    size_t acts_count;
} LM;

/* Set up a model with random weights. Returns 0 on success. */
int lm_init(LM *lm, LMConfig config, int batch, unsigned seed);
void lm_free(LM *lm);

/*
 * Run a batch forward and, if `targets` is given, backward too.
 * `inputs` and `targets` are batch x context token ids.
 * Returns the mean cross entropy loss in nats.
 */
float lm_forward(LM *lm, const unsigned short *inputs,
                 const unsigned short *targets, int do_backward);

/* One AdamW update from whatever is in lm->grads, then zero the grads. */
void lm_step(LM *lm, float learning_rate, float weight_decay, float clip);

/* Read out the logits lm_forward computed for one (batch, position) pair. */
void lm_read_logits(LM *lm, int batch_index, int position, float *out);

int lm_save(const LM *lm, const char *path);
int lm_load(LM *lm, const char *path, int batch);

/*
 * Continue `prompt` (a run of token ids) by sampling `max_new` tokens.
 * Stops early at any token in `stops`. Writes ids into `out`, returns how many.
 *
 * temperature 0 means always take the most likely word.
 * top_k 0 means consider every word.
 */
int lm_generate(LM *lm, const unsigned short *prompt, int prompt_len,
                unsigned short *out, int max_new,
                float temperature, int top_k, float repeat_penalty,
                const unsigned short *stops, int stop_count,
                unsigned *seed, float *out_logprob);

/*
 * A key/value cache for one sequence, so generating token N+1 does not mean
 * recomputing every layer's attention over tokens 0..N from scratch again --
 * only the one new token's own values need computing; everything before it
 * was already worked out on a previous call and is just reused. This is the
 * standard trick every LLM inference server relies on; without it, decoding
 * a run of N tokens costs O(N^2) instead of O(N).
 *
 * This model's position embeddings are absolute and learned (position 0 is
 * always a specific vector, not "the start of whatever window we have"), so
 * a cached key/value bakes in the exact position it was computed at. That
 * means the cache cannot just drop the oldest entry and slide the rest down
 * once it fills (length == config.context): every survivor would still
 * describe the position it used to be at, not the one it just moved into.
 * Instead, lm_decode_step rebuilds the whole cache in one pass exactly when
 * it fills up, using the same sliding window arithmetic lm_generate always
 * used before caching existed -- so a full conversation longer than the
 * context window costs an occasional O(context) rebuild rather than silently
 * drifting from what a full recompute would have produced.
 */
typedef struct {
    float *k, *v;    /* layers x context x dim */
    float *scratch;  /* internal use */
    unsigned short *tokens; /* token ids currently held, 0..length-1 */
    int length;      /* positions currently held, 0..config.context */
} LMCache;

int lm_cache_init(LMCache *cache, const LMConfig *config);
void lm_cache_free(LMCache *cache);

/*
 * Process `count` tokens (<= config.context) into an empty cache. Writes the
 * logits for the last of them (what should come after the prompt) into
 * `logits_out`, sized config.vocab.
 */
void lm_prefill(LM *lm, const unsigned short *tokens, int count,
                LMCache *cache, float *logits_out);

/*
 * Extend the cache by exactly one more token and write the logits for what
 * should come after *it* into `logits_out`. This is the cheap, O(1)-amortised
 * step lm_generate calls in a loop once the prompt has been prefilled.
 */
void lm_decode_step(LM *lm, unsigned short token, LMCache *cache,
                    float *logits_out);

#endif /* TINYLM_H */
