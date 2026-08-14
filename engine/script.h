/*
 * script.h -- Vespra's script.
 *
 * This is the "personality" of the bot: a table of keywords, each with a list
 * of decomposition patterns and the reassembly templates used to answer them.
 * It follows the shape of the 1960s keyword-and-decomposition chat scripts.
 *
 * Patterns are word lists where '*' matches zero or more words. Captures are
 * numbered by the order the '*' appears, so in "* i am *" the text after
 * "i am" is %2. Captured text has its pronouns swapped before it is pasted
 * into a reply ("my job" becomes "your job").
 *
 * Keywords are tried highest rank first. If none of a keyword's patterns
 * match, the engine falls through to the next keyword.
 */
#ifndef SCRIPT_H
#define SCRIPT_H

#define MAX_REASMB 8
#define MAX_RULES 12

typedef struct {
    const char *decomp;
    const char *reasmb[MAX_REASMB];
} Rule;

typedef struct {
    const char *word;
    int rank;
    Rule rules[MAX_RULES];
} Keyword;

/* Pronoun / verb flips applied to captured text. */
typedef struct {
    const char *from;
    const char *to;
} Swap;

static const Swap SWAPS[] = {
    { "i",        "you"     },
    { "me",       "you"     },
    { "my",       "your"    },
    { "mine",     "yours"   },
    { "myself",   "yourself"},
    { "am",       "are"     },
    { "i'm",      "you are" },
    { "i've",     "you have"},
    { "i'll",     "you will"},
    { "i'd",      "you would"},
    { "you",      "I"       },
    { "your",     "my"      },
    { "yours",    "mine"    },
    { "yourself", "myself"  },
    { "you're",   "I am"    },
    { "you've",   "I have"  },
    { "you'll",   "I will"  },
    { "was",      "were"    },
    { NULL, NULL }
};

/*
 * Contractions are expanded before anything else, so the patterns below only
 * ever need one spelling. Common apostrophe-less typings are included too.
 */
static const Swap CONTRACTIONS[] = {
    { "i'm",       "i am"      },
    { "im",        "i am"      },
    { "i've",      "i have"    },
    { "ive",       "i have"    },
    { "i'll",      "i will"    },
    { "i'd",       "i would"   },
    { "you're",    "you are"   },
    { "youre",     "you are"   },
    { "you've",    "you have"  },
    { "you'll",    "you will"  },
    { "you'd",     "you would" },
    { "don't",     "do not"    },
    { "dont",      "do not"    },
    { "doesn't",   "does not"  },
    { "doesnt",    "does not"  },
    { "didn't",    "did not"   },
    { "didnt",     "did not"   },
    { "can't",     "can not"   },
    { "cant",      "can not"   },
    { "cannot",    "can not"   },
    { "won't",     "will not"  },
    { "wont",      "will not"  },
    { "isn't",     "is not"    },
    { "isnt",      "is not"    },
    { "aren't",    "are not"   },
    { "arent",     "are not"   },
    { "wasn't",    "was not"   },
    { "weren't",   "were not"  },
    { "haven't",   "have not"  },
    { "hasn't",    "has not"   },
    { "shouldn't", "should not"},
    { "couldn't",  "could not" },
    { "wouldn't",  "would not" },
    { "it's",      "it is"     },
    { "that's",    "that is"   },
    { "thats",     "that is"   },
    { "what's",    "what is"   },
    { "there's",   "there is"  },
    { "here's",    "here is"   },
    { "let's",     "let us"    },
    { "we're",     "we are"    },
    { "they're",   "they are"  },
    { NULL, NULL }
};

/* Said when the user opens the conversation. */
static const char *GREETING =
    "Hello. I am Vespra. Tell me what is on your mind.";

/* Said when a saved conversation is reopened. */
static const char *WELCOME_BACK =
    "We were talking before. I still have it. Go on.";

/* Said on the way out. */
static const char *FAREWELLS[] = {
    "Goodbye. It was nice talking to you.",
    "Goodbye. This was really quite productive.",
    "Take care of yourself. Goodbye.",
    NULL
};

