#include "mod_resource.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

uint8_t *mod_resource_read(const char *root, const char *relative, size_t limit, size_t *size) {
    char path[241];
    size_t length = strlen(relative);
    if (length == 0 || length >= sizeof(path)) return NULL;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)relative[i];
        if (c < 0x20 || c == '\\' || c == ':') return NULL;
    }
    memcpy(path, relative, length + 1);

    int fd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return NULL;
    char *component = path;
    for (;;) {
        char *slash = strchr(component, '/');
        if (slash) *slash = 0;
        if (!*component || !strcmp(component, ".") || !strcmp(component, "..")) {
            close(fd);
            return NULL;
        }
        int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
        if (slash) flags |= O_DIRECTORY;
        int child = openat(fd, component, flags);
        close(fd);
        if (child < 0) return NULL;
        fd = child;
        if (!slash) break;
        component = slash + 1;
    }

    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
        (uintmax_t)info.st_size > limit) {
        close(fd);
        return NULL;
    }
    size_t length_on_disk = (size_t)info.st_size;
    uint8_t *bytes = malloc(length_on_disk);
    size_t offset = 0;
    while (bytes && offset < length_on_disk) {
        ssize_t count = read(fd, bytes + offset, length_on_disk - offset);
        if (count <= 0) break;
        offset += (size_t)count;
    }
    close(fd);
    if (offset != length_on_disk) {
        free(bytes);
        return NULL;
    }
    *size = length_on_disk;
    return bytes;
}
