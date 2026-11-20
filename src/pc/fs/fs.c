#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "macros.h"
#include "../platform.h"
#include "fs.h"

char fs_gamedir[SYS_MAX_PATH] = "";
char fs_writepath[SYS_MAX_PATH] = "";

struct fs_dir_s {
    void *pack;
    const char *realpath;
    fs_packtype_t *packer;
    struct fs_dir_s *prev, *next;
};

extern fs_packtype_t fs_packtype_dir;
extern fs_packtype_t fs_packtype_zip;

static fs_packtype_t *fs_packers[] = {
    &fs_packtype_dir,
    &fs_packtype_zip,
};

static fs_dir_t *fs_searchpaths = NULL;

static inline fs_dir_t *fs_find_dir(const char *realpath) {
    for (fs_dir_t *dir = fs_searchpaths; dir; dir = dir->next)
        if (!sys_strcasecmp(realpath, dir->realpath))
            return dir;
    return NULL;
}

static int mount_cmp(const void *p1, const void *p2) {
    const char *s1 = sys_file_name(*(const char **)p1);
    const char *s2 = sys_file_name(*(const char **)p2);

    // check if one or both of these are basepacks
    const int plen = strlen(FS_BASEPACK_PREFIX);
    const bool is_base1 = !strncmp(s1, FS_BASEPACK_PREFIX, plen);
    const bool is_base2 = !strncmp(s2, FS_BASEPACK_PREFIX, plen);

    // if both are basepacks, compare the postfixes only
    if (is_base1 && is_base2) return strcmp(s1 + plen, s2 + plen);
    // if only one is a basepack, it goes first
    if (is_base1) return -1;
    if (is_base2) return 1;
    // otherwise strcmp order
    return strcmp(s1, s2);
}

static void scan_path_dir(const char *ropath, const char *dir) {
    char dirpath[SYS_MAX_PATH];
    snprintf(dirpath, sizeof(dirpath), "%s/%s", ropath, dir);

    if (!fs_sys_dir_exists(dirpath)) return;

    // since filename order in readdir() isn't guaranteed, collect paths and sort them in strcmp() order
    // (but with basepacks first)
    fs_pathlist_t plist = fs_sys_enumerate(dirpath, false);
    if (plist.paths) {
        qsort(plist.paths, plist.numpaths, sizeof(char *), mount_cmp);
        for (int i = 0; i < plist.numpaths; ++i)
            fs_mount(plist.paths[i]);
        fs_pathlist_free(&plist);
    }

    // mount the directory itself
    fs_mount(dirpath);
}

bool fs_init(const char **rodirs, const char *gamedir, const char *writepath) {
    char buf[SYS_MAX_PATH];

    // expand and remember the write path
    strncpy(fs_writepath, fs_convert_path(buf, sizeof(buf), writepath), sizeof(fs_writepath));
    fs_writepath[sizeof(fs_writepath)-1] = 0;
    printf("fs: writepath set to `%s`\n", fs_writepath);

    // remember the game directory name
    strncpy(fs_gamedir, gamedir, sizeof(fs_gamedir));
    fs_gamedir[sizeof(fs_gamedir)-1] = 0;
    printf("fs: gamedir set to `%s`\n", fs_gamedir);

    // first, scan all possible paths and mount all basedirs in them
    for (const char **p = rodirs; p && *p; ++p)
        scan_path_dir(fs_convert_path(buf, sizeof(buf), *p), FS_BASEDIR);
    scan_path_dir(fs_writepath, FS_BASEDIR);

    // then mount all the gamedirs in them, if the game dir isn't the same
    if (sys_strcasecmp(FS_BASEDIR, fs_gamedir)) {
        for (const char **p = rodirs; p && *p; ++p)
            scan_path_dir(fs_convert_path(buf, sizeof(buf), *p), fs_gamedir);
        scan_path_dir(fs_writepath, fs_gamedir);
    }

    // as a special case, mount writepath itself
    fs_mount(fs_writepath);

    return true;
}

