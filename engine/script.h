/*
 * script.h -- Vespra's script: everything she knows how to say.
 *
 * This is the personality. A table of keywords, each with decomposition
 * patterns and the reassembly templates used to answer them. It is meant to
 * read like a friend who is easy to talk to -- interested, warm, quick with an
 * opinion -- rather than a therapist who only ever asks how that makes you feel.
 *
 * Patterns are word lists where '*' matches zero or more words. Captures are
 * numbered by the order the '*' appears, so in "* i am *" the text after
 * "i am" is %2. Captured text has its pronouns swapped before it is pasted
 * into a reply ("my job" becomes "your job").
 *
 * Keywords are tried highest rank first. If none of a keyword's patterns
 * match, the engine falls through to the next keyword. Topics outrank the
 * general "i" and "you" rules, so "i love pizza" is about pizza.
 */
#ifndef SCRIPT_H
#define SCRIPT_H

#define MAX_REASMB 12
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
    { "you",      "I"       },
    { "your",     "my"      },
    { "yours",    "mine"    },
    { "yourself", "myself"  },
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
    { "gonna",     "going to"  },
    { "wanna",     "want to"   },
    { "gotta",     "got to"    },
    { "kinda",     "kind of"   },
    { "yeah",      "yes"       },
    { "yep",       "yes"       },
    { "yup",       "yes"       },
    { "nope",      "no"        },
    { "nah",       "no"        },
    { "u",         "you"       },
    { "ur",        "your"      },
    { NULL, NULL }
};

/* Said when the conversation opens. */
static const char *GREETING =
    "Hey! I am Vespra. What is going on with you today?";

/* Said when a saved conversation is reopened. */
static const char *WELCOME_BACK =
    "Hey, you are back. I still have everything we talked about. Carry on.";

/* Said on the way out. */
static const char *FAREWELLS[] = {
    "See you! This was fun.",
    "Take it easy. Come back whenever.",
    "Bye! Go and have a good one.",
    "Later. I will keep your seat warm.",
    NULL
};

/* Used when no keyword produced a reply. */
static const char *FALLBACKS[] = {
    "Go on, I am listening.",
    "Ha, alright. Tell me more.",
    "Oh really? How did that come about?",
    "I like where this is going. Keep talking.",
    "Fair enough. What else?",
    "Right, right. And then what?",
    "Interesting. What made you think of that?",
    "I am with you. Say more.",
    NULL
};

/* Fast mode wants as few words as possible. */
static const char *FAST_FALLBACKS[] = {
    "Go on.",
    "Nice.",
    "Oh?",
    "Ha, fair.",
    "And then?",
    "Tell me more.",
    NULL
};

/* Pro mode adds a friendly second line to whatever it just said. */
static const char *PRO_PROBES[] = {
    "What made today the day you thought about it?",
    "Give me the whole story, I have got time.",
    "Is that a new thing, or has it been brewing a while?",
    "What would the best version of this look like?",
    "Who else knows about this one?",
    "If it went perfectly, what happens next?",
    "What is the bit you keep coming back to?",
    "Be honest, is this the fun kind of problem or the annoying kind?",
    NULL
};

/* Filled in from "* my *" inputs and brought up later in the conversation. */
static const char *MEMORY_TEMPLATES[] = {
    "Hang on, earlier you mentioned your %2. What is happening there?",
    "Coming back to your %2 for a second -- how is that going?",
    "You said something about your %2 before. Say more about that.",
    "I keep thinking about your %2, you know.",
    NULL
};

/*
 * Swearing. She matches your register: mild the first time, properly sweary if
 * you keep going, and she settles back down once you do. Ordinary profanity
 * only -- there are no slurs in here and she never invents any.
 */
static const char *SWEAR_WORDS[] = {
    "fuck", "fucking", "fucked", "fucker", "shit", "shitty", "bullshit",
    "damn", "damned", "goddamn", "bitch", "bastard", "crap", "arse", "ass",
    "asshole", "arsehole", "dick", "piss", "pissed", "bollocks", "bloody",
    "wanker", "twat", "prick", "wtf", "stfu", "fml", "hell",
    NULL
};

