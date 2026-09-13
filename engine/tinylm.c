/*
 * tinylm.c -- the model. A small GPT, implemented from first principles.
 *
 * Layout of one block, pre-norm style:
 *
 *     x = x + attention(layernorm(x))
 *     x = x + feedforward(layernorm(x))
 *
 * then a final layer norm and a projection back to vocabulary size using the
 * token embeddings themselves (weight tying). Training is plain backpropagation
 * with AdamW, gradient clipping and a warmup/cosine learning rate, which is the
 * same recipe the big models use -- there is just far less of everything here.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "tinylm.h"

#ifdef _OPENMP
#include <omp.h>
#endif

/* ------------------------------------------------------------ parameters */

static size_t param_sizes(const LMConfig *c, size_t *sizes)
{
    int V = c->vocab, D = c->dim, L = c->layers, T = c->context;
    int F = 4 * D;
    size_t total = 0;
    int i = 0;

    sizes[i++] = (size_t)V * D;        /* tok_emb   */
    sizes[i++] = (size_t)T * D;        /* pos_emb   */
    sizes[i++] = (size_t)L * D;        /* ln1_scale */
    sizes[i++] = (size_t)L * D;        /* ln1_bias  */
    sizes[i++] = (size_t)L * D * D;    /* wq */
    sizes[i++] = (size_t)L * D * D;    /* wk */
    sizes[i++] = (size_t)L * D * D;    /* wv */
    sizes[i++] = (size_t)L * D * D;    /* wo */
    sizes[i++] = (size_t)L * D;        /* ln2_scale */
    sizes[i++] = (size_t)L * D;        /* ln2_bias  */
    sizes[i++] = (size_t)L * F * D;    /* w1 */
    sizes[i++] = (size_t)L * F;        /* b1 */
    sizes[i++] = (size_t)L * D * F;    /* w2 */
    sizes[i++] = (size_t)L * D;        /* b2 */
    sizes[i++] = (size_t)D;            /* lnf_scale */
    sizes[i++] = (size_t)D;            /* lnf_bias  */

    for (int k = 0; k < 16; k++)
        total += sizes[k];
    return total;
}

static void point_params(LMParams *p, const LMConfig *c)
{
    size_t sizes[16];
    float *at = p->flat;
    float **fields[16];
    int i = 0;

    param_sizes(c, sizes);

    fields[i++] = &p->tok_emb;
    fields[i++] = &p->pos_emb;
    fields[i++] = &p->ln1_scale;
    fields[i++] = &p->ln1_bias;
    fields[i++] = &p->wq;
    fields[i++] = &p->wk;
    fields[i++] = &p->wv;
    fields[i++] = &p->wo;
    fields[i++] = &p->ln2_scale;
    fields[i++] = &p->ln2_bias;
    fields[i++] = &p->w1;
    fields[i++] = &p->b1;
    fields[i++] = &p->w2;
    fields[i++] = &p->b2;
    fields[i++] = &p->lnf_scale;
    fields[i++] = &p->lnf_bias;

    for (int k = 0; k < 16; k++) {
        *fields[k] = at;
        at += sizes[k];
    }
}

/* --------------------------------------------------------- activations */

/*
 * All the intermediate values one batch produces, kept because the backward
 * pass needs them. Same trick as the parameters: one flat block, pointers into
 * it.
 */
typedef struct {
    float *emb;        /* B*T*D */
    float *ln1, *ln1_mean, *ln1_rstd;
    float *q, *k, *v;
    float *att;        /* B*H*T*T */
    float *attout;     /* B*T*D  (weighted sum of v) */
    float *proj;       /* B*T*D  (after wo) */
    float *res1;       /* B*T*D */
    float *ln2, *ln2_mean, *ln2_rstd;
    float *fc1;        /* B*T*F  (after gelu) */
    float *fc1_raw;    /* B*T*F  (before gelu) */
    float *fc2;        /* B*T*D */
    float *res2;       /* B*T*D */
    float *lnf, *lnf_mean, *lnf_rstd;
    float *logits;     /* B*T*V */
    float *probs;      /* B*T*V */
} Acts;

static size_t act_sizes(const LMConfig *c, int B, size_t *sizes)
{
    int D = c->dim, L = c->layers, T = c->context, V = c->vocab, H = c->heads;
    int F = 4 * D;
    size_t BT = (size_t)B * T, LBT = (size_t)L * B * T, total = 0;
    int i = 0;

    sizes[i++] = BT * D;            /* emb */
    sizes[i++] = LBT * D;           /* ln1 */
    sizes[i++] = LBT;               /* ln1_mean */
    sizes[i++] = LBT;               /* ln1_rstd */
    sizes[i++] = LBT * D;           /* q */
    sizes[i++] = LBT * D;           /* k */
    sizes[i++] = LBT * D;           /* v */
    sizes[i++] = (size_t)L * B * H * T * T;  /* att */
    sizes[i++] = LBT * D;           /* attout */
    sizes[i++] = LBT * D;           /* proj */
    sizes[i++] = LBT * D;           /* res1 */
    sizes[i++] = LBT * D;           /* ln2 */
    sizes[i++] = LBT;               /* ln2_mean */
    sizes[i++] = LBT;               /* ln2_rstd */
    sizes[i++] = LBT * F;           /* fc1 */
    sizes[i++] = LBT * F;           /* fc1_raw */
    sizes[i++] = LBT * D;           /* fc2 */
    sizes[i++] = LBT * D;           /* res2 */
    sizes[i++] = BT * D;            /* lnf */
    sizes[i++] = BT;                /* lnf_mean */
    sizes[i++] = BT;                /* lnf_rstd */
    sizes[i++] = BT * V;            /* logits */
    sizes[i++] = BT * V;            /* probs */

    for (int k = 0; k < 23; k++)
        total += sizes[k];
    return total;
}

