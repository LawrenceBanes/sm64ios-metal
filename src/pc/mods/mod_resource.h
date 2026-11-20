#ifndef SM64IOS_MOD_RESOURCE_H
#define SM64IOS_MOD_RESOURCE_H

#include <stddef.h>
#include <stdint.h>

/* Read a regular file relative to an installed mod. No component may be a
 * symlink. The caller owns the returned buffer. */
uint8_t *mod_resource_read(const char *root, const char *relative, size_t limit, size_t *size);

#endif
