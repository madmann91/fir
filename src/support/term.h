#pragma once

#define TERM1(x)       "\33[" x "m"
#define TERM2(x, y)    "\33[" x ";" y "m"
#define TERM3(x, y, z) "\33[" x ";" y ";" z "m"

#define TERM_RESET "0"
#define TERM_BOLD "1"
#define TERM_ITALIC "3"
#define TERM_UNDERLINE "4"

#define TERM_FG_BLACK   "30"
#define TERM_FG_RED     "31"
#define TERM_FG_GREEN   "32"
#define TERM_FG_YELLOW  "33"
#define TERM_FG_BLUE    "34"
#define TERM_FG_MAGENTA "35"
#define TERM_FG_CYAN    "36"
#define TERM_FG_WHITE   "37"

#define TERM_BG_BLACK   "40"
#define TERM_BG_RED     "41"
#define TERM_BG_GREEN   "42"
#define TERM_BG_YELLOW  "43"
#define TERM_BG_BLUE    "44"
#define TERM_BG_MAGENTA "45"
#define TERM_BG_CYAN    "46"
#define TERM_BG_WHITE   "47"