static void point_acts(Acts *a, float *flat, const LMConfig *c, int B)
{
    size_t sizes[23];
    float *at = flat;
    float **fields[23];
    int i = 0;

    act_sizes(c, B, sizes);

    fields[i++] = &a->emb;
    fields[i++] = &a->ln1;      fields[i++] = &a->ln1_mean;
    fields[i++] = &a->ln1_rstd;
    fields[i++] = &a->q;        fields[i++] = &a->k;
    fields[i++] = &a->v;        fields[i++] = &a->att;
    fields[i++] = &a->attout;   fields[i++] = &a->proj;
    fields[i++] = &a->res1;
    fields[i++] = &a->ln2;      fields[i++] = &a->ln2_mean;
    fields[i++] = &a->ln2_rstd;
    fields[i++] = &a->fc1;      fields[i++] = &a->fc1_raw;
    fields[i++] = &a->fc2;      fields[i++] = &a->res2;
    fields[i++] = &a->lnf;      fields[i++] = &a->lnf_mean;
    fields[i++] = &a->lnf_rstd;
    fields[i++] = &a->logits;   fields[i++] = &a->probs;

    for (int k = 0; k < 23; k++) {
        *fields[k] = at;
        at += sizes[k];
    }
}

/* ------------------------------------------------------------- pieces */

/* out = inp @ weight^T + bias, where weight is (OC, C) row major. */
static void matmul_forward(float *out, const float *inp, const float *weight,
                           const float *bias, int BT, int C, int OC)
{
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < BT; i++) {
        const float *row = inp + (size_t)i * C;
        float *dest = out + (size_t)i * OC;
        for (int o = 0; o < OC; o++) {
            const float *w = weight + (size_t)o * C;
            float sum = bias ? bias[o] : 0.0f;
            for (int c = 0; c < C; c++)
                sum += row[c] * w[c];
            dest[o] = sum;
        }
    }
}

static void matmul_backward(float *dinp, float *dweight, float *dbias,
                            const float *dout, const float *inp,
                            const float *weight, int BT, int C, int OC)
{
    /* gradient into the input: one independent row at a time */
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < BT; i++) {
        const float *grad = dout + (size_t)i * OC;
        float *dest = dinp + (size_t)i * C;
        for (int o = 0; o < OC; o++) {
            const float *w = weight + (size_t)o * C;
            float g = grad[o];
            for (int c = 0; c < C; c++)
                dest[c] += g * w[c];
        }
    }

    /* gradient into the weights: split over output channels so threads do not
       write to the same place */
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int o = 0; o < OC; o++) {
        float *dw = dweight + (size_t)o * C;
        float sum = 0.0f;
        for (int i = 0; i < BT; i++) {
            float g = dout[(size_t)i * OC + o];
            const float *row = inp + (size_t)i * C;
            sum += g;
            for (int c = 0; c < C; c++)
                dw[c] += g * row[c];
        }
        if (dbias)
            dbias[o] += sum;
    }
}

static void layernorm_forward(float *out, float *mean_out, float *rstd_out,
                              const float *inp, const float *scale,
                              const float *bias, int BT, int C)
{
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < BT; i++) {
        const float *row = inp + (size_t)i * C;
        float *dest = out + (size_t)i * C;
        float mean = 0.0f, var = 0.0f;

        for (int c = 0; c < C; c++)
            mean += row[c];
        mean /= C;
        for (int c = 0; c < C; c++) {
            float d = row[c] - mean;
            var += d * d;
        }
        var /= C;

        float rstd = 1.0f / sqrtf(var + 1e-5f);
        for (int c = 0; c < C; c++)
            dest[c] = (row[c] - mean) * rstd * scale[c] + bias[c];

        mean_out[i] = mean;
        rstd_out[i] = rstd;
    }
}

static void layernorm_backward(float *dinp, float *dscale, float *dbias,
                               const float *dout, const float *inp,
                               const float *scale, const float *mean,
                               const float *rstd, int BT, int C)
{
    for (int i = 0; i < BT; i++) {
        const float *grad = dout + (size_t)i * C;
        const float *row = inp + (size_t)i * C;
        float *dest = dinp + (size_t)i * C;
        float m = mean[i], r = rstd[i];
        float dnorm_mean = 0.0f, dnorm_norm_mean = 0.0f;

        for (int c = 0; c < C; c++) {
            float norm = (row[c] - m) * r;
            float dnorm = scale[c] * grad[c];
            dnorm_mean += dnorm;
            dnorm_norm_mean += dnorm * norm;
        }
        dnorm_mean /= C;
        dnorm_norm_mean /= C;

        for (int c = 0; c < C; c++) {
            float norm = (row[c] - m) * r;
            float dnorm = scale[c] * grad[c];
            dscale[c] += norm * grad[c];
            dbias[c] += grad[c];
            dest[c] += (dnorm - dnorm_mean - norm * dnorm_norm_mean) * r;
        }
    }
}

#define GELU_C 0.7978845608028654f   /* sqrt(2/pi) */

static void gelu_forward(float *out, const float *inp, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        float x = inp[i];
        float inner = GELU_C * (x + 0.044715f * x * x * x);
        out[i] = 0.5f * x * (1.0f + tanhf(inner));
    }
}

static void gelu_backward(float *dinp, const float *inp, const float *dout,
                          size_t n)
{
    for (size_t i = 0; i < n; i++) {
        float x = inp[i];
        float cube = 0.044715f * x * x * x;
        float inner = GELU_C * (x + cube);
        float tanh_inner = tanhf(inner);
        float sech2 = 1.0f - tanh_inner * tanh_inner;
        float local = 0.5f * (1.0f + tanh_inner)
                    + 0.5f * x * sech2 * GELU_C * (1.0f + 3.0f * 0.044715f * x * x);
        dinp[i] += local * dout[i];
    }
}

