#include <stdio.h>
#include <stdlib.h>

#include "uhwi.h"

int show_usage(const char* argv0) {
    fprintf(stderr, "Usage: %s [-u|-l|-?]\n", argv0);
    return 1;
}

int main(const int argc, const char** argv) {
    uhwi_dev_t type = UHWI_DEV_NULL;

    if (argc >= 2) {
        if (argv[1][0] == '-' && argv[1][1] != '\0') {
            switch (argv[1][1]) {
                case 'u': {
                    type = UHWI_DEV_USB;
                    break;
                }
                case 'l': {
                    type = UHWI_DEV_PCI;
                    break;
                }
                default:
                    return show_usage(argv[0]);
            }
        }
    }

    uhwi_dev* first = uhwi_get_devs(type);

    if (!first) {
        fprintf(stderr, "failed to obtain UHWI device info (or no devices of this type are connected to the system)!!\n");
        return 1;
    }

    while (first) {
        uhwi_dev* next = first->next;

        fprintf(stdout, "[%s] vendor=0x%04x, device=0x%04x\n",
                        (first->type == UHWI_DEV_USB) ? "USB" : "PCI",
                        first->vendor, first->device);
        free(first);

        first = next;
    }

    return 0;

    return 0;
}