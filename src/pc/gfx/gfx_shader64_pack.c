#include "gfx_shader64_pack.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#define SHADER64_MKDIR(path, mode) _mkdir(path)
#define SHADER64_RMDIR(path) _rmdir(path)
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <unistd.h>
#define SHADER64_MKDIR(path, mode) mkdir(path, mode)
#define SHADER64_RMDIR(path) rmdir(path)
#endif
#include <tinfl.h>

#define ZIP_EOCD 0x06054b50u
#define ZIP_CENTRAL 0x02014b50u
#define ZIP_LOCAL 0x04034b50u
#define MAX_ENTRIES 64u
#define MAX_MANIFEST (64u * 1024u)

struct entry {
    const unsigned char *name;
    uint16_t name_len, flags, method;
    uint32_t crc, compressed, uncompressed, offset, external;
};

static uint16_t u16(const unsigned char *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t u32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void err(char *out, size_t n, const char *s) { if (out && n) { snprintf(out, n, "%s", s); } }
static int contains(const char *s, size_t n, const char *needle) {
    size_t length = strlen(needle);
    if (length > n) return 0;
    for (size_t i = 0; i <= n - length; ++i) if (memcmp(s + i, needle, length) == 0) return 1;
    return 0;
}
static uint32_t crc32_bytes(const unsigned char *data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static int safe_name(const unsigned char *s, size_t n, int *is_dir) {
    size_t i, component = 0;
    if (!n || s[0] == '/' || s[0] == '\\' || memchr(s, '\\', n) || memchr(s, ':', n)) return 0;
    *is_dir = s[n - 1] == '/';
    if (*is_dir) --n;
    if (!n) return 0;
    for (i = 0; i <= n; ++i) {
        if (i == n || s[i] == '/') {
            size_t len = i - component;
            if (!len || (len == 1 && s[component] == '.') ||
                (len == 2 && s[component] == '.' && s[component + 1] == '.')) return 0;
            component = i + 1;
        }
    }
    return 1;
}

static int allowed_name(const unsigned char *s, size_t n, int dir) {
    static const char manifest[] = "manifest.json";
    if (dir) return n == 8 && memcmp(s, "shaders/", 8) == 0;
    return (n == sizeof(manifest) - 1 && memcmp(s, manifest, n) == 0) ||
           (n > 14 && memcmp(s, "shaders/", 8) == 0 &&
            !memchr(s + 8, '/', n - 8) && memcmp(s + n - 6, ".metal", 6) == 0);
}

static int extract_one(const unsigned char *zip, size_t zip_size,
                       const struct entry *e, const char *destination,
                       char *error, size_t error_size) {
    const unsigned char *local;
    uint16_t name_len, extra_len;
    size_t data_pos;
    unsigned char *out = NULL;
    char path[1024];
    FILE *f = NULL;
    int ok = 0;
    if (e->offset > zip_size || zip_size - e->offset < 30) goto malformed;
    local = zip + e->offset;
    if (u32(local) != ZIP_LOCAL) goto malformed;
    name_len = u16(local + 26); extra_len = u16(local + 28);
    data_pos = (size_t)e->offset + 30u + name_len + extra_len;
    if (data_pos > zip_size || e->compressed > zip_size - data_pos) goto malformed;
    if (name_len != e->name_len || memcmp(local + 30, e->name, name_len) != 0) goto malformed;
    if (e->uncompressed > GFX_SHADER64_MAX_SOURCE_BYTES &&
        !(name_len == 13 && memcmp(e->name, "manifest.json", 13) == 0)) {
        err(error, error_size, "shader source exceeds 256 KiB"); return 0;
    }
    if (e->uncompressed > MAX_MANIFEST && name_len == 13 && memcmp(e->name, "manifest.json", 13) == 0) {
        err(error, error_size, "manifest exceeds 64 KiB"); return 0;
    }
    out = malloc((size_t)e->uncompressed + 1u);
    if (!out) { err(error, error_size, "out of memory extracting pack file"); return 0; }
    if (e->method == 0) {
        if (e->compressed != e->uncompressed) goto malformed;
        memcpy(out, zip + data_pos, e->uncompressed);
    } else if (e->method == 8) {
        mz_stream stream; memset(&stream, 0, sizeof(stream));
        if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
            err(error, error_size, "deflate initialization failed"); goto done;
        }
        stream.next_in = zip + data_pos; stream.avail_in = e->compressed;
        stream.next_out = out; stream.avail_out = e->uncompressed;
        int status = mz_inflate(&stream, MZ_FINISH);
        int end_status = mz_inflateEnd(&stream);
        if (status != MZ_STREAM_END || end_status != MZ_OK ||
            stream.total_out != e->uncompressed || stream.total_in != e->compressed) goto malformed;
    } else { err(error, error_size, "ZIP compression method unsupported (only stored/deflate)"); goto done; }
    if (crc32_bytes(out, e->uncompressed) != e->crc) { err(error, error_size, "ZIP CRC mismatch"); goto done; }
    out[e->uncompressed] = 0;
    if (snprintf(path, sizeof(path), "%s/%.*s", destination, (int)e->name_len, e->name) >= (int)sizeof(path)) {
        err(error, error_size, "shader pack path too long"); goto done;
    }
    if (e->name_len == 13 && memcmp(e->name, "manifest.json", 13) == 0 && memchr(out, 0, e->uncompressed)) {
        err(error, error_size, "manifest contains a NUL byte"); goto done;
    }
    f = fopen(path, "wb");
    if (!f) { err(error, error_size, "could not write installed shader file"); goto done; }
    size_t written = fwrite(out, 1, e->uncompressed, f);
    int close_result = fclose(f); f = NULL;
    if (written != e->uncompressed || close_result != 0) {
        remove(path); err(error, error_size, "could not write installed shader file"); goto done;
    }
    ok = 1;
done:
    if (f) fclose(f);
    free(out); return ok;
malformed:
    err(error, error_size, "malformed ZIP entry or inconsistent lengths");
    if (f) fclose(f);
    free(out); return 0;
}

int gfx_shader64_install_zip(const void *data, size_t size, const char *destination,
                             char *error, size_t error_size) {
    const unsigned char *zip = data;
    size_t start, eocd = SIZE_MAX, pos, cdir_end;
    struct entry entries[MAX_ENTRIES];
    size_t count = 0, accepted = 0, total = 0;
    int have_manifest = 0, have_source = 0;
    struct stat st;
    if (!data || !destination || size < 22 || size > GFX_SHADER64_MAX_ARCHIVE_BYTES) {
        err(error, error_size, "archive is missing or exceeds 8 MiB"); return 0;
    }
    if (stat(destination, &st) != 0 || !S_ISDIR(st.st_mode)) { err(error, error_size, "destination must be a new directory"); return 0; }
    start = size > 65557u ? size - 65557u : 0;
    for (pos = size - 22u + 1u; pos-- > start;) {
        if (pos + 22u <= size && u32(zip + pos) == ZIP_EOCD) {
            uint16_t comment = u16(zip + pos + 20);
            if (pos + 22u + comment == size) { eocd = pos; break; }
        }
    }
    if (eocd == SIZE_MAX || u16(zip + eocd + 4) || u16(zip + eocd + 6) ||
        u16(zip + eocd + 8) != u16(zip + eocd + 10)) { err(error, error_size, "unsupported ZIP layout or missing central directory"); return 0; }
    count = u16(zip + eocd + 10);
    uint32_t cdir_size = u32(zip + eocd + 12), cdir_offset = u32(zip + eocd + 16);
    if (!count || count > MAX_ENTRIES || cdir_offset > size || cdir_size > size - cdir_offset || cdir_offset + cdir_size > eocd) {
        err(error, error_size, "ZIP entry count or central directory bounds are invalid"); return 0;
    }
    pos = cdir_offset; cdir_end = pos + cdir_size;
    for (size_t i = 0; i < count; ++i) {
        if (pos > cdir_end || cdir_end - pos < 46 || u32(zip + pos) != ZIP_CENTRAL) { err(error, error_size, "invalid ZIP central directory record"); return 0; }
        uint16_t name_len = u16(zip + pos + 28), extra_len = u16(zip + pos + 30), comment_len = u16(zip + pos + 32);
        size_t rec_len = 46u + name_len + extra_len + comment_len;
        if (rec_len > cdir_end - pos || !name_len) { err(error, error_size, "truncated ZIP central directory record"); return 0; }
        const unsigned char *name = zip + pos + 46;
        int dir = 0;
        if (!safe_name(name, name_len, &dir)) { err(error, error_size, "unsafe path in shader archive"); return 0; }
        int accepted_name = allowed_name(name, name_len, dir);
        uint16_t flags = u16(zip + pos + 8), method = u16(zip + pos + 10);
        uint32_t comp = u32(zip + pos + 20), uncomp = u32(zip + pos + 24), external = u32(zip + pos + 38);
        unsigned int unix_mode = external >> 16;
        if ((unix_mode & 0170000u) == 0120000u) { err(error, error_size, "symbolic links are not allowed in shader packs"); return 0; }
        if (flags & 1u) { err(error, error_size, "encrypted ZIP entries are unsupported"); return 0; }
        if (uncomp == 0xffffffffu || comp == 0xffffffffu || u32(zip + pos + 42) == 0xffffffffu) { err(error, error_size, "ZIP64 entries are unsupported"); return 0; }
        if (accepted_name && !dir) {
            if (accepted >= GFX_SHADER64_MAX_PACK_FILES) { err(error, error_size, "too many shader pack files"); return 0; }
            if (method != 0 && method != 8) { err(error, error_size, "ZIP compression method unsupported"); return 0; }
            for (size_t previous = 0; previous < accepted; ++previous)
                if (entries[previous].name_len == name_len && memcmp(entries[previous].name, name, name_len) == 0) {
                    err(error, error_size, "duplicate file path in shader archive"); return 0;
                }
            if (uncomp > GFX_SHADER64_MAX_SOURCE_BYTES && !(name_len == 13 && memcmp(name, "manifest.json", 13) == 0)) { err(error, error_size, "shader source exceeds 256 KiB"); return 0; }
            if (total > GFX_SHADER64_MAX_ARCHIVE_BYTES - uncomp) { err(error, error_size, "expanded shader pack exceeds 8 MiB"); return 0; }
            total += uncomp;
            entries[accepted++] = (struct entry){ name, name_len, flags, method, u32(zip + pos + 16), comp, uncomp, u32(zip + pos + 42), external };
            if (name_len == 13 && memcmp(name, "manifest.json", 13) == 0) { if (have_manifest++) { err(error, error_size, "duplicate manifest"); return 0; } }
            else have_source = 1;
        } else if (!accepted_name) { err(error, error_size, "archive contains unsupported paths; only manifest.json and shaders/*.metal are accepted"); return 0; }
        pos += rec_len;
    }
    if (pos != cdir_end || !have_manifest || !have_source) { err(error, error_size, "pack needs one manifest.json and at least one shaders/*.metal file"); return 0; }
    char shader_dir[1024];
    if (snprintf(shader_dir, sizeof(shader_dir), "%s/shaders", destination) >= (int)sizeof(shader_dir) ||
        SHADER64_MKDIR(shader_dir, 0700) != 0) { err(error, error_size, "could not create shader source directory"); return 0; }
    for (size_t i = 0; i < accepted; ++i) if (!extract_one(zip, size, &entries[i], destination, error, error_size)) {
        char path[1024];
        for (size_t j = 0; j <= i; ++j) {
            if (snprintf(path, sizeof(path), "%s/%.*s", destination, (int)entries[j].name_len, entries[j].name) < (int)sizeof(path)) remove(path);
        }
        SHADER64_RMDIR(shader_dir);
        return 0;
    }
    if (error && error_size) error[0] = '\0';
    return 1;
}

int gfx_shader64_validate_post_source(const char *source, size_t size,
                                      char *error, size_t error_size) {
    static const char *const required[] = {
        "float4 shader64_post(", "float4 color", "float2 uv",
        "texture2d<float> image", "sampler imageSampler", "constant float4 &params",
        "#include <metal_stdlib>", "using namespace metal;"
    };
    if (!source || !size || size > GFX_SHADER64_MAX_SOURCE_BYTES) { err(error, error_size, "post source missing or exceeds 256 KiB"); return 0; }
    if (memchr(source, 0, size)) { err(error, error_size, "post source contains an embedded NUL byte"); return 0; }
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i)
        if (!contains(source, size, required[i])) { err(error, error_size, "post source does not implement Shader64 API v1 signature"); return 0; }
    if (contains(source, size, "#include \"")) { err(error, error_size, "local shader includes are not supported in API v1"); return 0; }
    if (error && error_size) error[0] = '\0';
    return 1;
}

