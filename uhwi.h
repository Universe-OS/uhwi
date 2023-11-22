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

#pragma once

#include <stdint.h>

/// vendor & hw ID for PCI/USB
typedef uint16_t uhwi_id_t;

typedef enum {
    UHWI_DEV_NULL = 0,

    UHWI_DEV_PCI,
    UHWI_DEV_USB
} uhwi_dev_t;

#define UHWI_DEV_NAME_MAX_LEN 128

typedef struct {
    /// device type
    uhwi_dev_t type;

    /// vendor ID (16-bit unsigned integer)
    uhwi_id_t vendor;
    /// device ID (16-bit unsigned integer)
    uhwi_id_t device;

    /// subvendor ID (16-bit unsigned integer, PCI-only)
    uhwi_id_t subvendor;
    /// subdevice ID (16-bit unsigned integer, PCI-only)
    uhwi_id_t subdevice;

    /// device user-friendly name C string
    char name[UHWI_DEV_NAME_MAX_LEN];

    /// reference to the next available device
    void* next;
} uhwi_dev;

/// enumerates devices of a specified type available in the system
uhwi_dev* uhwi_get_devs(const uhwi_dev_t type);

/// frees the entire linked list of devices
void uhwi_clean_up(uhwi_dev* first);

typedef enum {
    // successful operation
    UHWI_ERRNO_OK = 0,

    //
    // FreeBSD PCI vendors DB indexing
    //
    UHWI_ERRNO_PCI_DB_NO_ACCESS,

    //
    // IOKit on macOS
    //

    // out of memory
    UHWI_ERRNO_IOKIT_NO_MEM,
    // IOServiceGetMatchingServices() failure
    UHWI_ERRNO_IOKIT_SERVICE,

    //
    // pci(4) on FreeBSD
    //

    // open("/dev/pci") failed
    UHWI_ERRNO_PCI_OPEN,
    // ioctl(PCIOCGETCONF) failed
    UHWI_ERRNO_PCI_IOCTL,

    //
    // libusb 1.0 on FreeBSD
    //

    // libusb_init() failed
    UHWI_ERRNO_USB_INIT,
    // libusb_get_device_list() failed
    UHWI_ERRNO_USB_LIST
} uhwi_errno_t;

uhwi_errno_t uhwi_get_errno(void);
