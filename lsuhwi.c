#include <stdio.h>
#include <stdlib.h>

#include "uhwi.h"

int show_usage(const char* argv0) {
    fprintf(stderr, "Usage: %s [-u|-l|-?]\n", argv0);
    return 1;
}

#define UHWI_DEV_TYPE_TO_CSTR(type) \
    ((type == UHWI_DEV_USB) ? "USB" : "PCI")

void format_as_json(uhwi_dev* current, FILE* where) {
    if (!current || current->type == UHWI_DEV_NULL)
        return; // impossible though

    fprintf(where, "{");
    fprintf(where, "\"type\":\"%s\",\"vendor\":%u,\"device\":%u,",
                   UHWI_DEV_TYPE_TO_CSTR(current->type),
                   current->vendor, current->device);
    fprintf(where, "\"subvendor\":%u,\"subdevice\":%u,",
                   current->subvendor, current->subdevice);
    fprintf(where, "\"name\":\"");

    size_t index = 0;

    while (1) {
        const char cc = current->name[index];

        if (cc == '\0')
            break;

        switch (cc) {
            case '"':
            case '\\': {
                fputc('\\', where);
                break;
            }

            default:
                break;
        }

        fputc(cc, where);
        index++;
    }

    fprintf(where, "\"}");

    if (current->next)
        fputc(',', where);
}

int main(const int argc, const char** argv) {
    uhwi_dev_t type = UHWI_DEV_NULL;
    size_t as_json = 0;

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
                case 'J': {
                    as_json = 1;
                    break;
                }
                default:
                    return show_usage(argv[0]);
            }
        }
    }

    uhwi_dev* first = uhwi_get_devs(type);

    if (!first && uhwi_get_errno() != UHWI_ERRNO_OK) {
        fprintf(stderr, "failed to obtain UHWI device info (or no devices of this type are connected to the system)!!\n");
        return 1;
    }

    if (as_json)
        fputc('[', stdout);

    while (first) {
        uhwi_dev* next = first->next;

        if (as_json)
            format_as_json(first, stdout);
        else {
            if (type == UHWI_DEV_NULL)
                fprintf(stdout, "[%s] ", (first->type == UHWI_DEV_USB) ? "USB" :
                                                                     "PCI");

            fprintf(stdout, "vendor=0x%04x, device=0x%04x", first->vendor,
                                                        first->device);

            if (first->type == UHWI_DEV_PCI)
                fprintf(stdout, ", subvendor=0x%04x, subdevice=0x%04x",
                                first->subvendor, first->subdevice);

            if (first->name[0] != '\0')
                fprintf(stdout, ", name: %s", first->name);

            fprintf(stdout, "%c", '\n');
        }

        free(first);
        first = next;
    }

    if (as_json)
        fputc(']', stdout);

    return 0;
}
