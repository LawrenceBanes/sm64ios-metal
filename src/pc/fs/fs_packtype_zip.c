#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <tinfl.h>

#include "macros.h"
#include "../platform.h"
#include "fs.h"
#include "dirtree.h"

#define ZIP_BUFSIZE 16384
#define ZIP_EOCD_BUFSIZE 65578

#define ZIP_LFH_SIG 0x04034b50
#define ZIP_CDH_SIG  0x02014b50
#define ZIP_EOCD_SIG 0x06054b50

typedef struct {
    fs_dirtree_t tree;    // this should always be first, so this could be used as a dirtree root
    const char *realpath; // physical path to the zip file
    FILE *zipf;           // open zip file handle, if any
} zip_pack_t;

typedef struct {
    fs_dirtree_entry_t tree; // this should always be first, so this could be used as a dirtree entry
    uint64_t ofs;            // offset to compressed data in zip
    uint16_t bits;           // general purpose zip flags
    uint16_t comptype;       // compression method
    uint32_t crc;            // CRC-32
    uint64_t comp_size;      // size of compressed data in zip
    uint64_t uncomp_size;    // size of decompressed data
    uint16_t attr_int;       // internal attributes
    uint32_t attr_ext;       // external attributes
    bool     ofs_fixed;      // if true, `ofs` points to the file data, otherwise to LFH
    bool     explicit_entry; // false for directory ancestors synthesized by the tree
} zip_entry_t;

typedef struct {
    zip_entry_t *entry;  // the dirtree entry of this file
    uint32_t comp_pos;   // read position in compressed data
    uint32_t uncomp_pos; // read position in uncompressed data
    uint8_t *buffer;     // decompression buffer (if compressed)
    z_stream zstream;    // tinfl zlib stream
    FILE *fstream;       // duplicate of zipf of the parent zip file
} zip_file_t;

static int64_t zip_find_eocd(FILE *f, int64_t *outlen) {
    // the EOCD is somewhere in the last 65557 bytes of the file
    // get the total file size
    fseek(f, 0, SEEK_END);
    const int64_t fsize = ftell(f);
    if (fsize <= 16) return -1; // probably not a zip

    const int64_t rx = (fsize < ZIP_EOCD_BUFSIZE ? fsize : ZIP_EOCD_BUFSIZE);
    uint8_t *buf = malloc(rx);
    if (!buf) return -1;

    // read that entire chunk and search for EOCD backwards from the end
    fseek(f, fsize - rx, SEEK_SET);
    if (fread(buf, rx, 1, f)) {
        for (int64_t i = rx - 8; i >= 0; --i) {
            if ((buf[i + 0] == 0x50) && (buf[i + 1] == 0x4B) &&
                (buf[i + 2] == 0x05) && (buf[i + 3] == 0x06)) {
                // gotem
                free(buf);
                if (outlen) *outlen = fsize;
                return fsize - rx + i;
            }
        }
    }

    free(buf);
    return -1;
}

static bool zip_parse_eocd(FILE *f, uint64_t *cdir_ofs, uint64_t *data_ofs, uint64_t *count) {
    int64_t fsize = 0;

    // EOCD record struct
    struct eocd_s {
        uint32_t sig;
        uint16_t this_disk;
        uint16_t cdir_disk;
        uint16_t disk_entry_count;
        uint16_t total_entry_count;
        uint32_t cdir_size;
        uint32_t cdir_ofs;
        uint16_t comment_len;
        // zip comment follows
    } __attribute__((__packed__));
    struct eocd_s eocd;

    // find the EOCD and seek to it
    int64_t pos = zip_find_eocd(f, &fsize);
    if (pos < 0) return false;
    fseek(f, pos, SEEK_SET);

    // read it
    if (!fread(&eocd, sizeof(eocd), 1, f)) return false;

    // double check the sig
    if (LE_TO_HOST32(eocd.sig) != ZIP_EOCD_SIG) return false;

    // disks should all be 0
    if (eocd.this_disk || eocd.cdir_disk) return false;

    // total entry count should be the same as disk entry count
    if (eocd.disk_entry_count != eocd.total_entry_count) return false;

    *count = LE_TO_HOST16(eocd.total_entry_count);
    *cdir_ofs = LE_TO_HOST32(eocd.cdir_ofs);
    eocd.cdir_size = LE_TO_HOST32(eocd.cdir_size);

    // end of central dir can't be before central dir
    if ((uint64_t)pos < *cdir_ofs + eocd.cdir_size) return false;

    *data_ofs = (uint64_t)(pos - (*cdir_ofs + eocd.cdir_size));
    *cdir_ofs += *data_ofs;

    // make sure end of comment matches end of file
    eocd.comment_len = LE_TO_HOST16(eocd.comment_len);
    return ((pos + 22 + eocd.comment_len) == fsize);
}

