
#define GAB_INVIS "\x1b[28m"
#define GAB_BLINK "\x1b[25m"
#define GAB_SAVE "\x1b[s"
#define GAB_RESTORE "\x1b[u"
#define GAB_BLACK "\x1b[30m"
#define GAB_RED "\x1b[31m"
#define GAB_GREEN "\x1b[32m"
#define GAB_YELLOW "\x1b[33m"
#define GAB_BLUE "\x1b[34m"
#define GAB_MAGENTA "\x1b[35m"
#define GAB_CYAN "\x1b[36m"
#define GAB_RESET "\x1b[0m"
#define GAB_CLEAR "\x1b[2J"

#ifdef GAB_COLORS_IMPL
static const char *ANSI_COLORS[] = {
    GAB_GREEN,  GAB_MAGENTA, GAB_RED,
    GAB_YELLOW, GAB_BLUE,    GAB_CYAN,
};
#endif

#define GAB_COLORS_LEN (sizeof(ANSI_COLORS) / sizeof(ANSI_COLORS[0]))
