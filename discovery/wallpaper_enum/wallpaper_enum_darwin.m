#import <AppKit/AppKit.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

static int appendf(char *buf, size_t buf_size, size_t *used, const char *fmt, ...) {
    if (*used >= buf_size) {
        return 0;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf + *used, buf_size - *used, fmt, args);
    va_end(args);
    if (written < 0) {
        return 0;
    }
    if ((size_t)written >= buf_size - *used) {
        *used = buf_size - 1;
        buf[*used] = '\0';
        return 0;
    }
    *used += (size_t)written;
    return 1;
}

int wallpaper_enum_darwin(char *buf, size_t buf_size) {
    if (buf == NULL || buf_size == 0) {
        return -1;
    }

    size_t used = 0;
    int hits = 0;
    buf[0] = '\0';

    appendf(buf, buf_size, &used, "[i] Starting wallpaper enumeration...\n");

    @autoreleasepool {
        NSArray<NSScreen *> *screens = [NSScreen screens];
        NSUInteger count = [screens count];

        if (count == 0) {
            appendf(buf, buf_size, &used, "[!] No displays reported by AppKit; a GUI session may be required.\n");
            appendf(buf, buf_size, &used, "[i] Enumeration complete\n");
            appendf(buf, buf_size, &used, "    Wallpaper path hits: 0\n");
            return 0;
        }

        appendf(buf, buf_size, &used, "[i] Found %lu display(s)\n", (unsigned long)count);

        for (NSUInteger i = 0; i < count; i++) {
            NSScreen *screen = [screens objectAtIndex:i];
            NSDictionary *deviceDescription = [screen deviceDescription];
            NSNumber *screenNumber = [deviceDescription objectForKey:@"NSScreenNumber"];
            NSURL *imageURL = [[NSWorkspace sharedWorkspace] desktopImageURLForScreen:screen];

            char display_id[64];
            if (screenNumber != nil) {
                snprintf(display_id, sizeof(display_id), "NSScreen:%llu", [screenNumber unsignedLongLongValue]);
            } else {
                snprintf(display_id, sizeof(display_id), "NSScreen:%lu", (unsigned long)i);
            }

            if (imageURL == nil) {
                appendf(buf, buf_size, &used, "[+] Display %lu (%s): No wallpaper path reported\n",
                        (unsigned long)i, display_id);
                continue;
            }

            NSString *path = [imageURL isFileURL] ? [imageURL path] : [imageURL absoluteString];
            const char *path_c = [path UTF8String];
            if (path_c == NULL || path_c[0] == '\0') {
                appendf(buf, buf_size, &used, "[+] Display %lu (%s): No wallpaper path reported\n",
                        (unsigned long)i, display_id);
                continue;
            }

            hits++;
            appendf(buf, buf_size, &used, "[+] Display %lu (%s): %s\n",
                    (unsigned long)i, display_id, path_c);
        }
    }

    appendf(buf, buf_size, &used, "[i] Enumeration complete\n");
    appendf(buf, buf_size, &used, "    Wallpaper path hits: %d\n", hits);
    return hits;
}
