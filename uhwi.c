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

#include <stdio.h>
#include <ctype.h>

#include <fcntl.h>
#include <errno.h>

#include <unistd.h>

#include <sys/types.h>
#include <sys/ioctl.h>

#include <sys/stat.h>

#ifdef __FreeBSD__
#include <sys/pciio.h>

#include <libusb20.h>
#include <libusb20_desc.h>

#define UHWI_PCI_DEV_PATH_CONST "/dev/pci"
#define UHWI_PCI_IORS_SZ_BASE 32
#elif defined(__linux__)
#include <linux/limits.h>
#include <dirent.h>

#define UHWI_PCI_DIR_PATH_CONST "/sys/bus/pci/devices"
#define UHWI_PCI_PBSZ_CONST 7

#define UHWI_USB_DIR_PATH_CONST "/sys/bus/usb/devices"
#endif

#include "uhwi.h"

uhwi_errno_t uhwi_last_errno = UHWI_ERRNO_OK;

#ifdef __APPLE__
// use an IOKit wrapper function since the f/w supports device filtering by
// type natively (kinda)
uhwi_dev* uhwi_get_macos_devs(const uhwi_dev_t type, uhwi_dev** lpp);
#endif

#define SSCANF_ID(from, into, prefixed) { \
    const char* pfmt = "0x%04x"; \
    \
    if (!prefixed) /* afaik only Linux sysfs PCI IDs are prefixed with a 0x */ \
        pfmt += 2; \
    \
    /* on most OSses scanf() and its counterparts try to write "%x"-ed values into uint32_t-s, */ \
    /* which is problematic since uhwi_id_t is a 16-bit unsigned integer, hence we need a */ \
    /* proxy 32-bit unsigned integer variable to perform a cast almost immediately after the */ \
    /* sscanf() call */ \
    uint32_t conv = 0; \
    sscanf(from, pfmt, &conv); \
    \
    into = (uhwi_id_t)conv; \
}

#ifdef UHWI_ENABLE_PCI_DB

# ifndef UHWI_PCI_DB_PATH_CONST
#  ifdef __FreeBSD__
#   define UHWI_PCI_DB_PATH_CONST "/usr/share/misc/pci_vendors"
#  elif defined(__linux__)
#   define UHWI_PCI_DB_PATH_CONST "/usr/share/misc/pci.ids"
#  else
#   error "Please define UHWI_PCI_DB_PATH_CONST (path to PCI IDs DB) via your C compiler flags."
#  endif

# endif

typedef enum {
    // newline
    UHWI_DB_STATE_NL = 0,
    // device newline
    UHWI_DB_STATE_DEVICE_NL,

    // comment
    UHWI_DB_STATE_COMMENT,

    // vendor/device name C string
    UHWI_DB_STATE_NAME,
} uhwi_db_state_t;

#define INIT_DB_ENTRY(first, current, dfv, dfd) { \
    /* there could be default vendor and device IDs specified */ \
    uhwi_id_t vcur = dfv; \
    uhwi_id_t dcur = dfd; \
    \
    if (current) { \
        current->next = malloc(sizeof(uhwi_dev)); \
        current = current->next; \
    } else { \
        first = malloc(sizeof(uhwi_dev)); \
        current = first; \
    } \
    \
    memset(current, 0, sizeof(uhwi_dev)); \
    current->type = UHWI_DEV_PCI; \
    \
    current->vendor = vcur; \
    current->device = dcur; \
}

#define RESET_TOKEN(token, max, index) { \
    memset(token, 0, max); \
    index = 0; \
}

#define SCAN_DB_ID_INTO_NEW_ENTRY(first, current, dfv, dfd, field) { \
    /* initialize a new uhwi_dev* representing a DB entry */ \
    INIT_DB_ENTRY(first, current, dfv, dfd) \
    \
    /* read in the currently stored C string form of ID into the provided field of the */ \
    /* DB entry */ \
    SSCANF_ID(token, current->field, 0) \
    \
    /* don't forget to reset the token */ \
    RESET_TOKEN(token, UHWI_DEV_NAME_MAX_LEN, index) \
}