int gfx_shader64_build_wrapper(const char *source, size_t source_size,
                               char *output, size_t output_size) {
    static const char prefix[] =
        "#include <metal_stdlib>\nusing namespace metal;\n"
        "struct Shader64In { float4 position [[position]]; float2 uv; };\n"
        "vertex Shader64In sm64_shader64_vertex(uint id [[vertex_id]]) { Shader64In o; o.position = id == 0 ? float4(-1.0,-1.0,0.0,1.0) : (id == 1 ? float4(3.0,-1.0,0.0,1.0) : float4(-1.0,3.0,0.0,1.0)); o.uv = id == 0 ? float2(0.0,1.0) : (id == 1 ? float2(2.0,1.0) : float2(0.0,-1.0)); return o; }\n";
    static const char suffix[] =
        "\nfragment float4 sm64_shader64_fragment(Shader64In in [[stage_in]], texture2d<float> image [[texture(0)]], sampler imageSampler [[sampler(0)]], constant float4 &parameters [[buffer(0)]]) { float4 color=image.sample(imageSampler,in.uv); return shader64_post(color,in.uv,image,imageSampler,parameters); }\n";
    size_t needed = sizeof(prefix) - 1u + source_size + sizeof(suffix);
    if (!output || output_size < needed ||
        !gfx_shader64_validate_post_source(source, source_size, NULL, 0)) return 0;
    memcpy(output, prefix, sizeof(prefix) - 1u);
    memcpy(output + sizeof(prefix) - 1u, source, source_size);
    memcpy(output + sizeof(prefix) - 1u + source_size, suffix, sizeof(suffix));
    return 1;
}