bool fs_mount(const char *realpath) {
    if (fs_find_dir(realpath))
        return false; // already mounted

    const char *ext = sys_file_extension(realpath);
    void *pack = NULL;
    fs_packtype_t *packer = NULL;
    bool tried = false;
    for (unsigned int i = 0; i < sizeof(fs_packers) / sizeof(fs_packers[0]); ++i) {
        if (ext && sys_strcasecmp(ext, fs_packers[i]->extension))
            continue;
        tried = true;
        pack = fs_packers[i]->mount(realpath);
        if (pack) {
            packer = fs_packers[i];
            break;
        }
    }

    if (!pack || !packer) {
        if (tried)
            fprintf(stderr, "fs: could not mount '%s'\n", realpath);
        return false;
    }

    fs_dir_t *dir = calloc(1, sizeof(fs_dir_t));
    if (!dir) {
        packer->unmount(pack);
        return false;
    }

    dir->pack = pack;
    dir->realpath = sys_strdup(realpath);
    dir->packer = packer;
    dir->prev = NULL;
    dir->next = fs_searchpaths;
    if (fs_searchpaths)
        fs_searchpaths->prev = dir;
    fs_searchpaths = dir;

    printf("fs: mounting '%s'\n", realpath);

    return true;
}

bool fs_unmount(const char *realpath) {
    fs_dir_t *dir = fs_find_dir(realpath);
    if (dir) {
        dir->packer->unmount(dir->pack);
        free((void *)dir->realpath);
        if (dir->prev) dir->prev->next = dir->next;
        if (dir->next) dir->next->prev = dir->prev;
        if (dir == fs_searchpaths) fs_searchpaths = dir->next;
        free(dir);
        return true;
    }
    return false;
}

fs_walk_result_t fs_walk(const char *base, walk_fn_t walkfn, void *user, const bool recur) {
    bool found = false;
    for (fs_dir_t *dir = fs_searchpaths; dir; dir = dir->next) {
        fs_walk_result_t res = dir->packer->walk(dir->pack, base, walkfn, user, recur);
        if (res == FS_WALK_INTERRUPTED)
            return res;
        if (res != FS_WALK_NOTFOUND)
            found = true;
    }
    return found ? FS_WALK_SUCCESS : FS_WALK_NOTFOUND;
}

bool fs_is_file(const char *fname) {
    for (fs_dir_t *dir = fs_searchpaths; dir; dir = dir->next) {
        if (dir->packer->is_file(dir->pack, fname))
            return true;
    }
    return false;
}

bool fs_is_dir(const char *fname) {
    for (fs_dir_t *dir = fs_searchpaths; dir; dir = dir->next) {
        if (dir->packer->is_dir(dir->pack, fname))
            return true;
    }
    return false;
}

fs_file_t *fs_open(const char *vpath) {
    for (fs_dir_t *dir = fs_searchpaths; dir; dir = dir->next) {
        fs_file_t *f = dir->packer->open(dir->pack, vpath);
        if (f) {
            f->parent = dir;
            return f;
        }
    }
    return NULL;
}

void fs_close(fs_file_t *file) {
    if (!file) return;
    file->parent->packer->close(file->parent->pack, file);
}

int64_t fs_read(fs_file_t *file, void *buf, const uint64_t size) {
    if (!file) return -1;
    return file->parent->packer->read(file->parent->pack, file, buf, size);
}

bool fs_seek(fs_file_t *file, const int64_t ofs) {
    if (!file) return -1;
    return file->parent->packer->seek(file->parent->pack, file, ofs);
}

int64_t fs_tell(fs_file_t *file) {
    if (!file) return -1;
    return file->parent->packer->tell(file->parent->pack, file);
}

int64_t fs_size(fs_file_t *file) {
    if (!file) return -1;
    return file->parent->packer->size(file->parent->pack, file);
}

bool fs_eof(fs_file_t *file) {
    if (!file) return true;
    return file->parent->packer->eof(file->parent->pack, file);
}