uhwi_dev* uhwi_db_init(void) {
    uhwi_last_errno = UHWI_ERRNO_OK;

    uhwi_dev* first = NULL;
    int fd = open(UHWI_PCI_DB_PATH_CONST, O_RDONLY, 0);

    if (fd < 0) {
        uhwi_last_errno = UHWI_ERRNO_PCI_DB_NO_ACCESS;
        return NULL;
    }

    // the state machine for processing the PCI vendors DB file
    uhwi_db_state_t state = UHWI_DB_STATE_NL;

    // C string token
    char token[UHWI_DEV_NAME_MAX_LEN];
    size_t index = 0;

    RESET_TOKEN(token, UHWI_DEV_NAME_MAX_LEN, index)

    uhwi_dev* current = NULL;

    while (1) {
        char cc = '\0';
        read(fd, &cc, 1);

        if (cc == '\r' || cc == '\n' || cc == '\0') {
            // newline encountered
            if (state == UHWI_DB_STATE_NAME && index > 0 && current)
                strncpy(current->name, token, UHWI_DEV_NAME_MAX_LEN);

            // reset current token & state
            state = UHWI_DB_STATE_NL;
            RESET_TOKEN(token, UHWI_DEV_NAME_MAX_LEN, index)

            if (cc == '\0')
                break; // EOF or error, doesn't matter - time to stop regardless
        } else if (cc == '#' && state != UHWI_DB_STATE_NAME)
            state = UHWI_DB_STATE_COMMENT; // the entire line is a comment
        else if (isspace(cc) == 0 && cc != 'C' &&
                 (index + 1) < UHWI_DEV_NAME_MAX_LEN &&
                 state != UHWI_DB_STATE_COMMENT)
            token[index++] = cc; // add the character to the token as is
        else {
            switch (state) {
                case UHWI_DB_STATE_NL: {
                    // vendor  vendor_name
                    if (index < 1) {
                        if (cc == 'C') // TODO: use more conventional way to end the loop
                            lseek(fd, 0, SEEK_END); // classification -> forced EOF
                        else if (current)
                            state = UHWI_DB_STATE_DEVICE_NL; // one tab -> device line
                    } else {
                        state = UHWI_DB_STATE_NAME;
                        SCAN_DB_ID_INTO_NEW_ENTRY(first, current, 0, 0, vendor)
                    }

                    break;
                }
                case UHWI_DB_STATE_DEVICE_NL: {
                    //    device  device_name
                    if (index < 1)
                        state = UHWI_DB_STATE_COMMENT; // [currently TODO] two tabs -> subvendor line
                    else {
                        state = UHWI_DB_STATE_NAME;
                        SCAN_DB_ID_INTO_NEW_ENTRY(first, current, current->vendor, 0,
                                                  device)
                    }

                    break;
                }
                case UHWI_DB_STATE_COMMENT:
                    break; // ignore during a comment
                default: {
                    // treat the current space character as part of a vendor/device name
                    // C string
                    token[index++] = cc;
                    break;
                }
            }
        }
    }

    // clean up and return the first entry in the PCI DB
    close(fd);
    return first;
}

void uhwi_strncpy_pci_db_dev_name(uhwi_dev* current, uhwi_dev* db) {
    const char* vname = "Unknown";
    const char* dname = NULL;

    while (db) {
        if (db->vendor == current->vendor) {
            if (db->device == current->device) {
                // exact match (TODO: support subvendor/subdevice IDs)
                dname = db->name;
                break;
            } else // only the vendor name C string is known for now
                vname = db->name;
        }

        // go through each PCI DB device entry
        db = db->next;
    }

    // if any name is detected for the PCI device, use it, overriding
    // the one returned by the OS itself (if any)
    strncpy(current->name, vname, UHWI_DEV_NAME_MAX_LEN - 1);

    if (dname)
        strncat(current->name, dname, UHWI_DEV_NAME_MAX_LEN - 1);
}

#undef SCAN_DB_ID_INTO_NEW_ENTRY
#undef INIT_DB_ENTRY

#undef RESET_TOKEN
#endif

#ifdef __linux__
#define COMBINE_PATH(base, label, fn) { \
    memset(path, 0, PATH_MAX); \
    snprintf(path, PATH_MAX - 1, "%s/%s/%s", base, label, fn); \
}