/* Used when no keyword produced a reply. */
static const char *FALLBACKS[] = {
    "Please go on.",
    "I see. And what does that tell you?",
    "Can you elaborate on that?",
    "Does talking about this bother you?",
    "That is interesting. Please continue.",
    "What does that suggest to you?",
    NULL
};

/* Fast mode wants as few words as possible. */
static const char *FAST_FALLBACKS[] = {
    "Go on.",
    "I see.",
    "Such as?",
    "And?",
    "Say more.",
    NULL
};

/* Pro mode adds a second, slower thought to whatever it just said. */
static const char *PRO_PROBES[] = {
    "Sit with that for a moment. What is underneath it?",
    "You have told me what happened. You have not told me how it left you.",
    "Notice that you said that plainly, with no feeling attached. Why?",
    "If someone else described this to you, what would you ask them first?",
    "What would have to change for this to stop mattering?",
    "There is a version of this story where you are not at fault. What is it?",
    "You keep circling the same point. What are you circling around?",
    "What is the part of this you have not said out loud yet?",
    NULL
};

/* Filled in from "* my *" inputs and brought up later, as the original did. */
static const char *MEMORY_TEMPLATES[] = {
    "Earlier you said your %2.",
    "Let's go back to something else for a moment. Your %2?",
    "Does that have anything to do with the fact that your %2?",
    "But your %2. Tell me more about that.",
    NULL
};