/*
 * Causal self attention. Each position may look at itself and everything
 * before it, never ahead -- that restriction is the whole reason a language
 * model can be trained to predict the next word.
 */
static void attention_forward(float *out, float *att, const float *q,
                              const float *k, const float *v,
                              int B, int T, int D, int H)
{
    int hd = D / H;
    float scale = 1.0f / sqrtf((float)hd);

#ifdef _OPENMP
#pragma omp parallel for collapse(2) schedule(static)
#endif
    for (int b = 0; b < B; b++) {
        for (int h = 0; h < H; h++) {
            for (int t = 0; t < T; t++) {
                const float *qq = q + ((size_t)b * T + t) * D + h * hd;
                float *scores = att + (((size_t)b * H + h) * T + t) * T;
                float max = -1e30f, sum = 0.0f;

                for (int s = 0; s <= t; s++) {
                    const float *kk = k + ((size_t)b * T + s) * D + h * hd;
                    float dot = 0.0f;
                    for (int c = 0; c < hd; c++)
                        dot += qq[c] * kk[c];
                    dot *= scale;
                    scores[s] = dot;
                    if (dot > max)
                        max = dot;
                }
                for (int s = 0; s <= t; s++) {
                    scores[s] = expf(scores[s] - max);
                    sum += scores[s];
                }
                for (int s = 0; s <= t; s++)
                    scores[s] /= sum;
                for (int s = t + 1; s < T; s++)
                    scores[s] = 0.0f;

                float *dest = out + ((size_t)b * T + t) * D + h * hd;
                for (int c = 0; c < hd; c++)
                    dest[c] = 0.0f;
                for (int s = 0; s <= t; s++) {
                    const float *vv = v + ((size_t)b * T + s) * D + h * hd;
                    float weight = scores[s];
                    for (int c = 0; c < hd; c++)
                        dest[c] += weight * vv[c];
                }
            }
        }
    }
}

static void attention_backward(float *dq, float *dk, float *dv,
                               const float *dout, const float *att,
                               const float *q, const float *k, const float *v,
                               int B, int T, int D, int H)
{
    int hd = D / H;
    float scale = 1.0f / sqrtf((float)hd);

#ifdef _OPENMP
#pragma omp parallel for collapse(2) schedule(static)
#endif
    for (int b = 0; b < B; b++) {
        for (int h = 0; h < H; h++) {
            float *datt = (float *)malloc(sizeof(float) * (size_t)T);
            if (datt == NULL)
                continue;

            for (int t = 0; t < T; t++) {
                const float *scores = att + (((size_t)b * H + h) * T + t) * T;
                const float *grad = dout + ((size_t)b * T + t) * D + h * hd;

                /* through the weighted sum of v */
                for (int s = 0; s <= t; s++) {
                    const float *vv = v + ((size_t)b * T + s) * D + h * hd;
                    float *dvv = dv + ((size_t)b * T + s) * D + h * hd;
                    float dot = 0.0f;
                    for (int c = 0; c < hd; c++) {
                        dot += grad[c] * vv[c];
                        dvv[c] += scores[s] * grad[c];
                    }
                    datt[s] = dot;
                }

                /* through the softmax */
                float weighted = 0.0f;
                for (int s = 0; s <= t; s++)
                    weighted += datt[s] * scores[s];
                for (int s = 0; s <= t; s++)
                    datt[s] = scores[s] * (datt[s] - weighted);

                /* through the dot products */
                const float *qq = q + ((size_t)b * T + t) * D + h * hd;
                float *dqq = dq + ((size_t)b * T + t) * D + h * hd;
                for (int s = 0; s <= t; s++) {
                    const float *kk = k + ((size_t)b * T + s) * D + h * hd;
                    float *dkk = dk + ((size_t)b * T + s) * D + h * hd;
                    float g = datt[s] * scale;
                    for (int c = 0; c < hd; c++) {
                        dqq[c] += g * kk[c];
                        dkk[c] += g * qq[c];
                    }
                }
            }
            free(datt);
        }
    }
}

/* ------------------------------------------------------------ the model */

static float randn(unsigned *seed)
{
    /* Box-Muller, good enough for initial weights */
    float u1, u2;
    do {
        *seed = *seed * 1664525u + 1013904223u;
        u1 = (float)((*seed >> 8) & 0xFFFFFF) / (float)0x1000000;
    } while (u1 <= 1e-7f);
    *seed = *seed * 1664525u + 1013904223u;
    u2 = (float)((*seed >> 8) & 0xFFFFFF) / (float)0x1000000;
    return sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2);
}

