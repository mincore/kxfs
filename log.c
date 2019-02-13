#include <stdio.h>

static FILE *glogfp = stdout;

FILE* get_log_fp() {
    return glogfp;
}

void log_init(FILE* fp) {
    if (fp)
        glogfp = fp;
}

void log_exit() {
    if (glogfp && glogfp != stdout && stdout != stderr) {
        fclose(glogfp);
    }
}