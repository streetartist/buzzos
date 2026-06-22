#include "libc.h"

void worker(void) {
    for (int i = 0; i < 5; i++) {
        printf("[thread] tick %d\n", i);
        yield();
    }
    exit(0);
}

int main(int argc, char **argv) {
    puts("=== BuzzOS Multithreading Demo ===");
    printf("argc=%d argv0=%s\n", argc, argc > 0 ? argv[0] : "(none)");
    for (int i = 1; i < argc; i++)
        printf("arg%d=%s\n", i, argv[i]);

    /* Spawn a background thread */
    int tid = spawn(worker);
    printf("Spawned thread tid=%d\n", tid);

    /* Main thread also does work */
    for (int i = 0; i < 5; i++) {
        printf("[main]   tick %d\n", i);
        yield();
    }

    /* Wait for thread to finish */
    join(tid);
    puts("Thread joined. Sleeping 10s...");
    sleep_ms(10000);
    puts("Done!");
    return 0;
}