int lm_init(LM *lm, LMConfig config, int batch, unsigned seed)
{
    size_t sizes[16];
    size_t count = param_sizes(&config, sizes);
    size_t acts[23];

    memset(lm, 0, sizeof *lm);
    lm->config = config;
    lm->batch = batch;

    lm->params.count = lm->grads.count = count;
    lm->params.flat = (float *)calloc(count, sizeof(float));
    lm->grads.flat = (float *)calloc(count, sizeof(float));
    lm->adam_m = (float *)calloc(count, sizeof(float));
    lm->adam_v = (float *)calloc(count, sizeof(float));
    if (!lm->params.flat || !lm->grads.flat || !lm->adam_m || !lm->adam_v)
        return -1;

    point_params(&lm->params, &config);
    point_params(&lm->grads, &config);

    /* small random weights; layer norms start as identity */
    for (size_t i = 0; i < sizes[0] + sizes[1]; i++)
        lm->params.flat[i] = randn(&seed) * 0.02f;
    for (int l = 0; l < config.layers; l++) {
        int D = config.dim, F = 4 * D;
        for (int i = 0; i < D; i++) {
            lm->params.ln1_scale[l * D + i] = 1.0f;
            lm->params.ln2_scale[l * D + i] = 1.0f;
        }
        for (int i = 0; i < D * D; i++) {
            float s = 0.02f;
            lm->params.wq[(size_t)l * D * D + i] = randn(&seed) * s;
            lm->params.wk[(size_t)l * D * D + i] = randn(&seed) * s;
            lm->params.wv[(size_t)l * D * D + i] = randn(&seed) * s;
            /* output projections start smaller, which keeps deep stacks calm */
            lm->params.wo[(size_t)l * D * D + i] =
                randn(&seed) * s / sqrtf(2.0f * config.layers);
        }
        for (int i = 0; i < F * D; i++)
            lm->params.w1[(size_t)l * F * D + i] = randn(&seed) * 0.02f;
        for (int i = 0; i < D * F; i++)
            lm->params.w2[(size_t)l * D * F + i] =
                randn(&seed) * 0.02f / sqrtf(2.0f * config.layers);
    }
    for (int i = 0; i < config.dim; i++)
        lm->params.lnf_scale[i] = 1.0f;

    lm->acts_count = act_sizes(&config, batch, acts);
    lm->acts = (float *)calloc(lm->acts_count * 2, sizeof(float));
    if (lm->acts == NULL)
        return -1;

    return 0;
}

void lm_free(LM *lm)
{
    free(lm->params.flat);
    free(lm->grads.flat);
    free(lm->adam_m);
    free(lm->adam_v);
    free(lm->acts);
    memset(lm, 0, sizeof *lm);
}

/*
 * The logits row for one (batch, position) pair from whatever lm_forward
 * last computed. Exists so the shape of Acts stays private to this file
 * even for callers -- like the cache correctness check -- that only want to
 * read out one already-computed result, not touch the internals directly.
 */
void lm_read_logits(LM *lm, int batch_index, int position, float *out)
{
    Acts a;
    point_acts(&a, lm->acts, &lm->config, lm->batch);
    memcpy(out,
           a.logits + ((size_t)batch_index * lm->config.context + position)
                      * (size_t)lm->config.vocab,
           sizeof(float) * (size_t)lm->config.vocab);
}

float lm_forward(LM *lm, const unsigned short *inputs,
                 const unsigned short *targets, int do_backward)
{
    LMConfig *c = &lm->config;
    LMParams *p = &lm->params, *g = &lm->grads;
    int B = lm->batch, T = c->context, D = c->dim, V = c->vocab;
    int L = c->layers, H = c->heads, F = 4 * D;
    size_t BT = (size_t)B * T;
    Acts a, da;
    float loss = 0.0f;

    point_acts(&a, lm->acts, c, B);
    point_acts(&da, lm->acts + lm->acts_count, c, B);
    if (do_backward)
        memset(lm->acts + lm->acts_count, 0, lm->acts_count * sizeof(float));

    /* ---- embeddings ---- */
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            unsigned short token = inputs[(size_t)b * T + t];
            const float *tok = p->tok_emb + (size_t)token * D;
            const float *pos = p->pos_emb + (size_t)t * D;
            float *dest = a.emb + ((size_t)b * T + t) * D;
            for (int i = 0; i < D; i++)
                dest[i] = tok[i] + pos[i];
        }
    }

    /* ---- the blocks ---- */
    for (int l = 0; l < L; l++) {
        const float *x = (l == 0) ? a.emb : a.res2 + (size_t)(l - 1) * BT * D;
        size_t off = (size_t)l * BT * D;
        size_t offF = (size_t)l * BT * F;
        size_t offA = (size_t)l * B * H * T * T;

        layernorm_forward(a.ln1 + off, a.ln1_mean + (size_t)l * BT,
                          a.ln1_rstd + (size_t)l * BT, x,
                          p->ln1_scale + (size_t)l * D,
                          p->ln1_bias + (size_t)l * D, B * T, D);

        matmul_forward(a.q + off, a.ln1 + off, p->wq + (size_t)l * D * D,
                       NULL, B * T, D, D);
        matmul_forward(a.k + off, a.ln1 + off, p->wk + (size_t)l * D * D,
                       NULL, B * T, D, D);
        matmul_forward(a.v + off, a.ln1 + off, p->wv + (size_t)l * D * D,
                       NULL, B * T, D, D);

        attention_forward(a.attout + off, a.att + offA, a.q + off, a.k + off,
                          a.v + off, B, T, D, H);
        matmul_forward(a.proj + off, a.attout + off, p->wo + (size_t)l * D * D,
                       NULL, B * T, D, D);

        for (size_t i = 0; i < BT * D; i++)
            a.res1[off + i] = x[i] + a.proj[off + i];

        layernorm_forward(a.ln2 + off, a.ln2_mean + (size_t)l * BT,
                          a.ln2_rstd + (size_t)l * BT, a.res1 + off,
                          p->ln2_scale + (size_t)l * D,
                          p->ln2_bias + (size_t)l * D, B * T, D);

        matmul_forward(a.fc1_raw + offF, a.ln2 + off, p->w1 + (size_t)l * F * D,
                       p->b1 + (size_t)l * F, B * T, D, F);
        gelu_forward(a.fc1 + offF, a.fc1_raw + offF, BT * F);
        matmul_forward(a.fc2 + off, a.fc1 + offF, p->w2 + (size_t)l * D * F,
                       p->b2 + (size_t)l * D, B * T, F, D);

        for (size_t i = 0; i < BT * D; i++)
            a.res2[off + i] = a.res1[off + i] + a.fc2[off + i];
    }

    /* ---- output ---- */
    layernorm_forward(a.lnf, a.lnf_mean, a.lnf_rstd,
                      a.res2 + (size_t)(L - 1) * BT * D,
                      p->lnf_scale, p->lnf_bias, B * T, D);
    matmul_forward(a.logits, a.lnf, p->tok_emb, NULL, B * T, D, V);

    /* softmax and cross entropy */