static bool zip_fixup_offset(zip_file_t *zipfile) {
    // LFH record struct
    struct lfh_s {
        uint32_t sig;
        uint16_t version_required;
        uint16_t bits;
        uint16_t comptype;
        uint16_t mod_time;
        uint16_t mod_date;
        uint32_t crc;
        uint32_t comp_size;
        uint32_t uncomp_size;
        uint16_t fname_len;
        uint16_t extra_len;
        // file name, extra field and data follow
    } __attribute__((__packed__));

    struct lfh_s lfh;

    zip_entry_t *ent = zipfile->entry;
    if (fseek(zipfile->fstream, 0, SEEK_END) != 0) return false;
    long file_size = ftell(zipfile->fstream);
    if (file_size < 0 || ent->ofs > (uint64_t)file_size || ent->ofs + sizeof(lfh) > (uint64_t)file_size) return false;
    fseek(zipfile->fstream, ent->ofs, SEEK_SET);
    if (!fread(&lfh, sizeof(lfh), 1, zipfile->fstream)) return false;

    if (LE_TO_HOST32(lfh.sig) != ZIP_LFH_SIG) return false;
    uint16_t local_bits = LE_TO_HOST16(lfh.bits);
    uint16_t local_type = LE_TO_HOST16(lfh.comptype);
    uint32_t local_crc = LE_TO_HOST32(lfh.crc);
    uint32_t local_comp_size = LE_TO_HOST32(lfh.comp_size);
    uint32_t local_uncomp_size = LE_TO_HOST32(lfh.uncomp_size);
    lfh.fname_len = LE_TO_HOST16(lfh.fname_len);
    lfh.extra_len = LE_TO_HOST16(lfh.extra_len);
    if (local_bits != ent->bits || local_type != ent->comptype) return false;
    uint64_t header_len = sizeof(lfh) + (uint64_t)lfh.fname_len + lfh.extra_len;
    if (header_len > (uint64_t)file_size - ent->ofs) return false;
    if (ent->ofs + header_len > (uint64_t)file_size ||
        ent->comp_size > (uint64_t)file_size - (ent->ofs + header_len)) return false;
    if (!(ent->bits & (1u << 3)) &&
        (local_crc != ent->crc || local_comp_size != ent->comp_size || local_uncomp_size != ent->uncomp_size)) return false;
    char *local_name = malloc((size_t)lfh.fname_len + 1);
    if (!local_name) return false;
    if (fseek(zipfile->fstream, ent->ofs + sizeof(lfh), SEEK_SET) != 0 ||
        fread(local_name, lfh.fname_len, 1, zipfile->fstream) != 1) {
        free(local_name);
        return false;
    }
    local_name[lfh.fname_len] = 0;
    bool same_name = !strcmp(local_name, ent->tree.name);
    free(local_name);
    if (!same_name) return false;

    // ofs will now point to actual data
    ent->ofs += header_len;
    ent->ofs_fixed = true; // only need to do this once

    return true;
}

