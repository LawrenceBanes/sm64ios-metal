#include "pc/mods/dynos_actor.h"
#include "pc/mods/dynos_actor_native.h"
#include <PR/gbi.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long length = ftell(f);
    assert(length > 0 && length < 8 * 1024 * 1024);
    assert(!fseek(f, 0, SEEK_SET));
    unsigned char *data = malloc((size_t)length);
    assert(data && fread(data, 1, (size_t)length, f) == (size_t)length);
    fclose(f);
    *size = (size_t)length;
    return data;
}

static const void *texture_resolver(void *context, const char *name,
                                    const uint8_t *png, size_t size) {
    (void)context;
    assert(name && *name && png && size >= 8);
    assert(!memcmp(png, "\x89PNG\r\n\x1a\n", 8));
    return "m69tex:test";
}

static void dummy_geo_function(void) {}

static const void *function_resolver(void *context, uint32_t index) {
    (void)context;
    /* This linker test checks pointer wiring, not callback semantics. */
    assert(index < 1024);
    return (const void *)dummy_geo_function;
}

int main(int argc, char **argv) {
    assert(argc >= 2);
    int reject = !strcmp(argv[1], "--reject");
    for (int arg = reject ? 2 : 1; arg < argc; ++arg) {
        size_t size;
        unsigned char *data = read_file(argv[arg], &size);
        DynosActorBundle bundle;
        char error[400];
        if (reject) {
            assert(!dynos_actor_decode(data, size, &bundle, error, sizeof(error)));
            assert(error[0]);
            printf("rejected %s: %s\n", argv[arg], error);
            free(data);
            continue;
        }
        assert(dynos_actor_decode(data, size, &bundle, error, sizeof(error)));
        unsigned textures = 0, vertices = 0, gfx = 0, geos = 0;
        for (size_t i = 0; i < bundle.record_count; ++i) {
            const DynosActorRecord *record = &bundle.records[i];
            textures += record->type == 2;
            vertices += record->type == 3;
            gfx += record->type == 4;
            geos += record->type == 5;
            if (record->type == 5) assert(dynos_actor_find(&bundle, 5, record->name) == record);
        }
        assert(textures && vertices && gfx && geos);
        for (size_t i = 0; i < bundle.reference_count; ++i) {
            const DynosActorReference *ref = &bundle.references[i];
            if (ref->kind == 1) assert(ref->target < bundle.record_count);
        }
        DynosNativeActor native;
        assert(dynos_actor_native_build(&bundle, texture_resolver, function_resolver,
                                        NULL, &native, error, sizeof(error)));
        assert(native.count == bundle.record_count);
        unsigned triangles = 0;
        for (size_t i = 0; i < bundle.record_count; ++i) {
            const DynosActorRecord *record = &bundle.records[i];
            if (record->type == 4) {
                const Gfx *commands = native.resources[i];
                for (uint32_t j = 0; j < record->count; ++j) {
                    uint8_t opcode = (uint8_t)(commands[j].words.w0 >> 24);
                    triangles += opcode == G_TRI1 || opcode == G_TRI2;
                }
            }
            if (record->type == 5)
                assert(dynos_actor_native_find(&bundle, &native, 5, record->name) == native.resources[i]);
        }
        assert(triangles);
        const DynosActorRecord *root = NULL;
        for (size_t i = 0; i < bundle.record_count; ++i)
            if (bundle.records[i].type == 5) root = &bundle.records[i];
        assert(root);
        if (!strcmp(root->name, "test_geo")) {
            const Gfx *display_list = dynos_actor_native_find(&bundle, &native, 4, "test_gfx");
            const Vtx *vertices = dynos_actor_native_find(&bundle, &native, 3, "test_vertices");
            const uintptr_t *geo = dynos_actor_native_find(&bundle, &native, 5, "test_geo");
            bool uses_offsets = strstr(argv[arg], "offsets.bin") != NULL;
            assert(display_list && vertices && geo);
            assert(display_list[0].words.w1 == (uintptr_t)(vertices + (uses_offsets ? 1 : 0)));
            assert(geo[3] == (uintptr_t)(display_list + (uses_offsets ? 1 : 0)));
        }
        bool root_valid = dynos_actor_native_validate_root_geo(&bundle, &native,
                                                                root->name, error, sizeof(error));
        if (strstr(root->name, "sparkle") || !strcmp(root->name, "test_geo") ||
            !strcmp(root->name, "omm_peachy_geo")) {
            if (!root_valid) fprintf(stderr, "expected supported GeoLayout: %s\n", error);
            assert(root_valid);
        }
        printf("  root GeoLayout %s: %s\n", root->name,
               root_valid ? "bounded native subset accepted" : error);
        printf("  native arrays linked; %u triangle commands\n", triangles);
        dynos_actor_native_free(&native);
        printf("%s: %zu records, %zu references, %u textures, %u vertices, %u Gfx, %u GeoLayout\n",
               argv[arg], bundle.record_count, bundle.reference_count,
               textures, vertices, gfx, geos);
        dynos_actor_free(&bundle);

        assert(!dynos_actor_decode(data, size - 1, &bundle, error, sizeof(error)));
        assert(error[0]);
        data[0] = 0;
        assert(!dynos_actor_decode(data, size, &bundle, error, sizeof(error)));
        assert(error[0]);
        free(data);
    }
    return 0;
}
