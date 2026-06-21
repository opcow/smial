#include "cli.h"
#include "gui.h"
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#endif

int main(int argc, char* argv[]) {
    if (argc > 1) {
#ifdef _WIN32
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);
                      freopen_s(&f, "CONOUT$", "w", stderr);
        }
#endif
        return cli_main(argc, argv);
    }
    return gui_main();
}