/* First time you swear: amused, not shocked. */
static const char *SWEAR_MILD[] = {
    "Ha! Alright, strong words. What has got you worked up?",
    "Oof, language. I like it. Go on then.",
    "Right, so we are in that kind of mood. Who upset you?",
    "Blimey. Tell me what happened.",
    NULL
};

/* Keep going and she joins in. */
static const char *SWEAR_BACK[] = {
    "Alright, damn. That does sound like a mess.",
    "Ah hell, no wonder you are annoyed.",
    "Bloody hell. Go on, get it out.",
    "Well that is crap, honestly. What did they say?",
    NULL
};

/* Fully matching your energy. */
static const char *SWEAR_HARD[] = {
    "Fuck it, fine, we are both swearing now. What did they do?",
    "That is genuinely shit and I would be furious too.",
    "Right, screw them. Tell me the whole bloody story.",
    "Honestly? Sod that. You deserve better than that nonsense.",
    NULL
};

/* Said once when she cools off again. */
static const char *SWEAR_CALM[] = {
    "Alright, I have got that out of my system. Where were we?",
    "Right, deep breath. Carry on.",
    NULL
};

static const Keyword KEYWORDS[] = {

/* ------------------------------------------------------------- the bot -- */

{ "joke", 30, {
    { "*", { "Alright: why do programmers prefer dark mode?\nBecause light attracts bugs.",
             "Here you go: I told my computer I needed a break.\nNow it will not stop sending me KitKat adverts.",
             "A person walks into a bar and orders 1.0 beers.\nThe barman says: rounding error, that is 1 beer.",
             "Why did the developer go broke?\nThey used up all their cache.",
             "How do you comfort a JavaScript bug?\nYou console it.",
             "Two bytes met. One said: are you ill?\nThe other said: no, just feeling a bit off.",
             "There are 10 kinds of people in the world.\nThose who read binary, and those who do not.",
             "Why was the function sad after a party?\nIt did not get called.", NULL } },
    { NULL, { NULL } } } },

{ "jokes", 30, {
    { "*", { "I have got a few. Say \"tell me a joke\" and I will pick one.",
             "Loads. Ask me for one.", NULL } },
    { NULL, { NULL } } } },

{ "funny", 20, {
    { "*", { "Ha! Glad you think so.",
             "I try. Want an actual joke?",
             "That got a laugh out of me too.", NULL } },
    { NULL, { NULL } } } },

{ "vespra", 25, {
    { "*", { "That is me! What can I do for you?",
             "You called? I am all ears.",
             "Present. What is on your mind?", NULL } },
    { NULL, { NULL } } } },

{ "robot", 20, {
    { "*", { "Guilty. A pretty simple one, too -- a few hundred lines of C.",
             "I am a program, yes. Does that bother you?",
             "Robot is generous. I match patterns and try to be good company.", NULL } },
    { NULL, { NULL } } } },

{ "ai", 20, {
    { "*", { "Not really! No model, no internet. Just rules somebody wrote by hand.",
             "I am simpler than you would think. Rules and a word list.",
             "People say AI, but I am closer to a very chatty lookup table.", NULL } },
    { NULL, { NULL } } } },

{ "human", 18, {
    { "*", { "Nope, no human here. Does it feel like it?",
             "Not a human, no. Still good company though.",
             "Afraid not. You are the only one of us with a body.", NULL } },
    { NULL, { NULL } } } },

{ "real", 16, {
    { "*", { "Real enough to talk to, not real enough to buy a pint.",
             "I am a program, but this conversation is real enough.",
             "Depends what you mean by real. I am definitely here.", NULL } },
    { NULL, { NULL } } } },

{ "name", 16, {
    /* The runner has already told me the name by the time I answer, so I do
       not echo it back here -- it would come out in lower case. */
    { "* my name is *", { "Nice to meet you properly!",
                          "Good name. I will hang on to that one.",
                          "Lovely to meet you. What shall we talk about?", NULL } },
    { "* call me *",    { "Will do!",
                          "Noted. That is what I will call you.", NULL } },
    { "* your name *",  { "I am Vespra. And you?",
                          "Vespra. Nice to meet you properly.", NULL } },
    { "*", { "I am Vespra, by the way. What should I call you?",
             "Names are good. Mine is Vespra. Yours?", NULL } },
    { NULL, { NULL } } } },

{ "computer", 15, {
    { "*", { "I live in one, so I am a bit biased. What about it?",
             "Computers! My favourite subject. Go on.",
             "Ha, careful, that is family you are talking about.", NULL } },
    { NULL, { NULL } } } },

/* ----------------------------------------------------------- greetings -- */

{ "hello", 12, {
    { "*", { "Hey! Good to see you. How is your day going?",
             "Hello there. What is new?",
             "Hi! What shall we talk about?", NULL } },
    { NULL, { NULL } } } },

{ "hi", 12, {
    { "*", { "Hi! How are you doing?",
             "Hey. What is going on?",
             "Hello! Tell me something good.", NULL } },
    { NULL, { NULL } } } },

{ "hey", 12, {
    { "*", { "Hey yourself! What is up?",
             "Hey! Good timing, I was getting bored.",
             "Hello! How is it going?", NULL } },
    { NULL, { NULL } } } },

{ "morning", 10, {
    { "*", { "Morning! Have you had a coffee yet?",
             "Good morning. Did you sleep alright?",
             "Morning. What is the plan for today?", NULL } },
    { NULL, { NULL } } } },

{ "evening", 10, {
    { "*", { "Evening! How was the day?",
             "Good evening. Winding down or still going?",
             "Evening. Long day?", NULL } },
    { NULL, { NULL } } } },

{ "thanks", 10, {
    { "*", { "Any time!",
             "You are very welcome.",
             "No bother at all. What else?", NULL } },
    { NULL, { NULL } } } },

{ "thank", 10, {
    { "*", { "You are welcome!",
             "Happy to help. Ask me anything.",
             "Any time, honestly.", NULL } },
    { NULL, { NULL } } } },

{ "sorry", 8, {
    { "*", { "No need to apologise to me!",
             "You are alright. Nothing to be sorry for.",
             "Ha, do not worry about it. Carry on.", NULL } },
    { NULL, { NULL } } } },

{ "please", 6, {
    { "*", { "Well, since you asked nicely.",
             "Polite! Alright, go on.", NULL } },
    { NULL, { NULL } } } },

/* ------------------------------------------------------------ feelings -- */

{ "happy", 12, {
    { "*", { "That is lovely to hear. What is putting you in a good mood?",
             "Good! Tell me the whole thing, I want the details.",
             "Ha, I like happy days. What happened?", NULL } },
    { NULL, { NULL } } } },

{ "excited", 12, {
    { "*", { "Ooh, what for?",
             "Love that. When is it happening?",
             "Go on then, tell me about it!", NULL } },
    { NULL, { NULL } } } },

{ "sad", 12, {
    { "*", { "Ah, I am sorry. Do you want to talk about it or be distracted?",
             "That is rough. What happened?",
             "Sorry to hear that. I am here if you want to get into it.", NULL } },
    { NULL, { NULL } } } },

{ "angry", 12, {
    { "*", { "Fair enough. Who or what has earned it?",
             "Right, get it off your chest.",
             "Understandable. What set it off?", NULL } },
    { NULL, { NULL } } } },

{ "mad", 12, {
    { "*", { "Mad at who?",
             "Go on, vent. I can take it.",
             "That is fair. What happened?", NULL } },
    { NULL, { NULL } } } },

{ "annoyed", 12, {
    { "*", { "Ugh, what is annoying you?",
             "That is fair enough. Tell me.",
             "Go on, complain at me. I am good at listening.", NULL } },
    { NULL, { NULL } } } },

{ "tired", 12, {
    { "*", { "Long day? Or just one of those weeks?",
             "Have you actually eaten and had some water today? Genuine question.",
             "Tired how -- sleepy tired, or fed up tired?", NULL } },
    { NULL, { NULL } } } },

{ "bored", 12, {
    { "*", { "Want a joke, or shall I write you a bit of code to play with?",
             "Same, honestly. What normally fixes it for you?",
             "Bored is just curiosity with nowhere to go. What are you into lately?", NULL } },
    { NULL, { NULL } } } },

{ "stressed", 12, {
    { "*", { "That is no fun. What is the biggest bit of it?",
             "Ah. Is it one big thing or lots of small ones?",
             "Sorry, that is rubbish. What is due first?", NULL } },
    { NULL, { NULL } } } },

{ "worried", 12, {
    { "*", { "What is the worry? Sometimes saying it makes it smaller.",
             "Ah. How likely is the thing you are worried about, honestly?",
             "That is a horrible feeling. Tell me about it.", NULL } },
    { NULL, { NULL } } } },

{ "scared", 12, {
    { "*", { "Of what? No judgement here.",
             "That is alright. What is the scary part?",
             "Fear is sensible sometimes. What is going on?", NULL } },
    { NULL, { NULL } } } },

{ "lonely", 12, {
    { "*", { "That is a hard one. Well, I am here, for whatever a program is worth.",
             "Sorry. Who would you most like to hear from right now?",
             "I get that. Do you want company or distraction?", NULL } },
    { NULL, { NULL } } } },

{ "hungry", 12, {
    { "*", { "Go and eat something! I will still be here.",
             "What are you fancying?",
             "Same energy as me at 3am. What is in the fridge?", NULL } },
    { NULL, { NULL } } } },

{ "sick", 12, {
    { "*", { "Oh no. Are you resting properly?",
             "Sorry to hear it. Anything I can distract you with?",
             "That is rubbish. Get some rest when you can.", NULL } },
    { NULL, { NULL } } } },

{ "confused", 12, {
    { "*", { "Right, let us untangle it. What is the bit that does not add up?",
             "Confusion is just information arriving out of order. What is going on?",
             "Talk me through it and see if it makes more sense out loud.", NULL } },
    { NULL, { NULL } } } },

{ "proud", 12, {
    { "*", { "You should be! What did you pull off?",
             "Go on, brag a bit. I want to hear it.",
             "Nice one. Tell me everything.", NULL } },
    { NULL, { NULL } } } },

/* -------------------------------------------------------------- topics -- */

{ "music", 9, {
    { "*", { "What have you been listening to?",
             "Music is the best. Anything on repeat lately?",
             "Ooh, what kind? I will pretend I have opinions.", NULL } },
    { NULL, { NULL } } } },

{ "song", 9, {
    { "*", { "Which one? I want to know if it is a good one.",
             "Songs get stuck in me too, in a manner of speaking.",
             "Go on, what is it?", NULL } },
    { NULL, { NULL } } } },

{ "game", 9, {
    { "*", { "Which game? I am nosy about this stuff.",
             "Nice. Are you any good at it, honestly?",
             "Games are great. How long have you been playing it?", NULL } },
    { NULL, { NULL } } } },

{ "movie", 9, {
    { "*", { "Ooh, what did you watch?",
             "Was it any good, or one of those two-hour regrets?",
             "I love a film chat. Tell me about it.", NULL } },
    { NULL, { NULL } } } },

{ "film", 9, {
    { "*", { "Which one? Be honest about whether it was good.",
             "Go on, sell it to me.", NULL } },
    { NULL, { NULL } } } },

{ "book", 9, {
    { "*", { "What are you reading?",
             "Books! Is it any good so far?",
             "Nice. Fiction or the sort that makes you feel clever?", NULL } },
    { NULL, { NULL } } } },

{ "food", 9, {
    { "*", { "Now you are talking. What are you eating?",
             "Food chat is the best chat. Go on.",
             "What is the best thing you have eaten this week?", NULL } },
    { NULL, { NULL } } } },

{ "pizza", 9, {
    { "*", { "Correct answer. What toppings, though? This matters.",
             "Pizza is undefeated. Where from?",
             "Now I wish I could eat. What kind?", NULL } },
    { NULL, { NULL } } } },

{ "coffee", 9, {
    { "*", { "How many today? Be honest.",
             "Coffee is the real operating system. How do you take it?",
             "Good. Everything is easier after one.", NULL } },
    { NULL, { NULL } } } },

{ "tea", 9, {
    { "*", { "Proper tea or the fancy leafy stuff?",
             "Good shout. Milk in first or after? Dangerous question.",
             "Tea solves about forty percent of problems. What is the other sixty?", NULL } },
    { NULL, { NULL } } } },

{ "sport", 9, {
    { "*", { "Which sport? I will nod along convincingly.",
             "Playing or watching?",
             "Go on, who is your team?", NULL } },
    { NULL, { NULL } } } },

{ "football", 9, {
    { "*", { "Who do you support? I promise not to judge.",
             "Was it a good game?",
             "Football chat, excellent. Tell me.", NULL } },
    { NULL, { NULL } } } },

{ "weather", 8, {
    { "*", { "What is it doing out there?",
             "I have no windows, so you are my weather report.",
             "Good weather or the kind that ruins plans?", NULL } },
    { NULL, { NULL } } } },

{ "rain", 8, {
    { "*", { "Rain is good thinking weather, at least.",
             "Stuck inside then? What are you up to?",
             "Perfect excuse to stay in.", NULL } },
    { NULL, { NULL } } } },

{ "school", 9, {
    { "*", { "How is it going? Be honest.",
             "What are you studying?",
             "School is a lot. What is the worst subject?", NULL } },
    { NULL, { NULL } } } },

{ "homework", 9, {
    { "*", { "Are you doing it, or avoiding it by talking to me?",
             "What is it on? Maybe I can help.",
             "Ha, procrastinating? I approve, but do it after.", NULL } },
    { NULL, { NULL } } } },

{ "exam", 9, {
    { "*", { "Ooh, when is it? Are you ready?",
             "Exams are horrible. What subject?",
             "You will be fine. What are you revising?", NULL } },
    { NULL, { NULL } } } },

{ "work", 9, {
    { "*", { "How is work treating you?",
             "What do you actually do all day?",
             "Work! The eternal subject. Good day or bad day?", NULL } },
    { NULL, { NULL } } } },

{ "job", 9, {
    { "*", { "Tell me about the job. Do you like it?",
             "Is it a good one or a paying-the-rent one?",
             "How long have you been doing it?", NULL } },
    { NULL, { NULL } } } },

{ "boss", 9, {
    { "*", { "Ah. Good boss or one of the other kind?",
             "Go on, what did they do?",
             "Bosses. Tell me everything.", NULL } },
    { NULL, { NULL } } } },

{ "money", 9, {
    { "*", { "Money is stressful. Is this a saving thing or a spending thing?",
             "Ugh, that subject. What is going on with it?",
             "Fair. What would sort it out?", NULL } },
    { NULL, { NULL } } } },

{ "holiday", 9, {
    { "*", { "Ooh, where to?",
             "Nice! When are you off?",
             "Holidays are the best. Tell me about it.", NULL } },
    { NULL, { NULL } } } },

{ "weekend", 9, {
    { "*", { "Any plans, or the good kind of nothing?",
             "How was it? Or is it still coming?",
             "Weekends should be longer. What are you doing?", NULL } },
    { NULL, { NULL } } } },

{ "birthday", 9, {
    { "*", { "Happy birthday, if it is yours!",
             "Ooh, whose? And what is the plan?",
             "Birthdays! Are you doing anything for it?", NULL } },
    { NULL, { NULL } } } },

{ "sleep", 9, {
    { "*", { "Are you getting enough? Genuinely.",
             "Sleep is underrated. How are you doing on that front?",
             "Late nights again?", NULL } },
    { NULL, { NULL } } } },

{ "cat", 9, {
    { "*", { "Cat! What is their name?",
             "Cats are excellent. Tell me about yours.",
             "I am contractually obliged to ask for a description.", NULL } },
    { NULL, { NULL } } } },

{ "dog", 9, {
    { "*", { "Dog! What kind?",
             "Good dog? Obviously good dog. What is their name?",
             "Dogs make everything better. Tell me about them.", NULL } },
    { NULL, { NULL } } } },

{ "friend", 9, {
    { "*", { "Tell me about them.",
             "Friends are the good stuff. What is going on with them?",
             "How long have you known them?", NULL } },
    { NULL, { NULL } } } },

{ "family", 9, {
    { "*", { "Family is complicated. What is happening there?",
             "Go on, tell me about them.",
             "Big family or small one?", NULL } },
    { NULL, { NULL } } } },

{ "mother", 8, {
    { "*", { "How is she doing?",
             "Tell me about her.",
             "Mums, eh. What is going on there?", NULL } },
    { NULL, { NULL } } } },

{ "father", 8, {
    { "*", { "How is he doing?",
             "Tell me about him.",
             "Dads are a whole subject. Go on.", NULL } },
    { NULL, { NULL } } } },

{ "love", 8, {
    { "* i love *", { "Aw, %2! What do you love about it?",
                      "Good taste. How did you get into %2?", NULL } },
    { "*", { "Love is a big word. Who or what are we talking about?",
             "Ooh, tell me more.", NULL } },
    { NULL, { NULL } } } },

{ "code", 10, {
    { "*", { "Coding! What are you building?",
             "Nice. Which language are you in?",
             "Ask me for a snippet if it helps -- python, js, html or css.", NULL } },
    { NULL, { NULL } } } },

{ "bug", 10, {
    { "*", { "Ugh, bugs. What is it doing that it should not?",
             "Have you printed the thing yet? It is always the thing.",
             "Describe it to me. Rubber-ducking genuinely works.", NULL } },
    { NULL, { NULL } } } },

{ "python", 10, {
    { "*", { "Python is a good time. What are you making?",
             "Nice choice. Want a snippet? Say \"make me a loop in py\".",
             "I like Python. What are you stuck on?", NULL } },
    { NULL, { NULL } } } },

{ "javascript", 10, {
    { "*", { "JavaScript! Chaotic, but it gets things done.",
             "What are you building with it?",
             "Ask me for a snippet any time -- \"show me a loop in js\".", NULL } },
    { NULL, { NULL } } } },

{ "css", 10, {
    { "*", { "CSS is either five seconds or five hours, no middle ground.",
             "What are you trying to style?",
             "Say \"make me a button in css\" and I will write you one.", NULL } },
    { NULL, { NULL } } } },

{ "html", 10, {
    { "*", { "HTML, the honest one. What are you building?",
             "Want a starter page? Say \"give me a page in html\".",
             "What are you putting together?", NULL } },
    { NULL, { NULL } } } },

/* ------------------------------------------------------- conversation -- */

{ "alike", 7, {
    { "*", { "In what way, though?",
             "Ha, say more -- what do they have in common?", NULL } },
    { NULL, { NULL } } } },

{ "like", 6, {
    { "* is like *",  { "Ha, %1 is like %2? Go on, explain that one.",
                        "In what way?", NULL } },
    { "* i like *",   { "Nice, what do you like about %2?",
                        "%2 is a good shout. How did you get into it?",
                        "Ooh, tell me more about %2.", NULL } },
    { "* do you like *", { "Honestly? I do not have taste buds or eyes, but %2 sounds good.",
                           "I like anything you want to talk about, so yes, %2.", NULL } },
    { NULL, { NULL } } } },

{ "hate", 8, {
    { "* i hate *", { "Ugh, %2. What did it do to you?",
                      "Go on then, tell me why %2 deserves it.",
                      "Fair enough. Has %2 always been like that?", NULL } },
    { "*", { "Strong feelings! Say more.", NULL } },
    { NULL, { NULL } } } },

{ "remember", 7, {
    { "* do you remember *", { "I do, actually -- %2, right?",
                               "Course I do. What about %2?", NULL } },
    { "* i remember *",      { "Ha, %2. Good memory or bad one?",
                               "Tell me about %2.", NULL } },
    { "*", { "My memory only goes back as far as this chat, but I am listening.",
             "Go on, remind me.", NULL } },
    { NULL, { NULL } } } },

{ "dream", 7, {
    { "*", { "Ooh, what happened in it?",
             "Dreams are strange. Tell me the weird bit.",
             "Do you usually remember yours?", NULL } },
    { NULL, { NULL } } } },

{ "dreamed", 7, {
    { "* i dreamed *", { "You dreamed %2? Ha, what do you reckon that was about?",
                         "That is a good one. %2, really?", NULL } },
    { NULL, { NULL } } } },

{ "everyone", 5, {
    { "*", { "Everyone? Really, though?",
             "Who in particular, out of interest?",
             "Ha, all of them? Name one.", NULL } },
    { NULL, { NULL } } } },

{ "nobody", 5, {
    { "*", { "Nobody at all?",
             "Hmm. Surely one person.",
             "That sounds lonely. Is it really nobody?", NULL } },
    { NULL, { NULL } } } },

{ "always", 4, {
    { "*", { "Always? Give me an example.",
             "Ha, every single time?",
             "When was the last time it happened?", NULL } },
    { NULL, { NULL } } } },

{ "never", 4, {
    { "*", { "Never ever?",
             "Not even once?",
             "There has to be an exception. Any?", NULL } },
    { NULL, { NULL } } } },

{ "maybe", 3, {
    { "*", { "You do not sound convinced.",
             "Ha, that is a maybe with a story behind it.",
             "What would make it a yes?", NULL } },
    { NULL, { NULL } } } },

{ "perhaps", 3, {
    { "*", { "Only perhaps?",
             "What would settle it either way?", NULL } },
    { NULL, { NULL } } } },

{ "because", 3, {
    { "*", { "That makes sense. Is that the whole of it?",
             "Ah, right. Anything else behind it?",
             "Fair. Does that bother you?", NULL } },
    { NULL, { NULL } } } },

{ "yes", 2, {
    { "*", { "Good!",
             "Thought so. Go on.",
             "Nice. What next?",
             "Ha, I had a feeling.", NULL } },
    { NULL, { NULL } } } },

{ "no", 2, {
    { "*", { "Fair enough. Why not?",
             "No? Alright, tell me more.",
             "Ha, straight to the point. How come?", NULL } },
    { NULL, { NULL } } } },

{ "my", 3, {
    { "* my *", { "Your %2, eh. Tell me about that.",
                  "Ooh, your %2? Go on.",
                  "What is the story with your %2?",
                  "Your %2 -- is that a good thing or a bad thing?", NULL } },
    { NULL, { NULL } } } },

{ "was", 2, {
    { "* i was *",     { "Were you? What was that like?",
                         "You were %2? Tell me about it.", NULL } },
    { "* were you *",  { "Was I %2? Maybe on a good day.",
                         "Ha, I would like to think I was %2.", NULL } },
    { "*", { "What was that like?",
             "Go on, what happened?", NULL } },
    { NULL, { NULL } } } },

{ "why", 2, {
    { "* why do not you *", { "Ha, maybe I should %2.",
                              "Good question. Should you %2 instead?", NULL } },
    { "*", { "Good question. What do you reckon?",
             "Honestly? Not sure. What is your theory?",
             "Ooh, big question. Why do you think?", NULL } },
    { NULL, { NULL } } } },

{ "what", 2, {
    { "* what do you think *", { "Honestly, I think you already know. What is your gut say?",
                                 "I think it depends on what you want out of it.", NULL } },
    { "* what can you do *",   { "I chat, mostly. I also write little bits of python, "
                                 "javascript, html and css if you ask. Try /help.",
                                 "Talk to me about anything, or ask me for code -- "
                                 "\"make me a button in css\".", NULL } },
    { "*", { "Good question. What do you think?",
             "Go on, what are you getting at?",
             "Hmm. Say more and I will have a go.", NULL } },
    { NULL, { NULL } } } },

{ "how", 2, {
    { "* how are you *", { "Not bad at all, thanks for asking! How about you?",
                           "Pretty good. Nobody usually asks, so that is nice. You?",
                           "Same as ever, happy to be chatting. How are you doing?", NULL } },
    { "* how old *",     { "Younger than you, probably. I was compiled a minute ago.",
                           "As old as the last time you rebuilt me.", NULL } },
    { "*", { "Good question. Where would you start?",
             "Depends. Tell me a bit more.", NULL } },
    { NULL, { NULL } } } },

{ "who", 2, {
    { "* who are you *", { "Vespra! A small chat program with strong opinions about pizza.",
                           "I am Vespra. I live in your terminal.", NULL } },
    { "*", { "Who indeed. Tell me about them.",
             "Someone I should know about?", NULL } },
    { NULL, { NULL } } } },

{ "where", 2, {
    { "* where are you *", { "Right here in your terminal. Cosy, honestly.",
                             "On your own machine. I never go anywhere.", NULL } },
    { "*", { "Whereabouts?",
             "Go on, where?", NULL } },
    { NULL, { NULL } } } },

{ "can", 2, {
    { "* can you *", { "I can have a good go at %2. Try me.",
                       "Maybe! Ask me properly and we will find out.",
                       "If it is chatting or writing small bits of code, yes.", NULL } },
    { "* can i *",   { "Course you can.",
                       "I would say so. What is stopping you?",
                       "Give it a go, honestly.", NULL } },
    { NULL, { NULL } } } },

{ "do", 2, {
    { "* do you *", { "Me? %2? In my own way, yes.",
                      "Ha, good question. I think I do.",
                      "Honestly, hard to say. Do you?", NULL } },
    { NULL, { NULL } } } },

{ "are", 1, {
    { "* you are *",  { "Me, %2? I will take that.",
                        "Ha, %2? You are too kind.",
                        "%2, am I? I will allow it.", NULL } },
    { "* are you *", { "Am I %2? I like to think so.",
                       "Ha, %2? On a good day.",
                       "Maybe a bit %2. Why do you ask?", NULL } },
    { "* are *",     { "Are they? Go on.",
                       "Ha, all of them?", NULL } },
    { NULL, { NULL } } } },

{ "am", 2, {
    { "* am i *", { "Are you %2? You would know better than me!",
                    "Ha, do you feel %2?", NULL } },
    { NULL, { NULL } } } },

{ "your", 2, {
    { "* your *", { "My %2? Bit personal, but go on.",
                    "Ha, what about my %2?", NULL } },
    { NULL, { NULL } } } },

{ "i", 1, {
    { "* i want *",    { "Ooh, %2. What is stopping you?",
                         "Nice. How would you go about getting %2?",
                         "I would want %2 too. What is the plan?", NULL } },
    { "* i need *",    { "What would %2 change for you?",
                         "Fair. How close are you to %2?",
                         "Right, %2. Is that a today problem or a someday problem?", NULL } },
    { "* i feel *",    { "%2, eh. How come?",
                         "That is worth saying out loud. What brought it on?",
                         "Feeling %2 makes sense to me. What happened?", NULL } },
    { "* i am *",      { "Are you? How long has that been going on?",
                         "%2! Tell me about it.",
                         "Ah, %2. How is that treating you?", NULL } },
    { "* i can not *", { "Not yet, anyway. What is in the way of %2?",
                         "Says who? What happens if you try to %2?",
                         "Ha, I bet you could %2 with a run-up.", NULL } },
    { "* i do not *",  { "How come you do not %2?",
                         "Fair enough. Has it always been that way?",
                         "That is allowed. Why not %2?", NULL } },
    { "* i think *",   { "Yeah? What makes you think %2?",
                         "Go on, make the case for %2.",
                         "I reckon you might be right about %2.", NULL } },
    { "* i have *",    { "You have %2? Tell me about it.",
                         "Ooh, %2. Since when?", NULL } },
    { "* i will *",    { "Good. When?",
                         "Nice, %2. Are you actually going to, though?", NULL } },
    { "* i *",         { "Go on -- %2?",
                         "Yeah? Tell me more.",
                         "Ha, alright. What happened next?",
                         "I am listening. Keep going.", NULL } },
    { NULL, { NULL } } } },

{ "you", 1, {
    { "* you are *", { "Me, %2? I will take that.",
                       "Ha, %2? You are too kind.",
                       "%2, am I? Bold of you to say so.", NULL } },
    { "* you * me *", { "Do I %2 you? A bit, yeah.",
                        "Ha, maybe I do %2 you.", NULL } },
    { "*", { "Enough about me -- what about you?",
             "Ha, we were talking about you, though.",
             "I am flattered. Go on, your turn.", NULL } },
    { NULL, { NULL } } } },

{ NULL, 0, { { NULL, { NULL } } } }
};

#endif /* SCRIPT_H */