static zip_entry_t *zip_load_entry(FILE *f, fs_dirtree_t *tree, const uint64_t data_ofs) {
    // CDH record struct
    struct cdh_s {
        uint32_t sig;
        uint16_t version_used;
        uint16_t version_required;
        uint16_t bits;
        uint16_t comptype;
        uint16_t mod_time;
        uint16_t mod_date;
        uint32_t crc;
        uint32_t comp_size;
        uint32_t uncomp_size;
        uint16_t fname_len;
        uint16_t extra_len;
        uint16_t comment_len;
        uint16_t start_disk;
        uint16_t attr_int;
        uint32_t attr_ext;
        uint32_t lfh_ofs;
        // file name, extra field and comment follow
    } __attribute__((__packed__));

    struct cdh_s cdh;
    zip_entry_t zipent;

    memset(&zipent, 0, sizeof(zipent));

    if (!fread(&cdh, sizeof(cdh), 1, f)) return NULL;

    // check cdir entry header signature
    if (LE_TO_HOST32(cdh.sig) != ZIP_CDH_SIG) return NULL;

    // byteswap and copy some important fields
    zipent.bits = LE_TO_HOST16(cdh.bits);
    zipent.comptype = LE_TO_HOST16(cdh.comptype);
    zipent.crc = LE_TO_HOST32(cdh.crc);
    zipent.comp_size = LE_TO_HOST32(cdh.comp_size);
    zipent.uncomp_size = LE_TO_HOST32(cdh.uncomp_size);
    zipent.ofs = LE_TO_HOST32(cdh.lfh_ofs);
    zipent.attr_int = LE_TO_HOST16(cdh.attr_int);
    zipent.attr_ext = LE_TO_HOST32(cdh.attr_ext);
    cdh.fname_len = LE_TO_HOST16(cdh.fname_len);
    cdh.comment_len = LE_TO_HOST16(cdh.comment_len);
    cdh.extra_len = LE_TO_HOST16(cdh.extra_len);

    if (cdh.fname_len == 0) return NULL;

    // read the name
    char *name = calloc(1, cdh.fname_len + 1);
    if (!name) return NULL;
    if (!fread(name, cdh.fname_len, 1, f)) { free(name); return NULL; }

    // this is a directory if the name ends in a path separator
    bool is_dir = false;
    if (name[cdh.fname_len - 1] == '/') {
        is_dir = true;
        name[cdh.fname_len - 1] = 0;
    }
    name[cdh.fname_len] = 0;

    zip_entry_t *existing = (zip_entry_t *)fs_dirtree_find(tree, name);
    if (existing) {
        if (is_dir && existing->tree.is_dir && !existing->explicit_entry) {
            existing->explicit_entry = true;
            free(name);
            return existing;
        }
        free(name);
        return NULL;
    }

    // add to directory tree
    zip_entry_t *retent = (zip_entry_t *)fs_dirtree_add(tree, name, is_dir);
    free(name);
    if (!retent) return NULL;

    // copy the data we read into the new entry
    zipent.tree = retent->tree;
    zipent.explicit_entry = true;
    memcpy(retent, &zipent, sizeof(zipent));

    // this points to the LFH now; will be fixed up on file open
    // while the CDH includes an "extra field length" field, it's usually different
    retent->ofs += data_ofs;

    // skip to the next CDH
    fseek(f, cdh.extra_len + cdh.comment_len, SEEK_CUR);

    return retent;
}

static inline bool zip_load_entries(FILE *f, fs_dirtree_t *tree, const uint64_t cdir_ofs, const uint64_t data_ofs, const uint64_t count) {
    fseek(f, cdir_ofs, SEEK_SET);
    for (uint64_t i = 0; i < count; ++i) {
        if (!zip_load_entry(f, tree, data_ofs))
            return false;
    }
    return true;
}

static inline bool is_zip(FILE *f) {
    uint32_t sig = 0;
    if (fread(&sig, sizeof(sig), 1, f)) {
        // the first LFH might be at the start of the zip
        if (LE_TO_HOST32(sig) == ZIP_LFH_SIG)
            return true;
        // no signature, might still be a zip because fuck you
        // the only way now is to try and find the end of central directory
        return zip_find_eocd(f, NULL) >= 0;
    }
    return false;
}

static void *pack_zip_mount(const char *realpath) {
    uint64_t cdir_ofs, data_ofs, count;
    zip_pack_t *pack = NULL;
    FILE *f = NULL;

    f = fopen(realpath, "rb");
    if (!f) goto _fail;

    if (!is_zip(f)) goto _fail;

    pack = calloc(1, sizeof(zip_pack_t));
    if (!pack) goto _fail;

    if (!zip_parse_eocd(f, &cdir_ofs, &data_ofs, &count))
        goto _fail;

    if (!fs_dirtree_init(&pack->tree, sizeof(zip_entry_t)))
        goto _fail;

    if (!zip_load_entries(f, &pack->tree, cdir_ofs, data_ofs, count))
        goto _fail;

    pack->realpath = sys_strdup(realpath);
    pack->zipf = f;

    return pack;

_fail:
    if (f) fclose(f);
    if (pack) free(pack);
    return NULL;
}

static void pack_zip_unmount(void *pack) {
    zip_pack_t *zip = (zip_pack_t *)pack;
    fs_dirtree_free(&zip->tree);
    if (zip->realpath) free((void *)zip->realpath);
    if (zip->zipf) fclose(zip->zipf);
    free(zip);
}

static bool pack_zip_is_file(void *pack, const char *fname) {
    zip_entry_t *ent = (zip_entry_t *)fs_dirtree_find((fs_dirtree_t *)pack, fname);
    return ent && !ent->tree.is_dir;
}

static bool pack_zip_is_dir(void *pack, const char *fname) {
    zip_entry_t *ent = (zip_entry_t *)fs_dirtree_find((fs_dirtree_t *)pack, fname);
    return ent && ent->tree.is_dir;
}

static inline void pack_zip_close_zipfile(zip_file_t *zipfile) {
    if (zipfile->buffer) {
        inflateEnd(&zipfile->zstream);
        free(zipfile->buffer);
    }
    if (zipfile->fstream) fclose(zipfile->fstream);
    free(zipfile);
}