#ifdef _OPENMP
#pragma omp parallel for schedule(static) reduction(+:loss)
#endif
    for (long i = 0; i < (long)BT; i++) {
        float *row = a.logits + (size_t)i * V;
        float *prob = a.probs + (size_t)i * V;
        float max = -1e30f, sum = 0.0f;

        for (int j = 0; j < V; j++)
            if (row[j] > max)
                max = row[j];
        for (int j = 0; j < V; j++) {
            prob[j] = expf(row[j] - max);
            sum += prob[j];
        }
        for (int j = 0; j < V; j++)
            prob[j] /= sum;

        if (targets != NULL)
            loss += -logf(prob[targets[(size_t)i]] + 1e-9f);
    }

    if (targets == NULL || !do_backward)
        return targets ? loss / (float)BT : 0.0f;

    /* ---- backward ---- */
    for (size_t i = 0; i < BT; i++) {
        float *dlogit = da.logits + i * V;
        const float *prob = a.probs + i * V;
        float scale = 1.0f / (float)BT;
        for (int j = 0; j < V; j++)
            dlogit[j] = (prob[j] - (j == targets[i] ? 1.0f : 0.0f)) * scale;
    }

    matmul_backward(da.lnf, g->tok_emb, NULL, da.logits, a.lnf, p->tok_emb,
                    B * T, D, V);
    layernorm_backward(da.res2 + (size_t)(L - 1) * BT * D, g->lnf_scale,
                       g->lnf_bias, da.lnf, a.res2 + (size_t)(L - 1) * BT * D,
                       p->lnf_scale, a.lnf_mean, a.lnf_rstd, B * T, D);

    for (int l = L - 1; l >= 0; l--) {
        const float *x = (l == 0) ? a.emb : a.res2 + (size_t)(l - 1) * BT * D;
        float *dx = (l == 0) ? da.emb : da.res2 + (size_t)(l - 1) * BT * D;
        size_t off = (size_t)l * BT * D;
        size_t offF = (size_t)l * BT * F;
        size_t offA = (size_t)l * B * H * T * T;

        /* through the second residual */
        for (size_t i = 0; i < BT * D; i++) {
            da.res1[off + i] += da.res2[off + i];
            da.fc2[off + i] += da.res2[off + i];
        }

        matmul_backward(da.fc1 + offF, g->w2 + (size_t)l * D * F,
                        g->b2 + (size_t)l * D, da.fc2 + off, a.fc1 + offF,
                        p->w2 + (size_t)l * D * F, B * T, F, D);
        gelu_backward(da.fc1_raw + offF, a.fc1_raw + offF, da.fc1 + offF, BT * F);
        matmul_backward(da.ln2 + off, g->w1 + (size_t)l * F * D,
                        g->b1 + (size_t)l * F, da.fc1_raw + offF, a.ln2 + off,
                        p->w1 + (size_t)l * F * D, B * T, D, F);

        layernorm_backward(da.res1 + off, g->ln2_scale + (size_t)l * D,
                           g->ln2_bias + (size_t)l * D, da.ln2 + off,
                           a.res1 + off, p->ln2_scale + (size_t)l * D,
                           a.ln2_mean + (size_t)l * BT,
                           a.ln2_rstd + (size_t)l * BT, B * T, D);

        /* through the first residual */
        for (size_t i = 0; i < BT * D; i++) {
            dx[i] += da.res1[off + i];
            da.proj[off + i] += da.res1[off + i];
        }

        matmul_backward(da.attout + off, g->wo + (size_t)l * D * D, NULL,
                        da.proj + off, a.attout + off,
                        p->wo + (size_t)l * D * D, B * T, D, D);
        attention_backward(da.q + off, da.k + off, da.v + off, da.attout + off,
                           a.att + offA, a.q + off, a.k + off, a.v + off,
                           B, T, D, H);

        matmul_backward(da.ln1 + off, g->wq + (size_t)l * D * D, NULL,
                        da.q + off, a.ln1 + off, p->wq + (size_t)l * D * D,
                        B * T, D, D);
        matmul_backward(da.ln1 + off, g->wk + (size_t)l * D * D, NULL,
                        da.k + off, a.ln1 + off, p->wk + (size_t)l * D * D,
                        B * T, D, D);
        matmul_backward(da.ln1 + off, g->wv + (size_t)l * D * D, NULL,
                        da.v + off, a.ln1 + off, p->wv + (size_t)l * D * D,
                        B * T, D, D);

        layernorm_backward(dx, g->ln1_scale + (size_t)l * D,
                           g->ln1_bias + (size_t)l * D, da.ln1 + off, x,
                           p->ln1_scale + (size_t)l * D,
                           a.ln1_mean + (size_t)l * BT,
                           a.ln1_rstd + (size_t)l * BT, B * T, D);
    }

    /* into the embedding tables */
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            unsigned short token = inputs[(size_t)b * T + t];
            const float *grad = da.emb + ((size_t)b * T + t) * D;
            float *dtok = g->tok_emb + (size_t)token * D;
            float *dpos = g->pos_emb + (size_t)t * D;
            for (int i = 0; i < D; i++) {
                dtok[i] += grad[i];
                dpos[i] += grad[i];
            }
        }
    }

    lm->tokens_seen += (double)BT;
    return loss / (float)BT;
}

