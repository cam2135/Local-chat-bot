/*
 * codegen.h -- the one thing the 1960s chat scripts could not do:
 * hand you a small piece of Python, JavaScript, HTML or CSS when you ask for it.
 */
#ifndef CODEGEN_H
#define CODEGEN_H

#include <stddef.h>

/*
 * Look at a normalised input line (lower case, punctuation stripped) and, if it
 * is a request for code, write the whole reply into `out` and return 1.
 * Returns 0 when the line is ordinary conversation, so the chat script runs.
 */
int codegen_try(const char *input, char *out, size_t outsz);

/* The reply for "/code" with no language, and for the /help listing. */
void codegen_usage(char *out, size_t outsz);

#endif /* CODEGEN_H */