static fs_file_t *pack_zip_open(void *pack, const char *vpath) {
    fs_file_t *fsfile = NULL;
    zip_file_t *zipfile = NULL;
    zip_pack_t *zip = (zip_pack_t *)pack;
    zip_entry_t *ent = (zip_entry_t *)fs_dirtree_find((fs_dirtree_t *)zip, vpath);
    if (!ent || ent->tree.is_dir) goto _fail; // we're expecting a fucking file here

    zipfile = calloc(1, sizeof(zip_file_t));
    if (!zipfile) goto _fail;
    zipfile->entry = ent;

    // obtain an additional file descriptor
    // fdopen(dup(fileno())) is not very portable and might not create separate state
    zipfile->fstream = fopen(zip->realpath, "rb");
    if (!zipfile->fstream) goto _fail;

    // make ent->ofs point to the actual file data if it doesn't already
    if (!ent->ofs_fixed)
        if (!zip_fixup_offset(zipfile))
            goto _fail; // this shouldn't generally happen but oh well

    // if there's compression, assume it's zlib
    if (ent->comptype != 0) {
        zipfile->buffer = malloc(ZIP_BUFSIZE);
        if (!zipfile->buffer)
            goto _fail;
        if (inflateInit2(&zipfile->zstream, -MAX_WBITS) != Z_OK)
            goto _fail;
    }

    fsfile = malloc(sizeof(fs_file_t));
    if (!fsfile) goto _fail;
    fsfile->handle = zipfile;
    fsfile->parent = NULL;

    // point to the start of the file data
    fseek(zipfile->fstream, ent->ofs, SEEK_SET);

    return fsfile;

_fail:
    if (zipfile) pack_zip_close_zipfile(zipfile);
    if (fsfile) free(fsfile);
    return NULL;
}

static void pack_zip_close(UNUSED void *pack, fs_file_t *file) {
    if (!file) return;

    zip_file_t *zipfile = (zip_file_t *)file->handle;
    if (zipfile) pack_zip_close_zipfile(zipfile);

    free(file);
}

static int64_t pack_zip_read(UNUSED void *pack, fs_file_t *file, void *buf, const uint64_t size) {
    zip_file_t *zipfile = (zip_file_t *)file->handle;
    zip_entry_t *ent = zipfile->entry;

    int64_t avail = ent->uncomp_size - zipfile->uncomp_pos;
    int64_t max_read = ((int64_t)size > avail) ? avail : (int64_t)size;
    int64_t rx = 0;
    int err = 0;

    if (max_read == 0) return 0;

    if (ent->comptype == 0) {
        // no compression, just read
        rx = fread(buf, 1, size, zipfile->fstream);
    } else {
        zipfile->zstream.next_out = buf;
        zipfile->zstream.avail_out = (unsigned int)max_read;
        while (rx < max_read) {
            const uint32_t before = (uint32_t)zipfile->zstream.total_out;
            // check if we ran out of compressed bytes and read more if we did
            if (zipfile->zstream.avail_in == 0) {
                int32_t comp_rx = ent->comp_size - zipfile->comp_pos;
                if (comp_rx > 0) {
                    if (comp_rx > ZIP_BUFSIZE) comp_rx = ZIP_BUFSIZE;
                    comp_rx = fread(zipfile->buffer, 1, comp_rx, zipfile->fstream);
                    if (comp_rx == 0) break;
                    zipfile->comp_pos += (uint32_t)comp_rx;
                    zipfile->zstream.next_in = zipfile->buffer;
                    zipfile->zstream.avail_in = (unsigned int)comp_rx;
                }
            }
            // inflate
            err = inflate(&zipfile->zstream, Z_SYNC_FLUSH);
            rx += zipfile->zstream.total_out - before;
            if (err != Z_OK) break;
        }
    }

    zipfile->uncomp_pos += rx;
    return rx;
}

static bool pack_zip_seek(UNUSED void *pack, fs_file_t *file, const int64_t ofs) {
    zip_file_t *zipfile = (zip_file_t *)file->handle;
    zip_entry_t *ent = zipfile->entry;
    uint8_t buf[512];

    if (ofs > (int64_t)ent->uncomp_size) return false;

    if (ent->comptype == 0) {
        if (fseek(zipfile->fstream, ofs + ent->ofs, SEEK_SET) == 0)
            zipfile->uncomp_pos = ofs;
    } else {
        // if seeking backwards, gotta redecode the stream from the start until that point
        // so we make a copy of the zstream and clear it with a new one
        if (ofs < zipfile->uncomp_pos) {
            z_stream zstream;
            memset(&zstream, 0, sizeof(zstream));
            if (inflateInit2(&zstream, -MAX_WBITS) != Z_OK)
                return false;
            // reset the underlying file handle back to the start
            if (fseek(zipfile->fstream, ent->ofs, SEEK_SET) != 0)
                return false;
            // free and replace the old one
            inflateEnd(&zipfile->zstream);
            memcpy(&zipfile->zstream, &zstream, sizeof(zstream));
            zipfile->uncomp_pos = zipfile->comp_pos = 0;
        }
        // continue decoding the stream until we hit the new offset
        while (zipfile->uncomp_pos != ofs) {
            uint32_t max_read = (uint32_t)(ofs - zipfile->uncomp_pos);
            if (max_read > sizeof(buf)) max_read = sizeof(buf);
            if (pack_zip_read(pack, file, buf, max_read) != max_read)
                return false;
        }
    }

    return true;
}