void lm_step(LM *lm, float learning_rate, float weight_decay, float clip)
{
    size_t n = lm->params.count;
    float *w = lm->params.flat, *g = lm->grads.flat;
    double sum = 0.0;
    float scale = 1.0f;

    for (size_t i = 0; i < n; i++)
        sum += (double)g[i] * g[i];
    {
        float norm = (float)sqrt(sum);
        if (clip > 0.0f && norm > clip)
            scale = clip / (norm + 1e-6f);
    }

    lm->steps++;
    {
        float b1 = 0.9f, b2 = 0.95f, eps = 1e-8f;
        float c1 = 1.0f - powf(b1, (float)lm->steps);
        float c2 = 1.0f - powf(b2, (float)lm->steps);

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (long i = 0; i < (long)n; i++) {
            float grad = g[i] * scale;
            lm->adam_m[i] = b1 * lm->adam_m[i] + (1.0f - b1) * grad;
            lm->adam_v[i] = b2 * lm->adam_v[i] + (1.0f - b2) * grad * grad;
            float m = lm->adam_m[i] / c1;
            float v = lm->adam_v[i] / c2;
            w[i] -= learning_rate * (m / (sqrtf(v) + eps) + weight_decay * w[i]);
        }
    }

    memset(g, 0, n * sizeof(float));
}

/* ------------------------------------------------------------ saving */

int lm_save(const LM *lm, const char *path)
{
    FILE *file = fopen(path, "wb");
    unsigned magic = LM_MAGIC;
    if (file == NULL)
        return -1;

    fwrite(&magic, sizeof magic, 1, file);
    fwrite(&lm->config, sizeof lm->config, 1, file);
    fwrite(&lm->steps, sizeof lm->steps, 1, file);
    fwrite(&lm->tokens_seen, sizeof lm->tokens_seen, 1, file);
    fwrite(lm->params.flat, sizeof(float), lm->params.count, file);
    fwrite(lm->adam_m, sizeof(float), lm->params.count, file);
    fwrite(lm->adam_v, sizeof(float), lm->params.count, file);
    fclose(file);
    return 0;
}

int lm_load(LM *lm, const char *path, int batch)
{
    FILE *file = fopen(path, "rb");
    unsigned magic = 0;
    LMConfig config;
    long steps = 0;
    double seen = 0;

    if (file == NULL)
        return -1;
    if (fread(&magic, sizeof magic, 1, file) != 1 || magic != LM_MAGIC) {
        fclose(file);
        return -2;
    }
    if (fread(&config, sizeof config, 1, file) != 1 ||
        fread(&steps, sizeof steps, 1, file) != 1 ||
        fread(&seen, sizeof seen, 1, file) != 1) {
        fclose(file);
        return -2;
    }

    if (lm_init(lm, config, batch, 1234u) != 0) {
        fclose(file);
        return -1;
    }
    if (fread(lm->params.flat, sizeof(float), lm->params.count, file)
        != lm->params.count) {
        fclose(file);
        return -2;
    }
    /* optimiser state is optional: an older or trimmed file still loads */
    if (fread(lm->adam_m, sizeof(float), lm->params.count, file)
        == lm->params.count)
        if (fread(lm->adam_v, sizeof(float), lm->params.count, file) == 0)
            memset(lm->adam_v, 0, lm->params.count * sizeof(float));

    lm->steps = steps;
    lm->tokens_seen = seen;
    fclose(file);
    return 0;
}

/* ------------------------------------------------------------ the cache */

/*
 * Everything one token needs as it passes through one layer, sized D or F
 * (4*D), carved out of one flat allocation the same way params/acts are.
 * Reused for every layer in turn, since a decode step handles one token at a
 * time, one layer at a time -- there is never more than one layer's worth of
 * this live at once.
 */
typedef struct {
    float *x, *ln1, *q, *k, *v, *attn_out, *proj, *res1;
    float *ln2, *fc1_raw, *fc1, *fc2, *res2, *lnf;
    float *scores;   /* softmax buffer, one per cached position */
    float stat[6];   /* the six mean/rstd scalars layernorm_forward wants */
} DecodeScratch;

static size_t decode_scratch_floats(const LMConfig *c)
{
    int D = c->dim, F = 4 * D;
    /* x, ln1, q, k, v, attn_out, proj, res1, ln2, fc2, res2, lnf: 12 of them */
    return (size_t)12 * D + (size_t)2 * F + (size_t)c->context;
}

static void point_decode_scratch(DecodeScratch *s, float *flat, const LMConfig *c)
{
    int D = c->dim, F = 4 * D;
    float *at = flat;

    s->x        = at; at += D;
    s->ln1      = at; at += D;
    s->q        = at; at += D;
    s->k        = at; at += D;
    s->v        = at; at += D;
    s->attn_out = at; at += D;
    s->proj     = at; at += D;
    s->res1     = at; at += D;
    s->ln2      = at; at += D;
    s->fc1_raw  = at; at += F;
    s->fc1      = at; at += F;
    s->fc2      = at; at += D;
    s->res2     = at; at += D;
    s->lnf      = at; at += D;
    s->scores   = at; at += c->context;
}

int lm_cache_init(LMCache *cache, const LMConfig *config)
{
    size_t kv = (size_t)config->layers * config->context * config->dim;

    memset(cache, 0, sizeof *cache);
    cache->k = (float *)calloc(kv, sizeof(float));
    cache->v = (float *)calloc(kv, sizeof(float));
    cache->scratch = (float *)calloc(decode_scratch_floats(config), sizeof(float));
    cache->tokens = (unsigned short *)calloc((size_t)config->context, sizeof(unsigned short));
    if (cache->k == NULL || cache->v == NULL || cache->scratch == NULL ||
        cache->tokens == NULL) {
        lm_cache_free(cache);
        return -1;
    }
    cache->length = 0;
    return 0;
}

void lm_cache_free(LMCache *cache)
{
    free(cache->k);
    free(cache->v);
    free(cache->scratch);
    free(cache->tokens);
    memset(cache, 0, sizeof *cache);
}

/*
 * One token's query attending over every cached key/value up to and
 * including its own (just-appended) one. Same maths as attention_forward's
 * inner loop, specialised to a single query instead of a whole batch.
 */