#define POPULATE_ID_FROM_PATH(result, path, prefixed, mandatory) { \
    if (access(path, F_OK) == 0) { \
        int pfd = open(path, O_RDONLY, 0); \
        \
        if (pfd >= 0) { \
            /* buffer that stores the prettified contents of the specified */ \
            /* sysfs pseudo-file, which are probably in a format of a 4-digit */ \
            /* hexademical value prepended with a 0x */ \
            char pbuf[UHWI_PCI_PBSZ_CONST]; \
            \
            /* read in pseudo-file contents into the buffer */ \
            memset(pbuf, 0, UHWI_PCI_PBSZ_CONST); \
            if (read(pfd, pbuf, UHWI_PCI_PBSZ_CONST) > 0) \
                SSCANF_ID(pbuf, result, prefixed) \
            \
            /* clean up */ \
            close(pfd); \
        } \
    } else if (mandatory) \
        return NULL; /* fail as is (keep in mind, NO CLEAN UP PERFORMED!!) */ \
}

#define POPULATE_ID_FROM_COMBINED_PATH(base, label, fn, result, prefixed, mandatory) { \
    COMBINE_PATH(base, label, fn) \
    POPULATE_ID_FROM_PATH(result, path, prefixed, mandatory) \
}

uhwi_dev* uhwi_cat_sysfs_pci_dev(const char* label, uhwi_dev* db) {
    char path[PATH_MAX];

    uhwi_id_t vendor = 0;
    uhwi_id_t device = 0;

    uhwi_id_t subvendor = 0;
    uhwi_id_t subdevice = 0;

    // obtain utmost important values - PCI vendor and device IDs
    POPULATE_ID_FROM_COMBINED_PATH(UHWI_PCI_DIR_PATH_CONST,
                                   label, "vendor",
                                   vendor, 1, 1)
    POPULATE_ID_FROM_COMBINED_PATH(UHWI_PCI_DIR_PATH_CONST,
                                   label, "device",
                                   device, 1, 1)

    // then obtain the rest, if available (subvendor and subdevice IDs)
    POPULATE_ID_FROM_COMBINED_PATH(UHWI_PCI_DIR_PATH_CONST,
                                   label, "subsystem_vendor",
                                   subvendor, 1, 0)
    POPULATE_ID_FROM_COMBINED_PATH(UHWI_PCI_DIR_PATH_CONST,
                                   label, "subsystem_device",
                                   subdevice, 1, 0)

    // prepare the resulting uhwi_dev* populated with all the values we have
    // obtained so far
    uhwi_dev* result = malloc(sizeof(uhwi_dev));
    memset(result, 0, sizeof(uhwi_dev));

    result->type = UHWI_DEV_PCI;

    result->vendor = vendor;
    result->device = device;

    result->subvendor = subvendor;
    result->subdevice = subdevice;

# ifdef UHWI_ENABLE_PCI_DB
    // try to detect PCI device name C string, if possible
    uhwi_strncpy_pci_db_dev_name(result, db);
# endif

    return result;
}

#define READ_USB_DEVICE_CSTR_DIRECTLY_FROM_COMBINED_PATH(label, fn, into, max) { \
    COMBINE_PATH(UHWI_USB_DIR_PATH_CONST, label, fn) \
    \
    int pfd = open(path, O_RDONLY, 0); \
    \
    if (pfd) { \
        char pbuf[max]; \
        memset(pbuf, 0, max); \
        \
        ssize_t rdsz = read(pfd, pbuf, max); \
        \
        if (rdsz > 1) { \
            pbuf[rdsz - 1] = ' '; /* replace trailing newline to combine C strings */ \
            strncat(into, pbuf, max - 1); \
        } \
        \
        close(pfd); \
    } \
}

uhwi_dev* uhwi_sysfs_cat_usb_dev(const char* label) {
    char path[PATH_MAX];

    uhwi_id_t vendor = 0;
    uhwi_id_t device = 0;

    // obtain USB vendor and product/device IDs (all are mandatory)
    POPULATE_ID_FROM_COMBINED_PATH(UHWI_USB_DIR_PATH_CONST,
                                   label, "idVendor",
                                   vendor, 0, 1)
    POPULATE_ID_FROM_COMBINED_PATH(UHWI_USB_DIR_PATH_CONST,
                                   label, "idProduct",
                                   device, 0, 1)

    uhwi_dev* result = malloc(sizeof(uhwi_dev));
    memset(result, 0, sizeof(uhwi_dev));

    result->type = UHWI_DEV_USB;

    result->vendor = vendor;
    result->device = device;

    // attempt to read in self-reported USB device manufacturer + product/model
    // name
    READ_USB_DEVICE_CSTR_DIRECTLY_FROM_COMBINED_PATH(label, "manufacturer",
                                                     result->name,
                                                     UHWI_DEV_NAME_MAX_LEN)
    READ_USB_DEVICE_CSTR_DIRECTLY_FROM_COMBINED_PATH(label, "product",
                                                     result->name,
                                                     UHWI_DEV_NAME_MAX_LEN)

    return result;
}