static int64_t pack_zip_tell(UNUSED void *pack, fs_file_t *file) {
    return ((zip_file_t *)file->handle)->uncomp_pos;
}

static int64_t pack_zip_size(UNUSED void *pack, fs_file_t *file) {
    zip_file_t *zipfile = (zip_file_t *)file->handle;
    return zipfile->entry->uncomp_size;
}

static bool pack_zip_eof(UNUSED void *pack, fs_file_t *file) {
    zip_file_t *zipfile = (zip_file_t *)file->handle;
    return zipfile->uncomp_pos >= zipfile->entry->uncomp_size;
}

fs_packtype_t fs_packtype_zip = {
    "zip",
    pack_zip_mount,
    pack_zip_unmount,
    fs_dirtree_walk,
    pack_zip_is_file,
    pack_zip_is_dir,
    pack_zip_open,
    pack_zip_read,
    pack_zip_seek,
    pack_zip_tell,
    pack_zip_size,
    pack_zip_eof,
    pack_zip_close,
};

typedef struct {
    zip_pack_t *pack;
    zip_entry_t **entries;
    char **names;
    size_t count;
    uint64_t total_size;
    size_t common_root_len;
    char common_root[256];
    bool root_initialized;
    bool has_common_root;
    bool failed;
    char error[256];
} zip_import_ctx_t;

static bool zip_import_path_safe(const char *path) {
    if (!path || !*path || path[0] == '/' || path[0] == '\\') return false;
    const char *segment = path;
    for (const unsigned char *p = (const unsigned char *)path;; ++p) {
        unsigned char c = *p;
        if (c == ':' || c == '\\' || (c && c < 0x20)) return false;
        if (c == '/' || c == 0) {
            size_t n = (const char *)p - segment;
            if (!n || (n == 1 && segment[0] == '.') || (n == 2 && segment[0] == '.' && segment[1] == '.')) return false;
            if (!c) break;
            segment = (const char *)p + 1;
        }
    }
    return strlen(path) < SYS_MAX_PATH - 1;
}

static bool zip_import_add(void *userdata, const char *name) {
    zip_import_ctx_t *ctx = userdata;
    zip_entry_t *entry = (zip_entry_t *)fs_dirtree_find(&ctx->pack->tree, name);
    if (!entry || entry->tree.is_dir || !zip_import_path_safe(name)) {
        snprintf(ctx->error, sizeof(ctx->error), "unsafe ZIP path: %.180s", name ? name : "(null)");
        ctx->failed = true;
        return false;
    }
    if (ctx->count >= 1024) {
        snprintf(ctx->error, sizeof(ctx->error), "ZIP contains more than 1024 files");
        ctx->failed = true;
        return false;
    }
    if ((entry->bits & 1) || (entry->comptype != 0 && entry->comptype != 8)) {
        snprintf(ctx->error, sizeof(ctx->error), "encrypted or unsupported ZIP compression: %.160s", name);
        ctx->failed = true;
        return false;
    }
    if (entry->comp_size == UINT32_MAX || entry->uncomp_size == UINT32_MAX ||
        entry->uncomp_size > 64ull * 1024ull * 1024ull ||
        ctx->total_size > 256ull * 1024ull * 1024ull - entry->uncomp_size) {
        snprintf(ctx->error, sizeof(ctx->error), "ZIP resource size limit exceeded: %.160s", name);
        ctx->failed = true;
        return false;
    }
    uint32_t unix_mode = (entry->attr_ext >> 16) & 0170000;
    if (unix_mode == 0120000) {
        snprintf(ctx->error, sizeof(ctx->error), "symbolic links are not allowed: %.150s", name);
        ctx->failed = true;
        return false;
    }
    if (entry->uncomp_size && !entry->comp_size) {
        snprintf(ctx->error, sizeof(ctx->error), "invalid compressed size for %.170s", name);
        ctx->failed = true;
        return false;
    }
    if (entry->comp_size && entry->uncomp_size / entry->comp_size > 250) {
        snprintf(ctx->error, sizeof(ctx->error), "compression ratio exceeds limit: %.160s", name);
        ctx->failed = true;
        return false;
    }
    for (size_t i = 0; i < ctx->count; ++i) {
        if (!strcasecmp(ctx->names[i], name)) {
            snprintf(ctx->error, sizeof(ctx->error), "duplicate ZIP path: %.100s vs %.100s", ctx->names[i], name);
            ctx->failed = true;
            return false;
        }
    }
    zip_entry_t **new_entries = realloc(ctx->entries, sizeof(*ctx->entries) * (ctx->count + 1));
    char **new_names = realloc(ctx->names, sizeof(*ctx->names) * (ctx->count + 1));
    if (!new_entries || !new_names) {
        if (new_entries) ctx->entries = new_entries;
        if (new_names) ctx->names = new_names;
        snprintf(ctx->error, sizeof(ctx->error), "out of memory validating mod ZIP");
        ctx->failed = true;
        return false;
    }
    ctx->entries = new_entries; ctx->names = new_names;
    ctx->entries[ctx->count] = entry;
    ctx->names[ctx->count] = strdup(name);
    if (!ctx->names[ctx->count]) { ctx->failed = true; return false; }
    ctx->count++;
    ctx->total_size += entry->uncomp_size;
    if (strchr(name, '/') == NULL) {
        ctx->has_common_root = false;
        ctx->root_initialized = true;
    } else if (!ctx->root_initialized) {
        size_t n = (size_t)(strchr(name, '/') - name);
        if (n >= sizeof(ctx->common_root)) ctx->has_common_root = false;
        else { memcpy(ctx->common_root, name, n); ctx->common_root[n] = 0; ctx->common_root_len = n; ctx->has_common_root = true; }
        ctx->root_initialized = true;
    } else if (ctx->has_common_root) {
        size_t n = (size_t)(strchr(name, '/') - name);
        if (n != ctx->common_root_len || strncmp(name, ctx->common_root, n)) ctx->has_common_root = false;
    }
    return true;
}

