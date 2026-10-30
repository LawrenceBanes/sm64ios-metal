#include "gfx_metal_shader.h"

#include <stdio.h>
#include <string.h>

struct ShaderWriter {
    char *out;
    size_t size;
    size_t used;
    bool ok;
};

static void write_text(struct ShaderWriter *w, const char *text) {
    size_t n = strlen(text);
    if (!w->ok || n >= w->size - w->used) {
        w->ok = false;
        return;
    }
    memcpy(w->out + w->used, text, n);
    w->used += n;
    w->out[w->used] = '\0';
}

static void write_format(struct ShaderWriter *w, const char *fmt, int a, int b) {
    int n;
    if (!w->ok || w->used >= w->size) {
        w->ok = false;
        return;
    }
    n = snprintf(w->out + w->used, w->size - w->used, fmt, a, b);
    if (n < 0 || (size_t)n >= w->size - w->used) {
        w->ok = false;
        return;
    }
    w->used += (size_t)n;
}

static const char *shader_item(uint8_t item, bool alpha, bool only_alpha) {
    if (only_alpha) {
        switch (item) {
            case SHADER_INPUT_1: return "in.input1.a";
            case SHADER_INPUT_2: return "in.input2.a";
            case SHADER_INPUT_3: return "in.input3.a";
            case SHADER_INPUT_4: return "in.input4.a";
            case SHADER_TEXEL0: case SHADER_TEXEL0A: return "texel0.a";
            case SHADER_TEXEL1: return "texel1.a";
            default: return "0.0";
        }
    }
    switch (item) {
        case SHADER_INPUT_1: return alpha ? "in.input1" : "in.input1.rgb";
        case SHADER_INPUT_2: return alpha ? "in.input2" : "in.input2.rgb";
        case SHADER_INPUT_3: return alpha ? "in.input3" : "in.input3.rgb";
        case SHADER_INPUT_4: return alpha ? "in.input4" : "in.input4.rgb";
        case SHADER_TEXEL0: return alpha ? "texel0" : "texel0.rgb";
        case SHADER_TEXEL0A: return alpha ? "float4(texel0.a)" : "float3(texel0.a)";
        case SHADER_TEXEL1: return alpha ? "texel1" : "texel1.rgb";
        default: return alpha ? "float4(0.0)" : "float3(0.0)";
    }
}

static void write_formula(struct ShaderWriter *w, const struct CCFeatures *f,
                          int channel, bool only_alpha, bool alpha) {
    const uint8_t *c = f->c[only_alpha ? 1 : 0];
    if (f->do_single[channel]) {
        write_text(w, shader_item(c[3], alpha, only_alpha));
    } else if (f->do_multiply[channel]) {
        write_text(w, shader_item(c[0], alpha, only_alpha));
        write_text(w, " * ");
        write_text(w, shader_item(c[2], alpha, only_alpha));
    } else if (f->do_mix[channel]) {
        write_text(w, "mix(");
        write_text(w, shader_item(c[1], alpha, only_alpha));
        write_text(w, ", ");
        write_text(w, shader_item(c[0], alpha, only_alpha));
        write_text(w, ", ");
        write_text(w, shader_item(c[2], alpha, only_alpha));
        write_text(w, ")");
    } else {
        write_text(w, "(");
        write_text(w, shader_item(c[0], alpha, only_alpha));
        write_text(w, " - ");
        write_text(w, shader_item(c[1], alpha, only_alpha));
        write_text(w, ") * ");
        write_text(w, shader_item(c[2], alpha, only_alpha));
        write_text(w, " + ");
        write_text(w, shader_item(c[3], alpha, only_alpha));
    }
}