struct matchdata_s {
    const char *prefix;
    size_t prefix_len;
    char *dst;
    size_t dst_len;
};

static bool match_walk(void *user, const char *path) {
    struct matchdata_s *data = (struct matchdata_s *)user;
    if (!strncmp(path, data->prefix, data->prefix_len)) {
        // found our lad, copy path to destination and terminate
        strncpy(data->dst, path, data->dst_len);
        data->dst[data->dst_len - 1] = 0;
        return false;
    }
    return true;
}

const char *fs_match(char *outname, const size_t outlen, const char *prefix) {
    struct matchdata_s data = {
        .prefix = prefix,
        .prefix_len = strlen(prefix),
        .dst = outname,
        .dst_len = outlen,
    };

    if (fs_walk("", match_walk, &data, true) == FS_WALK_INTERRUPTED)
        return outname;

    return NULL;
}

static bool enumerate_walk(void *user, const char *path) {
    fs_pathlist_t *data = (fs_pathlist_t *)user;

    if (data->listcap == data->numpaths) {
        data->listcap *= 2;
        char **newpaths = realloc(data->paths, data->listcap * sizeof(char *));
        if (!newpaths) return false;
        data->paths = newpaths;
    }

    data->paths[data->numpaths++] = sys_strdup(path);
    return true;
}

fs_pathlist_t fs_enumerate(const char *base, const bool recur) {
    char **paths = malloc(sizeof(char *) * 32);
    fs_pathlist_t pathlist = { paths, 0, 32 };

    if (!paths) return pathlist;

    if (fs_walk(base, enumerate_walk, &pathlist, recur) == FS_WALK_INTERRUPTED)
        fs_pathlist_free(&pathlist);

    return pathlist;
}

void fs_pathlist_free(fs_pathlist_t *pathlist) {
    if (!pathlist || !pathlist->paths) return;
    for (int i = 0; i < pathlist->numpaths; ++i)
        free(pathlist->paths[i]);
    free(pathlist->paths);
    pathlist->paths = NULL;
    pathlist->numpaths = 0;
}

const char *fs_readline(fs_file_t *file, char *dst, uint64_t size) {
    int64_t rx = 0;
    char chr, *p;

    // assume we got buffered input
    for (p = dst, size--; size > 0; size--) {
        if ((rx = fs_read(file, &chr, 1)) <= 0)
            break;
        *p++ = chr;
        if (chr == '\n')
            break;
    }

    *p = 0;
    if (p == dst || rx <= 0)
        return NULL;

    return p;
}

void *fs_load_file(const char *vpath, uint64_t *outsize) {
    return fs_load_file_bounded(vpath, SIZE_MAX, outsize);
}

void *fs_load_file_bounded(const char *vpath, uint64_t max_size, uint64_t *outsize) {
    fs_file_t *f = fs_open(vpath);
    if (!f) return NULL;

    int64_t size = fs_size(f);
    if (size <= 0 || (uint64_t)size > max_size || (uint64_t)size > SIZE_MAX) {
        fs_close(f);
        return NULL;
    }

    void *buf = malloc(size);
    if (!buf) {
        fs_close(f);
        return NULL;
    }

    int64_t rx = fs_read(f, buf, size);
    fs_close(f);

    if (rx < size) {
        free(buf);
        return NULL;
    }

    if (outsize) *outsize = size;
    return buf;
}

static bool fs_write_relative_path_is_safe(const char *path) {
    if (!path || !*path || path[0] == '/' || path[0] == '\\') return false;
    const char *segment = path;
    for (const unsigned char *p = (const unsigned char *)path;; ++p) {
        unsigned char c = *p;
        if (c == '\\' || c == ':' || (c != '\0' && c < 0x20)) return false;
        if (c == '/' || c == '\0') {
            size_t length = (size_t)((const char *)p - segment);
            if (length == 0 || (length == 1 && segment[0] == '.') ||
                (length == 2 && segment[0] == '.' && segment[1] == '.')) return false;
            if (c == '\0') break;
            segment = (const char *)p + 1;
        }
    }
    return true;
}