static uint32_t zip_crc32_update(uint32_t crc, const uint8_t *data, size_t size) {
    crc = ~crc;
    while (size--) {
        crc ^= *data++;
        for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
    }
    return ~crc;
}

static bool zip_mkdir_parents(const char *path) {
    char buffer[SYS_MAX_PATH];
    if (strlen(path) >= sizeof(buffer)) return false;
    strcpy(buffer, path);
    for (char *p = buffer + 1; *p; ++p) {
        if (*p == '/') {
            *p = 0;
            if (!fs_sys_dir_exists(buffer) && !fs_sys_mkdir(buffer)) { *p = '/'; return false; }
            *p = '/';
        }
    }
    return true;
}

static bool zip_import_extract_entry(zip_import_ctx_t *ctx, size_t index, const char *staging,
                                     size_t strip_prefix) {
    const char *name = ctx->names[index] + strip_prefix;
    if (!*name || !zip_import_path_safe(name)) return false;
    char output[SYS_MAX_PATH];
    int n = snprintf(output, sizeof(output), "%s/%s", staging, name);
    if (n <= 0 || (size_t)n >= sizeof(output) || !zip_mkdir_parents(output)) return false;
    fs_file_t *input = pack_zip_open(ctx->pack, ctx->names[index]);
    if (!input) return false;
    FILE *file = fopen(output, "wb");
    if (!file) { pack_zip_close(ctx->pack, input); return false; }
    uint8_t buffer[8192];
    uint64_t remaining = ctx->entries[index]->uncomp_size;
    uint32_t crc = 0;
    bool ok = true;
    while (remaining) {
        size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        int64_t got = pack_zip_read(ctx->pack, input, buffer, chunk);
        if (got <= 0 || (size_t)got != chunk || fwrite(buffer, 1, chunk, file) != chunk) { ok = false; break; }
        crc = zip_crc32_update(crc, buffer, chunk);
        remaining -= chunk;
    }
    if (fclose(file) != 0) ok = false;
    pack_zip_close(ctx->pack, input);
    if (crc != ctx->entries[index]->crc) ok = false;
    if (!ok) unlink(output);
    return ok;
}

static bool zip_component_safe(const char *name) {
    if (!*name || !strcmp(name, ".") || !strcmp(name, "..")) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        if (!(isalnum(*p) || *p == '-' || *p == '_' || *p == '.')) return false;
    return strlen(name) < 100;
}

static bool zip_name_has_suffix(const char *name, const char *suffix) {
    size_t name_len = strlen(name), suffix_len = strlen(suffix);
    return name_len >= suffix_len && strcasecmp(name + name_len - suffix_len, suffix) == 0;
}

