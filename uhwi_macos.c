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

#include <CoreFoundation/CoreFoundation.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugin.h>

#include <IOKit/usb/IOUSBLib.h>

#include "uhwi.h"

extern uhwi_errno_t uhwi_last_errno;

void uhwi_strncpy_macos_dev_name_cstr(const uhwi_dev_t type,
                                      const io_service_t dvv,
                                      char* buf, const size_t max) {
    if (type == UHWI_DEV_NULL)
        return; // makes sense

    // detect what kind of key applies to the device we are working with (the
    // key that contains a user-friendly C string rendition of the device's
    // product string)
    const char* key = (type == UHWI_DEV_USB) ? kUSBProductString : "IOName";

    // try to obtain that key's value
    CFStringRef nmp = CFStringCreateWithCString(kCFAllocatorDefault, key,
                                                kCFStringEncodingASCII);
    CFTypeRef raw = IORegistryEntryCreateCFProperty(dvv, nmp, kCFAllocatorDefault,
                                                    0);

    // copy the resulting value, if valid, to the provided C string buffer
    if (raw && CFGetTypeID(raw) == CFStringGetTypeID())
        CFStringGetCString(raw, buf, max, kCFStringEncodingUTF8);

    // clean up
    CFRelease(nmp);
}

uhwi_dev* uhwi_get_macos_devs(const uhwi_dev_t type, uhwi_dev** lpp) {
    if (type == UHWI_DEV_NULL)
        return NULL; // must be a specific device type request

    uhwi_dev* first = NULL;
    uhwi_dev* last = NULL;

    uhwi_last_errno = UHWI_ERRNO_OK;

    // make a class matcher dictionary thing
    const char* mcn = (type == UHWI_DEV_USB) ? "IOUSBDevice" : "IOPCIDevice";
    CFMutableDictionaryRef matcher = IOServiceMatching(mcn);

    if (!matcher) {
        uhwi_last_errno = UHWI_ERRNO_IOKIT_NO_MEM;
        return NULL;
    }

    // obtain all IOKit IOService instances (these are basically device-representing
    // objects which are just typedef-ed mach_port_t-s)
    io_iterator_t iter = 0;
    kern_return_t err = IOServiceGetMatchingServices(kIOMainPortDefault,
                                                     matcher,
                                                     &iter);

    if (err != KERN_SUCCESS) {
        // clean up and fail
        CFRelease(matcher);

        uhwi_last_errno = UHWI_ERRNO_IOKIT_SERVICE;
        return NULL;
    }

    // iterate through all the detected devices
    io_service_t dvv = 0;

    while (1) {
        dvv = IOIteratorNext(iter);

        if (dvv) {
            uhwi_dev* current = NULL;

            if (type == UHWI_DEV_USB) {
                //
                // big thx to https://gist.github.com/thinkski/0ebc53ab4989d904a04c
                //
                // honestly, I can barely understand what kind of sourcery does this
                // half-baked API even do...
                //
                // ..guess what? Apple didn't bother to shit out even a brief des-
                // cription of what all these functions and triple-pointers do!!
                //
                // see for yourself:
                // https://developer.apple.com/documentation/iokit/1412429-iocreateplugininterfaceforservic?language=objc
                //
                // I love the Mac, but man this API is so mediocre
                //
                IOCFPlugInInterface** ifdp = NULL;
                IOUSBDeviceInterface** dvdp = NULL;

                // this is literally something we will never use in the code,
                // but this has to be here since we can't just pass NULL as
                // the final argument to IOCreatePlugInInterfaceForService
                // as it would crash in that case
                SInt32 state = 0;

                err = IOCreatePlugInInterfaceForService(dvv, kIOUSBDeviceUserClientTypeID,
                                                        kIOCFPlugInInterfaceID,
                                                        &ifdp, &state);

                if (err != kIOReturnSuccess || !ifdp)
                    continue;

                HRESULT rsl = (*ifdp)->QueryInterface(ifdp,
                                                      CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID650),
                                                      (LPVOID*)&dvdp);

                if (rsl != 0 || !dvdp)
                    continue;

                // finally, obtain device vendor and product ID after the dark
                // magic performed above
                current = malloc(sizeof(uhwi_dev));
                memset(current, 0, sizeof(uhwi_dev));

                (*dvdp)->GetDeviceVendor(dvdp, &current->vendor);
                (*dvdp)->GetDeviceProduct(dvdp, &current->device);

                uhwi_strncpy_macos_dev_name_cstr(type, dvv, current->name,
                                                 UHWI_DEV_NAME_MAX_LEN);
            }

            // clean up the device object reference port thing
            IOObjectRelease(dvv);

            // add our UHWI device structure to the linked list if it is valid
            if (current) {
                current->type = type;

                if (last)
                    last->next = current;
                else
                    first = current;

                last = current;
            }
        } else
            break;
    }

    // clean up
    IOObjectRelease(iter);

    // provide the user with the last UHWI device structure pointer, if requested
    if (lpp)
        (*lpp) = last;

    return first;
}
