/* targs: a test helper for the command line parser.
   targs            prints each argument of its own command line as [text].
   targs --split    reads lines of UTF-8 from standard input, splits each as a command line and prints
                    its arguments as [text], then a line "--". */
#include <impossible/base.h>
#include "../common/cmdline.h"
#include "../common/lines.h"
#include "../common/out.h"
#include "../common/text.h"

static void show(const cmdline *c) {
    for (int i = 0; i < c->argc; i++) out_char(&tk_out, '['), out_wide(&tk_out, c->argv[i]), out_str(&tk_out, "]\n");
}

int im_main(void) {
    out_init();
    cmdline cmd;
    if (!cmdline_get(&cmd)) return TK_ERROR;
    if (cmd.argc != 2 || cmd.argv[1][0] != L'-' || cmd.argv[1][1] != L'-' || cmd.argv[1][2] != L's') return show(&cmd), finish("targs", TK_OK);
    reader r;
    reader_stdin(&r);
    liner li;
    liner_start(&li, &r);
    const char *line;
    size_t len;
    while (liner_next(&li, &line, &len) > 0) {
        size_t n = utf8_to_utf16(line, len, NULL, true);
        wchar_t *wide = tk_alloc((n + 1) * sizeof(wchar_t));
        cmdline split;
        utf8_to_utf16(line, len, wide, true);
        if (!wide || !cmdline_split(wide, &split)) return TK_ERROR;
        show(&split);
        out_str(&tk_out, "--\n");
        cmdline_free(&split), tk_free(wide);
    }
    return finish("targs", TK_OK);
}