static bool zip_is_dynos_asset(const char *name) {
    /* Match CoopDX's ZIP type detection: these are the extensions it treats
     * as DynOS content. Other files may be dependencies inside the archive,
     * but should not independently turn arbitrary ZIPs into DynOS packs. */
    return zip_name_has_suffix(name, ".tex") || zip_name_has_suffix(name, ".png") ||
           zip_name_has_suffix(name, ".bin");
}

static void zip_remove_tree(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) { unlink(path); return; }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[SYS_MAX_PATH];
        int n = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (n <= 0 || (size_t)n >= sizeof(child)) continue;
        struct stat info;
        if (lstat(child, &info) == 0 && S_ISDIR(info.st_mode)) zip_remove_tree(child);
        else unlink(child);
    }
    closedir(dir);
    rmdir(path);
}

bool fs_zip_extract_package(const char *zip_path, const char *mods_dir, const char *dynos_packs_dir,
                            char *installed_path, size_t installed_path_size,
                            bool *contains_lua, bool *contains_dynos,
                            char *error, size_t error_size) {
    zip_pack_t *pack = NULL;
    zip_import_ctx_t ctx = {0};
    bool ok = false;
    char staging[SYS_MAX_PATH] = {0};
    char destination[SYS_MAX_PATH] = {0};
    if (error && error_size) error[0] = 0;
    if (contains_lua) *contains_lua = false;
    if (contains_dynos) *contains_dynos = false;
    FILE *probe = fopen(zip_path, "rb");
    if (!probe) { if (error && error_size) snprintf(error, error_size, "cannot open selected ZIP"); return false; }
    fseek(probe, 0, SEEK_END); long file_size = ftell(probe);
    uint64_t cdir = 0, data = 0, count = 0;
    bool archive_ok = file_size > 0 && file_size <= 128 * 1024 * 1024 &&
                      zip_parse_eocd(probe, &cdir, &data, &count) && count <= 1024;
    fclose(probe);
    if (!archive_ok) { if (error && error_size) snprintf(error, error_size, "invalid ZIP or archive limits exceeded"); return false; }
    pack = pack_zip_mount(zip_path);
    if (!pack) { if (error && error_size) snprintf(error, error_size, "ZIP parser rejected archive"); return false; }
    ctx.pack = pack; ctx.has_common_root = true;
    fs_dirtree_walk(&pack->tree, "", zip_import_add, &ctx, true);
    if (ctx.failed || ctx.count == 0) { if (error && error_size) snprintf(error, error_size, "%s", ctx.error[0] ? ctx.error : "ZIP contains no files"); goto done; }
    bool has_lua = false, has_dynos = false;
    for (size_t i = 0; i < ctx.count; ++i) {
        has_lua |= zip_name_has_suffix(ctx.names[i], ".lua") || zip_name_has_suffix(ctx.names[i], ".luac");
        has_dynos |= zip_is_dynos_asset(ctx.names[i]);
    }
    if (!has_lua && !has_dynos) {
        if (error && error_size) snprintf(error, error_size, "ZIP contains neither Lua scripts nor recognized DynOS assets");
        goto done;
    }
    if (contains_lua) *contains_lua = has_lua;
    if (contains_dynos) *contains_dynos = has_dynos;

    /* Some community archives contain one playable mod beside editor/sample
     * folders. Prefer the single unambiguous folder with main.lua and keep
     * unrelated sibling content out of the installed package. Multiple
     * candidates are rejected instead of silently choosing one. */
    char selected_mod_root[SYS_MAX_PATH] = {0};
    size_t selected_mod_root_len = 0;
    bool has_selected_mod_root = false;
    bool multiple_mod_roots = false;
    bool has_archive_root_main = false;
    static const char main_suffix[] = "/main.lua";
    for (size_t i = 0; i < ctx.count; ++i) {
        size_t name_len = strlen(ctx.names[i]);
        size_t suffix_len = sizeof(main_suffix) - 1;
        if (!strcmp(ctx.names[i], "main.lua")) {
            has_archive_root_main = true;
            continue;
        }
        if (name_len <= suffix_len || strcmp(ctx.names[i] + name_len - suffix_len, main_suffix)) continue;
        size_t root_len = name_len - suffix_len;
        if (root_len >= sizeof(selected_mod_root)) {
            if (error && error_size) snprintf(error, error_size, "mod root path exceeds the supported length");
            goto done;
        }
        if (!has_selected_mod_root) {
            memcpy(selected_mod_root, ctx.names[i], root_len);
            selected_mod_root[root_len] = '\0';
            selected_mod_root_len = root_len;
            has_selected_mod_root = true;
        } else if (root_len != selected_mod_root_len ||
                   memcmp(selected_mod_root, ctx.names[i], root_len)) {
            multiple_mod_roots = true;
            break;
        }
    }
    if (has_archive_root_main && has_selected_mod_root) multiple_mod_roots = true;
    if (multiple_mod_roots) {
        if (error && error_size) snprintf(error, error_size,
                                          "ZIP contains multiple Lua mod roots; import each mod separately");
        goto done;
    }
    bool ignored_siblings = false;
    if (has_selected_mod_root) {
        for (size_t i = 0; i < ctx.count; ++i) {
            if (strncmp(ctx.names[i], selected_mod_root, selected_mod_root_len) ||
                ctx.names[i][selected_mod_root_len] != '/') {
                ignored_siblings = true;
                break;
            }
        }
    }

    char package[128] = {0};
    size_t strip_prefix = 0;
    if (has_selected_mod_root) {
        strip_prefix = selected_mod_root_len + 1;
        const char *base = strrchr(selected_mod_root, '/');
        base = base ? base + 1 : selected_mod_root;
        snprintf(package, sizeof(package), "%s", base);
        for (char *p = package; *p; ++p)
            if (!(isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == '.')) *p = '_';
        if (!zip_component_safe(package)) snprintf(package, sizeof(package), "ImportedMod");
    } else if (ctx.has_common_root && ctx.common_root_len && zip_component_safe(ctx.common_root)) {
        snprintf(package, sizeof(package), "%s", ctx.common_root);
        strip_prefix = ctx.common_root_len + 1;
    } else {
        const char *base = strrchr(zip_path, '/'); base = base ? base + 1 : zip_path;
        snprintf(package, sizeof(package), "%s", base);
        char *dot = strrchr(package, '.'); if (dot) *dot = 0;
        for (char *p = package; *p; ++p) if (!(isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == '.')) *p = '_';
        if (!zip_component_safe(package)) snprintf(package, sizeof(package), "ImportedMod");
    }
    const char *install_root = has_lua ? mods_dir : dynos_packs_dir;
    if (!install_root || !*install_root ||
        !zip_mkdir_parents(install_root) ||
        (!fs_sys_dir_exists(install_root) && !fs_sys_mkdir(install_root))) {
        if (error && error_size) snprintf(error, error_size, "cannot create sandbox package folder");
        goto done;
    }
    char unique[128]; snprintf(unique, sizeof(unique), "%s", package);
    for (unsigned suffix = 2; ; ++suffix) {
        snprintf(destination, sizeof(destination), "%s/%s", install_root, unique);
        if (!fs_sys_dir_exists(destination) && !fs_sys_file_exists(destination)) break;
        snprintf(unique, sizeof(unique), "%.90s-%u", package, suffix);
        if (suffix > 999) { if (error && error_size) snprintf(error, error_size, "too many imported mods share this name"); goto done; }
    }
    bool stage_created = false;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        snprintf(staging, sizeof(staging), "%s/.import-%u-%u-%u", install_root,
                 (unsigned)getpid(), (unsigned)count, attempt);
        if (fs_sys_mkdir(staging)) { stage_created = true; break; }
    }
    if (!stage_created) { if (error && error_size) snprintf(error, error_size, "cannot create temporary extraction folder"); goto done; }
    size_t stripped = strip_prefix;
    if (strip_prefix && !has_selected_mod_root) {
        for (size_t i = 0; i < ctx.count; ++i) {
            if (strncmp(ctx.names[i], ctx.common_root, ctx.common_root_len) || ctx.names[i][ctx.common_root_len] != '/') {
                stripped = 0; break;
            }
        }
    }
    for (size_t i = 0; i < ctx.count; ++i) {
        if (has_selected_mod_root &&
            (strncmp(ctx.names[i], selected_mod_root, selected_mod_root_len) ||
             ctx.names[i][selected_mod_root_len] != '/')) continue;
        if (!zip_import_extract_entry(&ctx, i, staging, stripped)) {
            if (error && error_size) snprintf(error, error_size, "failed extracting or validating resource: %.160s", ctx.names[i]);
            goto done;
        }
    }
    if (rename(staging, destination) != 0) { if (error && error_size) snprintf(error, error_size, "could not install extracted mod package"); goto done; }
    staging[0] = 0;
    if (ignored_siblings && error && error_size)
        snprintf(error, error_size, "installed the single mod in '%s'; ignored unrelated archive entries", selected_mod_root);
    if (installed_path && installed_path_size) snprintf(installed_path, installed_path_size, "%s", destination);
    ok = true;
done:
    if (!ok && staging[0]) {
        zip_remove_tree(staging);
    }
    for (size_t i = 0; i < ctx.count; ++i) free(ctx.names[i]);
    free(ctx.names); free(ctx.entries);
    pack_zip_unmount(pack);
    return ok;
}