bool gfx_metal_build_shader(uint32_t shader_id, char *out, size_t out_size,
                            struct CCFeatures *features) {
    struct CCFeatures local;
    struct ShaderWriter w = { out, out_size, 0, out != NULL && out_size > 1 };
    int i;
    if (!features) features = &local;
    if (!w.ok) return false;
    gfx_cc_get_features(shader_id, features);
    out[0] = '\0';

    write_text(&w, "#include <metal_stdlib>\nusing namespace metal;\n");
    if (features->used_textures[0] || features->used_textures[1]) {
        write_text(&w, "struct SamplerParams { uint filter0; uint filter1; };\n"
                       "float4 sampleTex(texture2d<float> tex, sampler samp, float2 uv, bool dofilter) {\n"
                       "if (!dofilter) return tex.sample(samp, uv);\n"
                       "float2 size=float2(tex.get_width(),tex.get_height());\n"
                       "float2 offset=fract(uv*size-float2(0.5));\n"
                       "offset-=step(1.0,offset.x+offset.y);\n"
                       "float4 c0=tex.sample(samp,uv-offset/size);\n"
                       "float4 c1=tex.sample(samp,uv-float2(offset.x-sign(offset.x),offset.y)/size);\n"
                       "float4 c2=tex.sample(samp,uv-float2(offset.x,offset.y-sign(offset.y))/size);\n"
                       "return c0+abs(offset.x)*(c1-c0)+abs(offset.y)*(c2-c0); }\n");
    }
    write_text(&w,
                   "struct VIn { float4 pos [[attribute(0)]];\n");
    if (features->used_textures[0] || features->used_textures[1])
        write_text(&w, "float2 uv [[attribute(1)]];\n");
    if (features->opt_fog)
        write_text(&w, "float4 fog [[attribute(2)]];\n");
    for (i = 0; i < features->num_inputs; ++i) {
        int attr = 2 + (features->used_textures[0] || features->used_textures[1]) + features->opt_fog + i;
        write_format(&w, features->opt_alpha ? "float4 input%d [[attribute(%d)]];\n" :
                     "float3 input%d [[attribute(%d)]];\n", i + 1, attr);
    }
    write_text(&w, "};\nstruct VOut { float4 pos [[position]];\n");
    if (features->used_textures[0] || features->used_textures[1]) write_text(&w, "float2 uv;\n");
    if (features->opt_fog) write_text(&w, "float4 fog;\n");
    for (i = 0; i < features->num_inputs; ++i)
        write_format(&w, features->opt_alpha ? "float4 input%d;\n" : "float3 input%d;\n", i + 1, 0);
    write_text(&w, "};\nvertex VOut sm64_vertex(VIn in [[stage_in]]) { VOut o; o.pos=in.pos;\n");
    if (features->used_textures[0] || features->used_textures[1]) write_text(&w, "o.uv=in.uv;\n");
    if (features->opt_fog) write_text(&w, "o.fog=in.fog;\n");
    for (i = 0; i < features->num_inputs; ++i)
        write_format(&w, "o.input%d=in.input%d;\n", i + 1, i + 1);
    write_text(&w, "return o; }\nfragment float4 sm64_fragment(VOut in [[stage_in]]");
    if (features->used_textures[0]) write_text(&w, ", texture2d<float> tex0 [[texture(0)]], sampler samp0 [[sampler(0)]]");
    if (features->used_textures[1]) write_text(&w, ", texture2d<float> tex1 [[texture(1)]], sampler samp1 [[sampler(1)]]");
    if (features->used_textures[0] || features->used_textures[1])
        write_text(&w, ", constant SamplerParams &filterParams [[buffer(1)]]");
    write_text(&w, ") {\n");
    if (features->used_textures[0]) write_text(&w, "float4 texel0=sampleTex(tex0,samp0,in.uv,filterParams.filter0!=0);\n");
    if (features->used_textures[1]) write_text(&w, "float4 texel1=sampleTex(tex1,samp1,in.uv,filterParams.filter1!=0);\n");
    if (features->opt_alpha && !features->color_alpha_same) {
        write_text(&w, "float4 color=float4(");
        write_formula(&w, features, 0, false, false);
        write_text(&w, ", ");
        write_formula(&w, features, 1, true, true);
        write_text(&w, ");\n");
    } else if (features->opt_alpha) {
        write_text(&w, "float4 color=");
        write_formula(&w, features, 0, false, true);
        write_text(&w, ";\n");
    } else {
        write_text(&w, "float4 color=float4(");
        write_formula(&w, features, 0, false, false);
        write_text(&w, ",1.0);\n");
    }
    if (features->opt_texture_edge && features->opt_alpha)
        write_text(&w, "if(color.a<=0.3) discard_fragment(); color.a=1.0;\n");
    if (features->opt_fog) {
        if (features->opt_alpha) write_text(&w, "color=float4(mix(color.rgb,in.fog.rgb,in.fog.a),color.a);\n");
        else write_text(&w, "color.rgb=mix(color.rgb,in.fog.rgb,in.fog.a);\n");
    }
    write_text(&w, "return color; }\n");
    return w.ok;
}
