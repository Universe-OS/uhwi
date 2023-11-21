//
// Copyright (C) 2023 Universe-OS
// Copyright (C) 2023 Tim K. <timk@xfen.page>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <errno.h>

#include <unistd.h>

#include <sys/types.h>
#include <sys/ioctl.h>

#ifdef __FreeBSD__
#include <sys/pciio.h>
#include <libusb.h>

#define UHWI_PCI_DEV_PATH_CONST "/dev/pci"
#define UHWI_PCI_IORS_SZ_BASE 32
#endif

#include "uhwi.h"

#ifdef __APPLE__
// use an IOKit wrapper function since the f/w supports device filtering by
// type natively (kinda)
uhwi_dev* uhwi_get_macos_devs(const uhwi_dev_t type, uhwi_dev** lpp);
#endif

uhwi_dev* uhwi_get_pci_devs(uhwi_dev** lpp) {
    uhwi_dev* first = NULL;
    uhwi_dev* last = NULL;

#ifdef __FreeBSD__
    // open the PCI global control device for read first
    int fd = open(UHWI_PCI_DEV_PATH_CONST, O_RDONLY, 0);

    if (fd < 0)
        return NULL;

    // iors -> I/O (ioctl) result
    size_t iors_sz = sizeof(struct pci_conf) * UHWI_PCI_IORS_SZ_BASE;
    struct pci_conf* iors = malloc(iors_sz);

    // try to obtain as much PCI devices into the iors buffer as possible
    struct pci_conf_io cnf;
    memset(&cnf, 0, sizeof(struct pci_conf_io));

    cnf.match_buf_len = iors_sz;
    cnf.matches = iors;

    while (1) {
        if (ioctl(fd, PCIOCGETCONF, &cnf) == -1 ||
            cnf.status == PCI_GETCONF_LIST_CHANGED ||
            cnf.status == PCI_GETCONF_ERROR) {
            // clean up and fail
            free(iors);
            close(fd);

            return NULL;
        }

        size_t index = 0;

        while (index < cnf.num_matches) {
            uhwi_dev* current = malloc(sizeof(uhwi_dev));
            memset(current, 0, sizeof(uhwi_dev));

            current->type = UHWI_DEV_PCI;
            current->vendor = iors[index].pc_vendor;
            current->device = iors[index].pc_device;

            if (last)
                last->next = current;
            else
                first = current;

            last = current;
            index++;
        }

        if (cnf.status == PCI_GETCONF_LAST_DEVICE)
            break;
    }

    // clean up
    free(iors);
    close(fd);
#elif defined(__APPLE__)
    first = uhwi_get_macos_devs(UHWI_DEV_PCI, &last);
#endif

    // return pointer to the last detected PCI device, if requested
    if (lpp)
        (*lpp) = last;

    return first;
}

uhwi_dev* uhwi_get_usb_devs(void) {
    uhwi_dev* first = NULL;
    uhwi_dev* last = NULL;

#ifdef __FreeBSD__
    // FreeBSD comes with its copy of libusb, which is convenient
    libusb_context* ctx = NULL;

    if (libusb_init(&ctx) != 0)
        return NULL; // failed then

    // try to obtain a list of USB devices plugged into the current system
    libusb_device** list = NULL;
    ssize_t lsz = libusb_get_device_list(ctx, &list);

    if (lsz < 0) {
        // clean up and fail
        libusb_exit(ctx);
        return NULL;
    }

    for (size_t index = 0; index < (size_t)lsz; index++) {
        // try to obtain USB device descriptor structure, which contains all
        // the necessary information we need
        libusb_device_descriptor desc;
        memset(&desc, 0, sizeof(libusb_device_descriptor));

        if (libusb_get_device_descriptor(list[index], &desc) != 0)
            continue;

        // populate a UHWI device structure
        uhwi_dev* current = malloc(sizeof(uhwi_dev));
        memset(current, 0, sizeof(uhwi_dev));

        current->type = UHWI_DEV_USB;
        current->vendor = desc.idVendor;
        current->device = desc.idProduct;

        // add it to the linked list of USB devices
        if (last)
            last->next = current;
        else
            first = last;

        last = current;
    }

    // clean up
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
#elif defined(__APPLE__)
    first = uhwi_get_macos_devs(UHWI_DEV_USB, &last);
#endif

    return first;
}

uhwi_dev* uhwi_get_devs(const uhwi_dev_t type) {
    uhwi_dev* pci_last = NULL;

    uhwi_dev* pci = (type != UHWI_DEV_USB) ? uhwi_get_pci_devs(&pci_last) :
                                             NULL;
    uhwi_dev* usb = (type != UHWI_DEV_PCI) ? uhwi_get_usb_devs() : NULL;

    switch (type) {
        case UHWI_DEV_PCI:
            return pci;
        case UHWI_DEV_USB:
            return usb;

        default: {
            if (pci_last) {
                pci_last->next = usb;
                return pci;
            } else
                return usb;
        }
    }
}

void uhwi_clean_up(uhwi_dev* first) {
    while (first) {
        uhwi_dev* next = first->next;

        free(first);
        first = next;
    }
}