static void attention_decode_step(float *out, float *scores,
                                  const float *cache_k, const float *cache_v,
                                  const float *q, int length, int D, int H)
{
    int hd = D / H;
    float scale = 1.0f / sqrtf((float)hd);

    for (int h = 0; h < H; h++) {
        const float *qq = q + h * hd;
        float max = -1e30f, sum = 0.0f;

        for (int s = 0; s < length; s++) {
            const float *kk = cache_k + (size_t)s * D + h * hd;
            float dot = 0.0f;
            for (int c = 0; c < hd; c++)
                dot += qq[c] * kk[c];
            dot *= scale;
            scores[s] = dot;
            if (dot > max)
                max = dot;
        }
        for (int s = 0; s < length; s++) {
            scores[s] = expf(scores[s] - max);
            sum += scores[s];
        }

        float *dest = out + h * hd;
        for (int c = 0; c < hd; c++)
            dest[c] = 0.0f;
        for (int s = 0; s < length; s++) {
            const float *vv = cache_v + (size_t)s * D + h * hd;
            float weight = scores[s] / sum;
            for (int c = 0; c < hd; c++)
                dest[c] += weight * vv[c];
        }
    }
}

/* One token through one layer, using and extending that layer's cache. */
static void decode_layer(LM *lm, int layer, DecodeScratch *s,
                         float *cache_k, float *cache_v, int length)
{
    LMConfig *c = &lm->config;
    LMParams *p = &lm->params;
    int D = c->dim, H = c->heads, F = 4 * D;
    float *mean = &s->stat[0], *rstd = &s->stat[1];
    float *k_slot = cache_k + (size_t)length * D;
    float *v_slot = cache_v + (size_t)length * D;

    layernorm_forward(s->ln1, mean, rstd, s->x,
                      p->ln1_scale + (size_t)layer * D,
                      p->ln1_bias + (size_t)layer * D, 1, D);

    matmul_forward(s->q, s->ln1, p->wq + (size_t)layer * D * D, NULL, 1, D, D);
    matmul_forward(k_slot, s->ln1, p->wk + (size_t)layer * D * D, NULL, 1, D, D);
    matmul_forward(v_slot, s->ln1, p->wv + (size_t)layer * D * D, NULL, 1, D, D);

    attention_decode_step(s->attn_out, s->scores, cache_k, cache_v, s->q,
                          length + 1, D, H);
    matmul_forward(s->proj, s->attn_out, p->wo + (size_t)layer * D * D,
                  NULL, 1, D, D);

    for (int i = 0; i < D; i++)
        s->res1[i] = s->x[i] + s->proj[i];

    layernorm_forward(s->ln2, mean + 2, rstd + 2, s->res1,
                      p->ln2_scale + (size_t)layer * D,
                      p->ln2_bias + (size_t)layer * D, 1, D);

    matmul_forward(s->fc1_raw, s->ln2, p->w1 + (size_t)layer * F * D,
                  p->b1 + (size_t)layer * F, 1, D, F);
    gelu_forward(s->fc1, s->fc1_raw, (size_t)F);
    matmul_forward(s->fc2, s->fc1, p->w2 + (size_t)layer * D * F,
                  p->b2 + (size_t)layer * D, 1, F, D);

    for (int i = 0; i < D; i++)
        s->res2[i] = s->res1[i] + s->fc2[i];
}

/* Shared tail end of prefill and decode: one token's final residual stream
 * value in, its logits out. */
static void decode_finish(LM *lm, DecodeScratch *s, const float *residual,
                          float *logits_out)
{
    LMConfig *c = &lm->config;
    LMParams *p = &lm->params;
    float *mean = &s->stat[4], *rstd = &s->stat[5];

    layernorm_forward(s->lnf, mean, rstd, residual,
                      p->lnf_scale, p->lnf_bias, 1, c->dim);
    matmul_forward(logits_out, s->lnf, p->tok_emb, NULL, 1, c->dim, c->vocab);
}

void lm_prefill(LM *lm, const unsigned short *tokens, int count,
               LMCache *cache, float *logits_out)
{
    LMConfig *c = &lm->config;
    DecodeScratch s;

    point_decode_scratch(&s, cache->scratch, c);
    cache->length = 0;

    for (int t = 0; t < count; t++) {
        const float *tok = lm->params.tok_emb + (size_t)tokens[t] * c->dim;
        const float *pos = lm->params.pos_emb + (size_t)t * c->dim;

        cache->tokens[t] = tokens[t];
        for (int i = 0; i < c->dim; i++)
            s.x[i] = tok[i] + pos[i];

        for (int l = 0; l < c->layers; l++) {
            decode_layer(lm, l, &s,
                        cache->k + (size_t)l * c->context * c->dim,
                        cache->v + (size_t)l * c->context * c->dim,
                        cache->length);
            memcpy(s.x, s.res2, sizeof(float) * (size_t)c->dim);
        }
        cache->length++;

        if (t == count - 1)
            decode_finish(lm, &s, s.x, logits_out);
    }
}