void *fs_load_write_file(const char *relative_path, uint64_t max_size, uint64_t *outsize) {
    if (!fs_writepath[0] || !fs_write_relative_path_is_safe(relative_path)) return NULL;
    char path[SYS_MAX_PATH];
    int written = snprintf(path, sizeof(path), "%s/%s", fs_writepath, relative_path);
    if (written <= 0 || (size_t)written >= sizeof(path)) return NULL;

    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long file_size = ftell(file);
    if (file_size <= 0 || (uint64_t)file_size > max_size ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    void *buffer = malloc((size_t)file_size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t bytes_read = fread(buffer, 1, (size_t)file_size, file);
    bool read_ok = bytes_read == (size_t)file_size && !ferror(file);
    fclose(file);
    if (!read_ok) {
        free(buffer);
        return NULL;
    }
    if (outsize) *outsize = (uint64_t)file_size;
    return buffer;
}

/* External world assets share a bounded, versioned Documents library. Legacy
 * Documents paths retain priority. Cache only the small index, never all PNGs. */
static char *scene_library_index;
static uint64_t scene_library_index_bytes;

void fs_scene_library_reset(void) {
    free(scene_library_index);
    scene_library_index = NULL;
    scene_library_index_bytes = 0;
}

static void *fs_load_library_file(const char *path, uint64_t max_size, uint64_t *outsize) {
#ifndef _WIN32
    char absolute[SYS_MAX_PATH];
    int n = snprintf(absolute, sizeof(absolute), "%s/%s", fs_writepath, path);
    if (n <= 0 || (size_t)n >= sizeof(absolute)) return NULL;
    /* Reject symlink components below Documents, including the library root. */
    for (char *p = absolute + strlen(fs_writepath) + 1; ; ++p) {
        if (*p == '/' || *p == 0) {
            char saved = *p;
            *p = 0;
            struct stat status;
            int result = lstat(absolute, &status);
            *p = saved;
            if (result || S_ISLNK(status.st_mode)) return NULL;
            if (!saved) break;
        }
    }
#else
    /* Reparse-point containment has no tested implementation on Windows. */
    return NULL;
#endif
    return fs_load_write_file(path, max_size, outsize);
}

void *fs_load_scene_resource(const char *relative_path, uint64_t max_size, uint64_t *outsize) {
    if (!fs_write_relative_path_is_safe(relative_path) ||
        (strncmp(relative_path, "gfx/scenes/", 11) && strncmp(relative_path, "scenes/", 7))) return NULL;
    void *legacy = fs_load_write_file(relative_path, max_size, outsize);
    if (legacy) return legacy;
    if (!scene_library_index) {
        uint64_t size = 0;
        char *raw = fs_load_library_file("oot-assets/runtime-index.tsv", 4 * 1024 * 1024, &size);
        if (!raw) return NULL;
        char *index = malloc(size + 1);
        if (!index) { free(raw); return NULL; }
        memcpy(index, raw, size);
        free(raw);
        index[size] = 0;
        if (size < 8 || memcmp(index, "OOTLIB1\n", 8) || memchr(index, 0, size)) {
            fprintf(stderr, "OoT library: invalid runtime index version or encoding\n");
            free(index);
            return NULL;
        }
        scene_library_index = index;
        scene_library_index_bytes = size;
    }
    const char *line = scene_library_index + 8;
    const char *end = scene_library_index + scene_library_index_bytes;
    while (line < end) {
        const char *newline = memchr(line, '\n', end - line);
        if (!newline) break;
        const char *tab = memchr(line, '\t', newline - line);
        if (!tab) break;
        const char *second = memchr(tab + 1, '\t', newline - tab - 1);
        if (!second) break;
        if ((size_t)(tab - line) == strlen(relative_path) && !memcmp(line, relative_path, tab - line)) {
            char target[512], number[32], *number_end;
            size_t target_size = second - tab - 1, number_size = newline - second - 1;
            if (!target_size || target_size >= sizeof(target) || !number_size || number_size >= sizeof(number)) break;
            memcpy(target, tab + 1, target_size); target[target_size] = 0;
            memcpy(number, second + 1, number_size); number[number_size] = 0;
            unsigned long long expected = strtoull(number, &number_end, 10);
            if (*number_end || number[0] < '0' || number[0] > '9' || !expected || expected > max_size ||
                !fs_write_relative_path_is_safe(target) ||
                (strncmp(target, "gfx/scenes/", 11) && strncmp(target, "scenes/", 7))) break;
            char full[sizeof(target) + 16];
            snprintf(full, sizeof(full), "oot-assets/%s", target);
            uint64_t actual = 0;
            void *data = fs_load_library_file(full, max_size, &actual);
            if (data && actual == expected) { if (outsize) *outsize = actual; return data; }
            free(data);
            break;
        }
        line = newline + 1;
    }
    fprintf(stderr, "OoT library resource missing or invalid: %s\n", relative_path);
    return NULL;
}

const char *fs_get_write_path(const char *vpath) {
    static char path[SYS_MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", fs_writepath, vpath);
    return path;
}

const char *fs_convert_path(char *buf, const size_t bufsiz, const char *path)  {
    // ! means "executable directory"
    if (path[0] == '!') {
        snprintf(buf, bufsiz, "%s%s", sys_exe_path(), path + 1);
    } else {
        strncpy(buf, path, bufsiz);
        buf[bufsiz-1] = 0;
    }

    // change all backslashes
    for (char *p = buf; *p; ++p)
        if (*p == '\\') *p = '/';

    return buf;
}

/* these operate on the real file system */

bool fs_sys_file_exists(const char *name) {
    struct stat st;
    return (stat(name, &st) == 0 && S_ISREG(st.st_mode));
}

bool fs_sys_dir_exists(const char *name) {
    struct stat st;
    return (stat(name, &st) == 0 && S_ISDIR(st.st_mode));
}

bool fs_sys_walk(const char *base, walk_fn_t walk, void *user, const bool recur) {
    char fullpath[SYS_MAX_PATH];
    DIR *dir;
    struct dirent *ent;

    if (!(dir = opendir(base))) {
        fprintf(stderr, "fs_dir_walk(): could not open `%s`\n", base);
        return false;
    }

    bool ret = true;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == 0 || ent->d_name[0] == '.') continue; // skip ./.. and hidden files
        snprintf(fullpath, sizeof(fullpath), "%s/%s", base, ent->d_name);
        if (fs_sys_dir_exists(fullpath)) {
            if (recur) {
                if (!fs_sys_walk(fullpath, walk, user, recur)) {
                    ret = false;
                    break;
                }
            }
        } else {
            if (!walk(user, fullpath)) {
                ret = false;
                break;
            }
        }
    }

    closedir(dir);
    return ret;
}

fs_pathlist_t fs_sys_enumerate(const char *base, const bool recur) {
    char **paths = malloc(sizeof(char *) * 32);
    fs_pathlist_t pathlist = { paths, 0, 32 };

    if (!paths) return pathlist;

    if (!fs_sys_walk(base, enumerate_walk, &pathlist, recur))
        fs_pathlist_free(&pathlist);

    return pathlist;
}

bool fs_sys_mkdir(const char *name) {
    #ifdef _WIN32
    return _mkdir(name) == 0;
    #else
    return mkdir(name, 0777) == 0;
    #endif
}

bool fs_sys_copy_file(const char *oldname, const char *newname) {
    uint8_t buf[2048];

    FILE *fin = fopen(oldname, "rb");
    if (!fin) return false;

    FILE *fout = fopen(newname, "wb");
    if (!fout) {
        fclose(fin);
        return false;
    }

    bool ret = true;
    size_t rx;
    while ((rx = fread(buf, 1, sizeof(buf), fin)) > 0) {
        if (!fwrite(buf, rx, 1, fout)) {
            ret = false;
            break;
        }
    }

    fclose(fout);
    fclose(fin);

    return ret;
}