#undef READ_USB_DEVICE_CSTR_DIRECTLY_FROM_COMBINED_PATH

#undef POPULATE_ID_FROM_COMBINED_PATH
#undef POPULATE_ID_FROM_PATH

#undef COMBINE_PATH
#endif

#ifdef __FreeBSD__
void uhwi_strncat_libusb20_indexed_cstr(struct libusb20_device* dvp,
                                        const uint8_t idx,
                                        char* target,
                                        const size_t max) {
    // attempt to open USB device in control transfer-exclusive mode
    if (libusb20_dev_open(dvp, 0) != 0)
        return;

    char buf[max];
    memset(buf, 0, max);

    // try to obtain ASCII C string on the specified USB index
    if (libusb20_dev_req_string_simple_sync(dvp, idx, buf, max - 1) == 0) {
        // on success, strncat() it with a trailing space (to make additional
        // reads to the same C string buffer combineable)
        strncat(target, buf, max - 1);
        strncat(target, " ", max - 1);
    }

    // clean up
    libusb20_dev_close(dvp);
}
#endif

#define ADD_TO_LINKED_LIST(first, last, current) { \
    if (current) { \
        if (last) \
            last->next = current; \
        else \
            first = current; \
        \
        last = current; \
    } \
}

uhwi_dev* uhwi_get_pci_devs(uhwi_dev** lpp) {
    uhwi_dev* first = NULL;
    uhwi_dev* last = NULL;

    uhwi_dev* db = NULL;
    uhwi_last_errno = UHWI_ERRNO_OK;

#ifdef UHWI_ENABLE_PCI_DB
    // parse PCI device naming DB into memory
    db = uhwi_db_init();

    if (!db && uhwi_last_errno != UHWI_ERRNO_OK)
        return NULL; // fail in case if parsing failed
#endif

#ifdef __FreeBSD__
    // open the PCI global control device for read first
    int fd = open(UHWI_PCI_DEV_PATH_CONST, O_RDONLY, 0);

    if (fd < 0) {
        uhwi_last_errno = UHWI_ERRNO_PCI_OPEN;

        uhwi_clean_up(db);
        return NULL;
    }

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
            uhwi_clean_up(db);

            free(iors);
            close(fd);

            uhwi_last_errno = UHWI_ERRNO_PCI_IOCTL;
            return NULL;
        }

        size_t index = 0;

        while (index < cnf.num_matches) {
            uhwi_dev* current = malloc(sizeof(uhwi_dev));
            memset(current, 0, sizeof(uhwi_dev));

            current->type = UHWI_DEV_PCI;
            current->vendor = iors[index].pc_vendor;
            current->device = iors[index].pc_device;

            current->subvendor = iors[index].pc_subvendor;
            current->subdevice = iors[index].pc_subdevice;

# ifdef UHWI_ENABLE_PCI_DB
            // try to guess PCI device C string from the DB, if possible
            uhwi_strncpy_pci_db_dev_name(current, db);
# endif

            ADD_TO_LINKED_LIST(first, last, current)
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
#elif defined(__linux__)
    DIR* descd = opendir(UHWI_PCI_DIR_PATH_CONST);

    if (!descd) {
        // failed to access /sys/bus/pci/devices directory -> clean up & fail
        uhwi_clean_up(db);

        uhwi_last_errno = UHWI_ERRNO_SYSFS_OPEN;
        return NULL;
    }

    // each PCI device is represented by a directory
    struct dirent* entry = NULL;

    while (1) {
        entry = readdir(descd);

        if (!entry)
            break; // end of directory listing
        else if (entry->d_name[0] == '.')
            continue; // skip all hidden or parent reference entries

        // prepare uhwi_dev* from the specified PCI device-representing
        // pseudo-directory
        uhwi_dev* current = uhwi_cat_sysfs_pci_dev(entry->d_name, db);

        if (!current)
            continue;

        ADD_TO_LINKED_LIST(first, last, current)
    }

    // clean up
    closedir(descd);
#endif

    // return pointer to the last detected PCI device, if requested
    if (lpp)
        (*lpp) = last;

    // unload PCI DB from memory, if it was loaded in the first place
    uhwi_clean_up(db);

    return first;
}

