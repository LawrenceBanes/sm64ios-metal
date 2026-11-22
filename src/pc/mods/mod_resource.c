#include "mod_resource.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int open_mod_resource_parent(const char *root, const char *relative,
                                   char *path, size_t path_size, char **leaf) {
    if (!root || !relative || !path || !leaf) return -1;
    size_t length = strlen(relative);
    if (length == 0 || length >= path_size) return -1;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)relative[i];
        if (c < 0x20 || c == '\\' || c == ':') return -1;
    }
    memcpy(path, relative, length + 1);
    int fd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    char *component = path;
    for (;;) {
        char *slash = strchr(component, '/');
        if (slash) *slash = 0;
        if (!*component || !strcmp(component, ".") || !strcmp(component, "..")) {
            close(fd);
            return -1;
        }
        if (!slash) {
            *leaf = component;
            return fd;
        }
        int child = openat(fd, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(fd);
        if (child < 0) return -1;
        fd = child;
        component = slash + 1;
    }
}

int mod_resource_exists(const char *root, const char *relative) {
    char path[241];
    char *leaf = NULL;
    int parent = open_mod_resource_parent(root, relative, path, sizeof(path), &leaf);
    if (parent < 0) return 0;
    int fd = openat(parent, leaf, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    close(parent);
    if (fd < 0) return 0;
    struct stat info;
    int exists = fstat(fd, &info) == 0 && S_ISREG(info.st_mode);
    close(fd);
    return exists;
}

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