void lm_decode_step(LM *lm, unsigned short token, LMCache *cache,
                    float *logits_out)
{
    LMConfig *c = &lm->config;
    DecodeScratch s;
    int position;

    if (cache->length >= c->context) {
        /* The window is full: an absolute position baked into a cached
         * key/value can't just slide down with a memmove and stay correct,
         * so rebuild the whole cache in one pass instead, using exactly the
         * same last-context-tokens window lm_generate always used before
         * caching existed. Once a conversation is longer than the context
         * window this branch is taken on every step (there is no room left
         * to grow into, so each new token evicts one), which is the honest
         * O(context)-per-token cost the old method always paid; the cache
         * only saves work while the conversation still fits inside the
         * context window without evicting. */
        memmove(cache->tokens, cache->tokens + 1,
                sizeof(unsigned short) * (size_t)(c->context - 1));
        cache->tokens[c->context - 1] = token;
        lm_prefill(lm, cache->tokens, c->context, cache, logits_out);
        return;
    }

    point_decode_scratch(&s, cache->scratch, c);
    position = cache->length;
    cache->tokens[position] = token;

    {
        const float *tok = lm->params.tok_emb + (size_t)token * c->dim;
        const float *pos = lm->params.pos_emb + (size_t)position * c->dim;
        for (int i = 0; i < c->dim; i++)
            s.x[i] = tok[i] + pos[i];
    }

    for (int l = 0; l < c->layers; l++) {
        decode_layer(lm, l, &s,
                    cache->k + (size_t)l * c->context * c->dim,
                    cache->v + (size_t)l * c->context * c->dim,
                    position);
        memcpy(s.x, s.res2, sizeof(float) * (size_t)c->dim);
    }
    cache->length = position + 1;

    decode_finish(lm, &s, s.x, logits_out);
}

/* --------------------------------------------------------- generating */

int lm_generate(LM *lm, const unsigned short *prompt, int prompt_len,
                unsigned short *out, int max_new,
                float temperature, int top_k, float repeat_penalty,
                const unsigned short *stops, int stop_count,
                unsigned *seed, float *out_logprob)
{
    LMConfig *c = &lm->config;
    int T = c->context, V = c->vocab;
    unsigned short *window = (unsigned short *)calloc((size_t)T, sizeof(unsigned short));
    float *scratch = (float *)malloc(sizeof(float) * (size_t)V);
    LMCache cache;
    int produced = 0;
    float logprob_total = 0.0f;

    if (window == NULL || scratch == NULL || lm->batch != 1 ||
        lm_cache_init(&cache, c) != 0) {
        free(window);      /* generation runs one sequence at a time */
        free(scratch);
        return 0;
    }

    /* the model can only see the last T tokens; keep the most recent ones.
     * `window` is now bookkeeping only (for the repeat penalty and the
     * eviction-timing check below) -- the actual computation lives in the
     * cache, built once by lm_prefill and then extended one token at a time
     * by lm_decode_step, instead of recomputing the whole window from
     * scratch on every single generated token. */
    int start = prompt_len > T ? prompt_len - T : 0;
    int len = prompt_len - start;
    for (int i = 0; i < len; i++)
        window[i] = prompt[start + i];

    lm_prefill(lm, window, len, &cache, scratch);

    for (int step = 0; step < max_new; step++) {
        /* never invent the unknown-word token */
        scratch[TOK_UNK] = -1e30f;

        /* discourage saying the same word over and over */
        if (repeat_penalty > 1.0f) {
            for (int i = 0; i < len; i++) {
                float value = scratch[window[i]];
                scratch[window[i]] = value > 0 ? value / repeat_penalty
                                               : value * repeat_penalty;
            }
        }

        int choice = 0;
        if (temperature <= 0.001f) {
            float best = -1e30f;
            for (int j = 0; j < V; j++)
                if (scratch[j] > best) {
                    best = scratch[j];
                    choice = j;
                }
        } else {
            float max = -1e30f, sum = 0.0f, cutoff = -1e30f;

            for (int j = 0; j < V; j++)
                scratch[j] /= temperature;

            if (top_k > 0 && top_k < V) {
                /* find the k-th largest by repeated max, k is small */
                float *copy = (float *)malloc(sizeof(float) * (size_t)top_k);
                if (copy != NULL) {
                    for (int i = 0; i < top_k; i++)
                        copy[i] = -1e30f;
                    for (int j = 0; j < V; j++) {
                        float value = scratch[j];
                        if (value <= copy[top_k - 1])
                            continue;
                        int i = top_k - 1;
                        while (i > 0 && copy[i - 1] < value) {
                            copy[i] = copy[i - 1];
                            i--;
                        }
                        copy[i] = value;
                    }
                    cutoff = copy[top_k - 1];
                    free(copy);
                }
            }

            for (int j = 0; j < V; j++)
                if (scratch[j] > max)
                    max = scratch[j];
            for (int j = 0; j < V; j++) {
                if (scratch[j] < cutoff) {
                    scratch[j] = 0.0f;
                    continue;
                }
                scratch[j] = expf(scratch[j] - max);
                sum += scratch[j];
            }

            *seed = *seed * 1664525u + 1013904223u;
            float pick = (float)((*seed >> 8) & 0xFFFFFF) / (float)0x1000000 * sum;
            float running = 0.0f;
            choice = V - 1;
            for (int j = 0; j < V; j++) {
                running += scratch[j];
                if (running >= pick) {
                    choice = j;
                    break;
                }
            }
            logprob_total += logf(scratch[choice] / (sum + 1e-9f) + 1e-9f);
        }

        for (int s = 0; s < stop_count; s++)
            if (choice == stops[s]) {
                lm_cache_free(&cache);
                free(window);
                free(scratch);
                if (out_logprob)
                    *out_logprob = produced ? logprob_total / produced : -99.0f;
                return produced;
            }

        out[produced++] = (unsigned short)choice;
        if (len < T) {
            window[len++] = (unsigned short)choice;
        } else {        /* full: shuffle everything back one and append */
            memmove(window, window + 1,
                    sizeof(unsigned short) * (size_t)(T - 1));
            window[T - 1] = (unsigned short)choice;
        }

        /* logits for the *next* step, extending the cache by this one token
         * instead of recomputing the whole window over again */
        lm_decode_step(lm, (unsigned short)choice, &cache, scratch);
    }

    lm_cache_free(&cache);
    free(window);
    free(scratch);
    if (out_logprob)
        *out_logprob = produced ? logprob_total / produced : -99.0f;
    return produced;
}