uhwi_dev* uhwi_get_usb_devs(void) {
    uhwi_dev* first = NULL;
    uhwi_dev* last = NULL;

    uhwi_last_errno = UHWI_ERRNO_OK;

#ifdef __FreeBSD__
    //
    // FreeBSD comes with its copy of libusb 1.0, which seems to be broken,
    // however - libusb 2.0 seems to be a relatively unrelated FreeBSD-specific
    // USB device API akin to macOS's IOKit
    //
    // Oh, yeah, and the docs for libusb 2.0 are very poorly written... like,
    // you don't even get to see the contents of struct LIBUSB20_DEVICE_DESC_DECODED,
    // not even in the header... this time, it seems, FreeBSD devs have taken
    // the lead for unreadable API docs from Apple :P
    //
    // As for reference on how to use libusb 2.0, I checked these sources:
    // - https://man.freebsd.org/cgi/man.cgi?query=libusb20&apropos=0&sektion=3&manpath=FreeBSD+13.2-RELEASE&arch=default&format=html
    // - https://github.com/freebsd/wireless/blob/main/usr.sbin/usbconfig/usbconfig.c
    //
    struct libusb20_backend* bke = libusb20_be_alloc_default();

    if (!bke) {
        uhwi_last_errno = UHWI_ERRNO_USB_INIT;
        return NULL;
    }

    // try to go through a list of USB devices plugged into the current system
    struct libusb20_device* dvp = NULL;

    while (1) {
        dvp = libusb20_be_device_foreach(bke, dvp);

        if (!dvp)
            break; // end of listing

        struct LIBUSB20_DEVICE_DESC_DECODED* desc = libusb20_dev_get_device_desc(dvp);

        if (!desc)
            continue; // skip devices the description of which is unavailable

        // populate a new uhwi_dev* with the information from the description
        // structure
        uhwi_dev* current = malloc(sizeof(uhwi_dev));
        memset(current, 0, sizeof(uhwi_dev));

        current->type = UHWI_DEV_USB;

        current->vendor = desc->idVendor;
        current->device = desc->idProduct;

        if (current->vendor == 0 || current->device == 0) {
            // skip invalid USB devices
            free(current);
            continue;
        }

        // try to obtain manufacturer and product name C strings
        uhwi_strncat_libusb20_indexed_cstr(dvp, desc->iManufacturer,
                                           current->name,
                                           UHWI_DEV_NAME_MAX_LEN);
        uhwi_strncat_libusb20_indexed_cstr(dvp, desc->iProduct,
                                           current->name,
                                           UHWI_DEV_NAME_MAX_LEN);

        // don't forget to add the uhwi_dev* to the linked list
        ADD_TO_LINKED_LIST(first, last, current)
    }

    // clean up
    libusb20_be_free(bke);
#elif defined(__APPLE__)
    first = uhwi_get_macos_devs(UHWI_DEV_USB, &last);
#elif defined(__linux__)
    DIR* drd = opendir(UHWI_USB_DIR_PATH_CONST);

    if (!drd) {
        uhwi_last_errno = UHWI_ERRNO_SYSFS_OPEN;
        return NULL;
    }

    struct dirent* entry = NULL;

    while (1) {
        entry = readdir(drd);

        if (!entry)
            break; // end of directory listing
        else if (entry->d_name[0] == '.')
            continue; // skip hidden and parent reference entries

        // try to process sysfs representation of the USB device into a
        // uhwi_dev*
        uhwi_dev* current = uhwi_sysfs_cat_usb_dev(entry->d_name);

        // skip this USB device in case of failure
        if (!current)
            continue;

        ADD_TO_LINKED_LIST(first, last, current)
    }

    // clean up
    closedir(drd);
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

uhwi_errno_t uhwi_get_errno(void) {
    return uhwi_last_errno;
}