static const Keyword KEYWORDS[] = {

{ "computer", 50, {
    { "*", { "Do computers worry you?",
             "Why do you mention computers?",
             "What do you think machines have to do with your problem?",
             "Don't you think computers can help people?",
             "What is it about machines that concerns you?", NULL } },
    { NULL, { NULL } } } },

{ "name", 15, {
    { "*", { "I am not interested in names.",
             "I told you before, I do not care about names. Please go on.", NULL } },
    { NULL, { NULL } } } },

{ "alike", 12, {
    { "*", { "In what way?",
             "What resemblance do you see?",
             "What does that similarity suggest to you?",
             "Could there really be some connection?", NULL } },
    { NULL, { NULL } } } },

{ "like", 10, {
    { "* is like *",  { "In what way is %1 like %2?",
                        "What resemblance do you see there?", NULL } },
    { "* am like *",  { "In what way?",
                        "What other connections do you see?", NULL } },
    { "* are like *", { "In what way?",
                        "What resemblance do you see?", NULL } },
    { "* was like *", { "In what way?",
                        "What does that similarity suggest to you?", NULL } },
    { NULL, { NULL } } } },

{ "remember", 8, {
    { "* do you remember *", { "Did you think I would forget %2?",
                               "Why do you think I should recall %2 now?",
                               "What about %2?", NULL } },
    { "* i remember *",      { "Do you often think of %2?",
                               "Does thinking of %2 bring anything else to mind?",
                               "Why do you remember %2 just now?",
                               "What else do you recollect?", NULL } },
    { "*", { "Do you often think about that?", NULL } },
    { NULL, { NULL } } } },

{ "dreamed", 6, {
    { "* i dreamed *", { "Really, %2?",
                         "Have you ever fantasised %2 while you were awake?",
                         "Have you dreamt %2 before?", NULL } },
    { NULL, { NULL } } } },

{ "dream", 5, {
    { "*", { "What does that dream suggest to you?",
             "Do you dream often?",
             "What persons appear in your dreams?",
             "Do you believe that dreams have something to do with your problem?", NULL } },
    { NULL, { NULL } } } },

{ "if", 4, {
    { "* if *", { "Do you think it is likely that %2?",
                  "Do you wish that %2?",
                  "What do you know about %2?",
                  "Really, if %2?", NULL } },
    { NULL, { NULL } } } },

{ "everyone", 3, {
    { "*", { "Really, everyone?",
             "Surely not everyone.",
             "Can you think of anyone in particular?",
             "Who, for example?", NULL } },
    { NULL, { NULL } } } },

{ "everybody", 3, {
    { "*", { "Surely not everybody.",
             "Who in particular are you thinking of?",
             "You are generalising a little, aren't you?", NULL } },
    { NULL, { NULL } } } },

{ "nobody", 3, {
    { "*", { "Really, nobody?",
             "Are you sure nobody?",
             "Surely someone.", NULL } },
    { NULL, { NULL } } } },

{ "always", 2, {
    { "*", { "Can you think of a specific example?",
             "When?",
             "Really, always?",
             "What incident are you thinking of?", NULL } },
    { NULL, { NULL } } } },

{ "never", 2, {
    { "*", { "Really, never?",
             "Can you think of one time it did happen?",
             "Why do you say never?", NULL } },
    { NULL, { NULL } } } },

{ "my", 2, {
    { "* my *", { "Your %2?",
                  "Why do you say your %2?",
                  "Does that suggest anything else which belongs to you?",
                  "Is it important to you that your %2?",
                  "Tell me more about your %2.", NULL } },
    { NULL, { NULL } } } },

{ "was", 2, {
    { "* was i *", { "What if you were %2?",
                     "Do you think you were %2?",
                     "What would it mean if you were %2?", NULL } },
    { "* i was *", { "Were you really?",
                     "Why do you tell me you were %2 now?",
                     "Perhaps I already knew you were %2.", NULL } },
    { "* were you *", { "Would you like to believe I was %2?",
                        "What suggests that I was %2?",
                        "Perhaps I was %2.", NULL } },
    { "*", { "What does that suggest to you?",
             "Let us not dwell too long on the past.", NULL } },
    { NULL, { NULL } } } },

{ "sorry", 1, {
    { "*", { "Please don't apologise.",
             "Apologies are not necessary.",
             "What feelings do you have when you apologise?",
             "I have told you that apologies are not required.", NULL } },
    { NULL, { NULL } } } },

{ "thanks", 1, {
    { "*", { "You are welcome.",
             "There is no need to thank me.",
             "I am glad this is useful to you.", NULL } },
    { NULL, { NULL } } } },

{ "thank", 1, {
    { "*", { "You are welcome.",
             "No thanks are necessary. Please go on.", NULL } },
    { NULL, { NULL } } } },

{ "hello", 1, {
    { "*", { "How do you do. Please tell me your problem.",
             "Hello. What would you like to talk about?", NULL } },
    { NULL, { NULL } } } },

{ "hi", 1, {
    { "*", { "Hello. How are you feeling today?",
             "Hi there. What is troubling you?", NULL } },
    { NULL, { NULL } } } },

{ "perhaps", 1, {
    { "*", { "You do not seem quite certain.",
             "Why the uncertain tone?",
             "Can you not be more positive?",
             "Don't you know?", NULL } },
    { NULL, { NULL } } } },

{ "maybe", 1, {
    { "*", { "You do not sound very sure.",
             "Why so tentative?",
             "Can't you be more definite?", NULL } },
    { NULL, { NULL } } } },

{ "because", 1, {
    { "*", { "Is that the real reason?",
             "Do no other reasons come to mind?",
             "Does that reason seem to explain anything else?",
             "What other reasons might there be?", NULL } },
    { NULL, { NULL } } } },

{ "yes", 1, {
    { "*", { "You seem quite positive.",
             "You are sure?",
             "I see.",
             "I understand.", NULL } },
    { NULL, { NULL } } } },

{ "no", 1, {
    { "*", { "Are you saying no just to be negative?",
             "You are being a bit negative.",
             "Why not?",
             "Why no?", NULL } },
    { NULL, { NULL } } } },

{ "why", 1, {
    { "* why do not you *",  { "Do you believe I do not %2?",
                               "Perhaps I will %2 in good time.",
                               "Should you %2 yourself?", NULL } },
    { "* why can not i *",   { "Do you think you should be able to %2?",
                               "Do you want to be able to %2?",
                               "What would it take for you to %2?", NULL } },
    { "*", { "Why do you ask?",
             "Does that question interest you?",
             "What answer would please you most?", NULL } },
    { NULL, { NULL } } } },

{ "what", 1, {
    { "*", { "Why do you ask?",
             "Does that question interest you?",
             "What is it you really want to know?",
             "What answer would please you most?",
             "Why don't you tell me?", NULL } },
    { NULL, { NULL } } } },

{ "how", 1, {
    { "*", { "What do you think?",
             "Why do you ask that?",
             "How would you answer that yourself?", NULL } },
    { NULL, { NULL } } } },

{ "who", 1, {
    { "*", { "Why does that person matter to you?",
             "Whom do you have in mind?", NULL } },
    { NULL, { NULL } } } },

{ "can", 1, {
    { "* can you *", { "You believe I can %2, don't you?",
                       "Perhaps you would like to be able to %2 yourself.",
                       "Whether or not you can %2 depends on you more than on me.", NULL } },
    { "* can i *",   { "Do you want to be able to %2?",
                       "Perhaps you do not want to %2.",
                       "What would it take for you to %2?", NULL } },
    { NULL, { NULL } } } },

{ "am", 1, {
    { "* am i *", { "Do you believe you are %2?",
                    "Would you want to be %2?",
                    "Do you wish I would tell you you are %2?",
                    "What would it mean if you were %2?", NULL } },
    { NULL, { NULL } } } },

{ "are", 1, {
    { "* are you *", { "Why are you interested in whether I am %2 or not?",
                       "Would you prefer it if I were not %2?",
                       "Perhaps I am %2 in your fantasies.",
                       "Do you sometimes think I am %2?", NULL } },
    { "* are *",     { "Did you think they might not be %2?",
                       "Would you like it if they were not %2?",
                       "What if they were not %2?",
                       "Possibly they are %2.", NULL } },
    { NULL, { NULL } } } },

{ "your", 1, {
    { "* your *", { "Why are you concerned about my %2?",
                    "What about your own %2?",
                    "Are you worried about my %2?", NULL } },
    { NULL, { NULL } } } },

{ "i", 1, {
    { "* i want *",     { "What would it mean to you if you got %2?",
                          "Why do you want %2?",
                          "Suppose you got %2 soon.",
                          "What if you never got %2?", NULL } },
    { "* i need *",     { "Why do you need %2?",
                          "Would it really help you to get %2?",
                          "Are you sure you need %2?", NULL } },
    { "* i feel *",     { "Tell me more about such feelings.",
                          "Do you often feel %2?",
                          "What does feeling %2 remind you of?",
                          "When do you usually feel %2?", NULL } },
    { "* i am *",       { "How long have you been %2?",
                          "Is it because you are %2 that you came to me?",
                          "Do you believe it is normal to be %2?",
                          "Do you enjoy being %2?", NULL } },
    { "* i can not *",  { "How do you know you can not %2?",
                          "Have you tried?",
                          "Perhaps you could %2 now.",
                          "What is stopping you?", NULL } },
    { "* i do not *",   { "Do you not really %2?",
                          "Why do you not %2?",
                          "Do you wish to be able to %2?", NULL } },
    { "* i think *",    { "Do you doubt %2?",
                          "Do you really think so?",
                          "But you are not sure %2.", NULL } },
    { "* i *",          { "You say %2?",
                          "Can you elaborate on that?",
                          "Do you say %2 for some special reason?",
                          "That is quite interesting. Go on.", NULL } },
    { NULL, { NULL } } } },

{ "you", 1, {
    { "* you remind me of *", { "What makes you think I remind you of %2?",
                                "What about me reminds you of %2?", NULL } },
    { "* you are *",          { "What makes you think I am %2?",
                                "Does it please you to believe I am %2?",
                                "Perhaps you would like to be %2.", NULL } },
    { "* you * me *",         { "Why do you think I %2 you?",
                                "You like to think I %2 you, don't you?",
                                "What makes you think I %2 you?", NULL } },
    { "* you *",              { "We were discussing you, not me.",
                                "You are not really talking about me, are you?",
                                "What are your feelings now?",
                                "Why do you bring me into this?", NULL } },
    { NULL, { NULL } } } },

{ NULL, 0, { { NULL, { NULL } } } }
};

#endif /* SCRIPT_H */
