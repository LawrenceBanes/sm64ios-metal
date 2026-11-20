#include "gfx_metal_shader.h"
#include "gfx_metal_vertex_layout.h"

#include <stdio.h>
#include <string.h>

const char *gfx_metal_present_shader_source(void) {
    return
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct PresentOut { float4 position [[position]]; float2 uv; };\n"
        "vertex PresentOut sm64_present_vertex(uint id [[vertex_id]]) {\n"
        "  PresentOut o;\n"
        "  o.position = id == 0 ? float4(-1.0, -1.0, 0.0, 1.0) : (id == 1 ? float4(3.0, -1.0, 0.0, 1.0) : float4(-1.0, 3.0, 0.0, 1.0));\n"
        "  o.uv = id == 0 ? float2(0.0, 1.0) : (id == 1 ? float2(2.0, 1.0) : float2(0.0, -1.0));\n"
        "  return o;\n"
        "}\n"
        "inline float3 sm64_view_position(float2 uv, float d, constant float4x4 &invP) { float4 clip=float4(uv.x*2.0-1.0,1.0-uv.y*2.0,d*2.0-1.0,1.0); float4 p=invP*clip; return p.xyz/(abs(p.w)>1.0e-6?p.w:1.0e-6); }\n"
        "fragment float4 sm64_present_fragment(PresentOut in [[stage_in]], texture2d<float> image [[texture(0)]], sampler imageSampler [[sampler(0)]], texture2d<float> ao [[texture(1)]], sampler aoSampler [[sampler(1)]], depth2d<float> depth [[texture(2)]], texture2d<float> bloom [[texture(3)]], depth2d<float> shadowMap [[texture(4)]], texture2d<float> godRays [[texture(6)]], constant uint4 &presentFlags [[buffer(0)]], constant float4 &gradeParams [[buffer(1)]], constant float4x4 &inverseProjection [[buffer(2)]], constant float4 &fogColor [[buffer(3)]], constant float4 &bloomParams [[buffer(4)]], constant float4 &gradeStyle [[buffer(5)]], constant float4 &skyParams [[buffer(6)]], constant float4x4 &inverseView [[buffer(7)]], constant float4 &shadowRightMin [[buffer(8)]], constant float4 &shadowUpMin [[buffer(9)]], constant float4 &shadowForwardMin [[buffer(10)]], constant float4 &shadowRanges [[buffer(11)]], constant float &rayParams [[buffer(12)]], constant float4 &sunParams [[buffer(13)]]) {\n"
        "  float4 color=image.sample(imageSampler,in.uv); if(presentFlags.y==1u) { float a=ao.sample(aoSampler,in.uv).r; return float4(a,a,a,1.0); } if(presentFlags.y==2u) { float d=depth.sample(aoSampler,in.uv); return float4(d,d,d,1.0); } float sceneDepth=depth.sample(aoSampler,in.uv); float shadowVisibility=1.0; if(skyParams.x>0.5&&sceneDepth>=0.999999) { float3 horizon=skyParams.x<1.5?float3(0.52,0.76,0.96):float3(0.98,0.42,0.18); float3 zenith=skyParams.x<1.5?float3(0.08,0.28,0.72):float3(0.13,0.19,0.39); float h=pow(clamp(1.0-in.uv.y,0.0,1.0),0.65); color.rgb=mix(horizon,zenith,h); if(skyParams.y>0.5){float sd=distance(in.uv,skyParams.zw); float disk=1.0-smoothstep(0.014,0.021,sd); color.rgb=mix(color.rgb,float3(1.0,0.91,0.73),disk);} } if(presentFlags.x!=0u) color.rgb*=ao.sample(aoSampler,in.uv).r;\n"
        "  if(shadowRanges.w>0.0&&sceneDepth<0.999999){ constexpr sampler shadowSampler(coord::normalized,address::clamp_to_edge,filter::nearest); float4 clip=float4(in.uv.x*2.0-1.0,1.0-in.uv.y*2.0,sceneDepth*2.0-1.0,1.0); float4 view=inverseProjection*clip; view/=max(abs(view.w),1.0e-6); float4 world=inverseView*view; world/=max(abs(world.w),1.0e-6); float sx=(dot(shadowRightMin.xyz,world.xyz)-shadowRightMin.w)/shadowRanges.x; float sy=(dot(shadowUpMin.xyz,world.xyz)-shadowUpMin.w)/shadowRanges.y; float sz=(dot(shadowForwardMin.xyz,world.xyz)-shadowForwardMin.w)/shadowRanges.z; float2 suv=float2(sx,1.0-sy); if(all(suv>=0.0)&&all(suv<=1.0)&&sz>=0.0&&sz<=1.0){ float visible=0.0; float2 texel=1.0/float2(shadowMap.get_width(),shadowMap.get_height()); for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++){ float stored=shadowMap.sample(shadowSampler,suv+float2(x,y)*texel); visible+=sz-shadowRanges.w<=stored?1.0:0.0; } shadowVisibility=visible/9.0; color.rgb*=mix(0.62,1.0,shadowVisibility); } }\n"
        "  if(sunParams.w>0.0&&sceneDepth<0.999999){float2 t=1.0/float2(depth.get_width(),depth.get_height());float dl=depth.sample(aoSampler,in.uv-float2(t.x,0.0)),dr=depth.sample(aoSampler,in.uv+float2(t.x,0.0)),du=depth.sample(aoSampler,in.uv-float2(0.0,t.y)),dd=depth.sample(aoSampler,in.uv+float2(0.0,t.y));if(max(max(dl,dr),max(du,dd))<0.999999&&max(max(dl,dr),max(du,dd))-min(min(dl,dr),min(du,dd))<0.08){float3 p=sm64_view_position(in.uv,sceneDepth,inverseProjection);float3 l=sm64_view_position(in.uv-float2(t.x,0.0),dl,inverseProjection),r=sm64_view_position(in.uv+float2(t.x,0.0),dr,inverseProjection),u=sm64_view_position(in.uv-float2(0.0,t.y),du,inverseProjection),d=sm64_view_position(in.uv+float2(0.0,t.y),dd,inverseProjection);float3 n=normalize(cross(r-l,d-u));if(dot(n,-p)<0.0)n=-n;float3 worldNormal=normalize((inverseView*float4(n,0.0)).xyz);float diffuse=max(dot(worldNormal,normalize(sunParams.xyz)),0.0);color.rgb*=1.0+diffuse*sunParams.w*shadowVisibility;}}\n"
        "  if(rayParams>0.5&&sceneDepth<0.999999) color.rgb+=float3(1.0,0.83,0.60)*godRays.sample(imageSampler,in.uv).r;\n"
        "  if(presentFlags.w!=0u) { float2 t=1.0/float2(image.get_width(),image.get_height()); float l=dot(color.rgb,float3(0.299,0.587,0.114)); float ln=dot(image.sample(imageSampler,in.uv+float2(0.0,-t.y)).rgb,float3(0.299,0.587,0.114)); float ls=dot(image.sample(imageSampler,in.uv+float2(0.0,t.y)).rgb,float3(0.299,0.587,0.114)); float le=dot(image.sample(imageSampler,in.uv+float2(t.x,0.0)).rgb,float3(0.299,0.587,0.114)); float lw=dot(image.sample(imageSampler,in.uv+float2(-t.x,0.0)).rgb,float3(0.299,0.587,0.114)); float edge=max(max(ln,ls),max(le,lw))-min(min(ln,ls),min(le,lw)); if(edge>max(0.0312,l*0.125)) color.rgb=mix(color.rgb,(image.sample(imageSampler,in.uv+float2(t.x,t.y)).rgb+image.sample(imageSampler,in.uv-float2(t.x,t.y)).rgb+image.sample(imageSampler,in.uv+float2(-t.x,t.y)).rgb+image.sample(imageSampler,in.uv+float2(t.x,-t.y)).rgb)*0.25,0.5); }\n"
        "  if(gradeParams.y>0.5) { float d=depth.sample(aoSampler,in.uv); if(d<0.999999) { float4 clip=float4(in.uv.x*2.0-1.0,1.0-in.uv.y*2.0,d*2.0-1.0,1.0); float4 view=inverseProjection*clip; float distance=length(view.xyz/max(abs(view.w),1.0e-6)); float fog=1.0-exp(-gradeParams.z*max(distance-gradeParams.w,0.0)); color.rgb=mix(color.rgb,fogColor.rgb,clamp(fog,0.0,1.0)); } }\n"
        "  if(bloomParams.x>0.5) color.rgb+=bloom.sample(aoSampler,in.uv).rgb*bloomParams.y; float luma=dot(color.rgb,float3(0.2126,0.7152,0.0722)); color.rgb=mix(float3(luma),color.rgb,gradeStyle.x); color.rgb=(color.rgb-0.18)*gradeStyle.y+0.18; color.rgb+=float3(gradeStyle.z,0.0,-gradeStyle.z)+gradeStyle.w; if(presentFlags.z!=0u) { float3 x=max(color.rgb*exp2(gradeParams.x),0.0); if(bloomParams.w<0.5) color.rgb=clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0); else color.rgb=x/(1.0+x); } return color;\n"
        "}\n";
}

const char *gfx_metal_ssao_shader_source(void) {
    return
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "inline float3 ssaoViewPosition(float2 uv, float depth, constant float4x4 &invP) {\n"
        " float4 clip=float4(uv.x*2.0-1.0,1.0-uv.y*2.0,depth*2.0-1.0,1.0);\n"
        " float4 p=invP*clip; return p.xyz/max(abs(p.w),1.0e-6)*sign(p.w); }\n"
        "kernel void sm64_ssao(texture2d<float,access::read> depthTex [[texture(0)]],\n"
        " texture2d<float,access::write> aoTex [[texture(1)]],\n"
        " constant float4x4 &inverseProjection [[buffer(0)]],\n"
        " constant float4 &parameters [[buffer(1)]],\n"
        " constant uint &sampleCount [[buffer(2)]],\n"
        " uint2 tid [[thread_position_in_grid]]) {\n"
        " if(tid.x>=aoTex.get_width()||tid.y>=aoTex.get_height()) return;\n"
        " uint2 size=uint2(depthTex.get_width(),depthTex.get_height());\n"
        " uint2 p=min(tid*2u+1u,size-1u); float d=depthTex.read(p).r;\n"
        " if(d>=0.999999) { aoTex.write(float4(1.0),tid); return; }\n"
        " float2 uv=(float2(p)+0.5)/float2(size);\n"
        " float3 center=ssaoViewPosition(uv,d,inverseProjection);\n"
        " uint2 leftP=uint2(max(int(p.x)-1,0),p.y), rightP=uint2(min(p.x+1,size.x-1),p.y);\n"
        " uint2 upP=uint2(p.x,max(int(p.y)-1,0)), downP=uint2(p.x,min(p.y+1,size.y-1));\n"
        " float dl=depthTex.read(leftP).r,dr=depthTex.read(rightP).r,du=depthTex.read(upP).r,dd=depthTex.read(downP).r;\n"
        " if(dl>=0.999999||dr>=0.999999||du>=0.999999||dd>=0.999999||max(max(dl,dr),max(du,dd))-min(min(dl,dr),min(du,dd))>0.08) { aoTex.write(float4(1.0),tid); return; }\n"
        " float3 leftV=ssaoViewPosition((float2(leftP)+0.5)/float2(size),dl,inverseProjection);\n"
        " float3 rightV=ssaoViewPosition((float2(rightP)+0.5)/float2(size),dr,inverseProjection);\n"
        " float3 upV=ssaoViewPosition((float2(upP)+0.5)/float2(size),du,inverseProjection);\n"
        " float3 downV=ssaoViewPosition((float2(downP)+0.5)/float2(size),dd,inverseProjection);\n"
        " float3 normal=normalize(cross(rightV-leftV,downV-upV)); if(dot(normal,-center)<0.0) normal=-normal;\n"
        " const short2 offsets[8]={short2(1,0),short2(-1,0),short2(0,1),short2(0,-1),short2(1,1),short2(-1,1),short2(1,-1),short2(-1,-1)};\n"
        " float radius=max(parameters.x,1.0); float bias=max(parameters.y,0.0);\n"
        " float intensity=max(parameters.z,0.0); float occlusion=0.0;\n"
        " for(uint i=0;i<min(sampleCount,8u);i++){ int2 q=clamp(int2(p)+int2(offsets[i])*int(radius),int2(0),int2(size)-1);\n"
        " float sd=depthTex.read(uint2(q)).r; if(sd>=0.999999) continue;\n"
        " float3 samplePos=ssaoViewPosition((float2(q)+0.5)/float2(size),sd,inverseProjection);\n"
        " float3 delta=samplePos-center; float distance=length(delta);\n"
        " float hemisphere=dot(normal,delta/max(distance,1.0e-5));\n"
        " if(distance>1.0e-4&&distance<parameters.w&&hemisphere>bias)\n"
        " occlusion+=hemisphere*(1.0-distance/parameters.w); }\n"
        " float ao=clamp(1.0-intensity*occlusion/8.0,0.0,1.0); aoTex.write(float4(ao),tid);\n"
        "}\n"
        "kernel void sm64_ssao_denoise(texture2d<float,access::read> depthTex [[texture(0)]],\n"
        " texture2d<float,access::read> sourceAO [[texture(1)]],\n"
        " texture2d<float,access::write> filteredAO [[texture(2)]],\n"
        " uint2 tid [[thread_position_in_grid]]) {\n"
        " if(tid.x>=filteredAO.get_width()||tid.y>=filteredAO.get_height()) return;\n"
        " uint2 size=uint2(filteredAO.get_width(),filteredAO.get_height());\n"
        " uint2 depthSize=uint2(depthTex.get_width(),depthTex.get_height()); uint2 p=min(tid*2u+1u,depthSize-1u); float d=depthTex.read(p).r;\n"
        " if(d>=0.999999) { filteredAO.write(float4(1.0),tid); return; }\n"
        " float sum=0.0,weightSum=0.0; const float weights[3]={1.0,2.0,1.0};\n"
        " for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++){\n"
        " uint2 q=uint2(clamp(int2(tid)+int2(x,y),int2(0),int2(size)-1));\n"
        " float qd=depthTex.read(min(q*2u+1u,depthSize-1u)).r; if(qd>=0.999999) continue;\n"
        " float w=weights[x+1]*weights[y+1]*exp(-abs(qd-d)*180.0);\n"
        " sum+=sourceAO.read(q).r*w; weightSum+=w; }\n"
        " float filtered=weightSum>1.0e-5?sum/weightSum:sourceAO.read(tid).r;\n"
        " filteredAO.write(float4(filtered),tid); }\n";
}

const char *gfx_metal_bloom_shader_source(void) {
    return
        "#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void sm64_bloom_extract(texture2d<float,access::read> scene [[texture(0)]], texture2d<float,access::write> bright [[texture(1)]], constant float &threshold [[buffer(0)]], uint2 tid [[thread_position_in_grid]]) {\n"
        " if(tid.x>=bright.get_width()||tid.y>=bright.get_height()) return; uint2 size=uint2(scene.get_width(),scene.get_height()); float3 c=float3(0.0);\n"
        " for(uint y=0;y<4;y++) for(uint x=0;x<4;x++) c+=scene.read(min(tid*4u+uint2(x,y),size-1u)).rgb*(1.0/16.0);\n"
        " float peak=max(c.r,max(c.g,c.b)); float contribution=max(peak-threshold,0.0)/max(peak,1.0e-5); bright.write(float4(c*contribution,1.0),tid); }\n"
        "kernel void sm64_bloom_blur(texture2d<float,access::read> source [[texture(0)]], texture2d<float,access::write> output [[texture(1)]], constant uint &axis [[buffer(0)]], uint2 tid [[thread_position_in_grid]]) {\n"
        " if(tid.x>=output.get_width()||tid.y>=output.get_height()) return; const float w[5]={0.0625,0.25,0.375,0.25,0.0625}; float4 c=float4(0.0);\n"
        " for(int i=-2;i<=2;i++){ int2 delta=axis==0u?int2(i,0):int2(0,i); uint2 q=uint2(clamp(int2(tid)+delta,int2(0),int2(output.get_width(),output.get_height())-1)); c+=source.read(q)*w[i+2]; } output.write(c,tid); }\n";
}

const char *gfx_metal_shadow_shader_source(void) {
    return "#include <metal_stdlib>\nusing namespace metal;\n"
           "vertex float4 sm64_shadow_vertex(uint id [[vertex_id]], const device float4 *positions [[buffer(0)]]) { return positions[id]; }\n";
}

const char *gfx_metal_god_ray_shader_source(void) {
    return "#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void sm64_god_rays(depth2d<float,access::read> depthTex [[texture(0)]], texture2d<float,access::write> raysTex [[texture(1)]], constant float2 &sunUV [[buffer(0)]], constant float &strength [[buffer(1)]], constant uint &sampleCount [[buffer(2)]], uint2 tid [[thread_position_in_grid]]) {\n"
        " if(tid.x>=raysTex.get_width()||tid.y>=raysTex.get_height()) return; uint2 size=uint2(depthTex.get_width(),depthTex.get_height()); float2 uv=(float2(tid)+0.5)/float2(raysTex.get_width(),raysTex.get_height()); uint2 center=min(uint2(uv*float2(size)),size-1u); float centerDepth=depthTex.read(center); if(centerDepth>=0.999999){raysTex.write(float4(0.0),tid);return;}\n"
        " float visibility=0.0; uint count=clamp(sampleCount,1u,16u); for(uint i=0;i<count;i++){float t=(float(i)+0.5)/float(count); float2 q=uv+(sunUV-uv)*t; if(any(q<0.0)||any(q>1.0)) continue; uint2 p=min(uint2(q*float2(size)),size-1u); visibility+=depthTex.read(p)>=0.999999?1.0:0.0;} float rays=clamp((visibility/float(count))*strength*0.65,0.0,0.35); raysTex.write(float4(rays),tid); }\n";
}

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

static void write_bytes(struct ShaderWriter *w, const char *bytes, size_t size) {
    if (!w->ok || w->used >= w->size || size >= w->size - w->used) {
        w->ok = false;
        return;
    }
    memcpy(w->out + w->used, bytes, size);
    w->used += size;
    w->out[w->used] = '\0';
}

static bool shader_source_contains(const char *source, size_t size, const char *needle) {
    size_t length = strlen(needle);
    if (length > size) return false;
    for (size_t i = 0; i <= size - length; ++i)
        if (memcmp(source + i, needle, length) == 0) return true;
    return false;
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

bool gfx_metal_build_shader_with_material(uint32_t shader_id,
                                         const char *material_source, size_t material_size,
                                         char *out, size_t out_size,
                                         struct CCFeatures *features) {
    struct CCFeatures local;
    struct GfxMetalVertexLayout vertex_layout;
    struct ShaderWriter w = { out, out_size, 0, out != NULL && out_size > 1 };
    int i;
    if (!features) features = &local;
    if (!w.ok) return false;
    if ((material_source == NULL) != (material_size == 0) ||
        material_size > 256u * 1024u ||
        (material_source && (memchr(material_source, '\0', material_size) ||
         !shader_source_contains(material_source, material_size, "float4 shader64_material(") ||
         !shader_source_contains(material_source, material_size, "constant float4 &params"))))
        return false;
    gfx_cc_get_features(shader_id, features);
    if (features->scene_material &&
        ((features->used_textures[1] && !features->used_textures[0]) ||
         (features->scene_blend && features->opt_texture_edge) ||
         (shader_id & ~(SHADER_OPT_SCENE_MATERIAL | SHADER_SCENE_FOG | 15u)))) return false;
    if (!gfx_metal_get_vertex_layout(features, &vertex_layout)) return false;
    out[0] = '\0';

    write_text(&w, "#include <metal_stdlib>\nusing namespace metal;\n");
    write_text(&w,
        "struct FragmentParams { uint filter0; uint filter1; uint diagnosticMode; "
        "uint textureId0; uint textureId1; uint materialClass; float frameCount; };\n"
        "float3 diagnosticTextureColor(uint textureId) {\n"
        "float hue=fract(float(textureId)*0.61803398875);\n"
        "float3 color=clamp(abs(fract(hue+float3(0.0,0.6666667,0.3333333))*6.0-3.0)-1.0,0.0,1.0);\n"
        "return mix(float3(0.18),float3(0.95),color); }\n");
    if (features->used_textures[0] || features->used_textures[1]) {
        write_text(&w,
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
    if (features->opt_alpha && features->opt_noise) {
        write_text(&w, "float randomValue(float3 value) {\n"
                       "float random=dot(sin(value),float3(12.9898,78.233,37.719));\n"
                       "return fract(sin(random)*143758.5453); }\n");
    }
    write_text(&w,
                   "struct VIn { float4 pos [[attribute(0)]];\n");
    /* Keep MSL's explicit attribute indices in the same packed order as the
     * MTLVertexDescriptor: position, optional UV, optional fog, then inputs.
     * Starting inputs at 2 unconditionally left holes for absent UV/fog
     * attributes, which Metal rejects during pipeline creation. */
    {
        if (vertex_layout.has_uv) {
            write_format(&w, "float2 uv [[attribute(%d)]];\n", vertex_layout.uv_attribute, 0);
        }
        if (vertex_layout.uv1_attribute != UINT8_MAX) {
            write_format(&w, "float2 uv1 [[attribute(%d)]];\n", vertex_layout.uv1_attribute, 0);
        }
        if (vertex_layout.has_fog) {
            write_format(&w, "float4 fog [[attribute(%d)]];\n", vertex_layout.fog_attribute, 0);
        }
        for (i = 0; i < features->num_inputs; ++i) {
            write_format(&w, features->opt_alpha ? "float4 input%d [[attribute(%d)]];\n" :
                         "float3 input%d [[attribute(%d)]];\n", i + 1, vertex_layout.input_attribute[i]);
        }
    }
    write_text(&w, "};\nstruct VOut { float4 pos [[position]]; float3 barycentric;\n");
    if (features->used_textures[0] || features->used_textures[1]) write_text(&w, "float2 uv;\n");
    if (vertex_layout.uv1_attribute != UINT8_MAX) write_text(&w, "float2 uv1;\n");
    if (features->opt_fog) write_text(&w, "float4 fog;\n");
    for (i = 0; i < features->num_inputs; ++i)
        write_format(&w, features->opt_alpha ? "float4 input%d;\n" : "float3 input%d;\n", i + 1, 0);
    write_text(&w,
        "};\nvertex VOut sm64_vertex(VIn in [[stage_in]], uint vertex_id [[vertex_id]]) { "
        "VOut o; o.pos=in.pos; uint corner=vertex_id%3u; "
        "o.barycentric=corner==0u?float3(1.0,0.0,0.0):(corner==1u?float3(0.0,1.0,0.0):float3(0.0,0.0,1.0));\n");
    if (features->used_textures[0] || features->used_textures[1]) write_text(&w, "o.uv=in.uv;\n");
    if (vertex_layout.uv1_attribute != UINT8_MAX) write_text(&w, "o.uv1=in.uv1;\n");
    if (features->opt_fog) write_text(&w, "o.fog=in.fog;\n");
    for (i = 0; i < features->num_inputs; ++i)
        write_format(&w, "o.input%d=in.input%d;\n", i + 1, i + 1);
    write_text(&w, "return o; }\n");
    if (material_source) {
        write_bytes(&w, material_source, material_size);
        write_text(&w, "\n");
    }
    write_text(&w, "fragment float4 sm64_fragment(VOut in [[stage_in]]");
    if (features->used_textures[0]) write_text(&w, ", texture2d<float> tex0 [[texture(0)]], sampler samp0 [[sampler(0)]]");
    if (features->used_textures[1]) write_text(&w, ", texture2d<float> tex1 [[texture(1)]], sampler samp1 [[sampler(1)]]");
    write_text(&w, ", constant FragmentParams &params [[buffer(1)]]");
    if (material_source) write_text(&w, ", constant float4 &shader64Parameters [[buffer(2)]]");
    write_text(&w, ") {\n");
    /* Diagnostic mode 1 substitutes opaque white at the combiner input. This
     * avoids touching texture memory while retaining the original geometry,
     * vertex colors, combiner, fog, depth, and blend state. */
    if (features->used_textures[0])
        write_text(&w, "float4 texel0=params.diagnosticMode==1u?float4(1.0):sampleTex(tex0,samp0,in.uv,params.filter0!=0);\n");
    if (features->used_textures[1])
        write_text(&w, features->scene_material
            ? "float4 texel1=params.diagnosticMode==1u?float4(1.0):sampleTex(tex1,samp1,in.uv1,params.filter1!=0);\n"
            : "float4 texel1=params.diagnosticMode==1u?float4(1.0):sampleTex(tex1,samp1,in.uv,params.filter1!=0);\n");
    write_text(&w, "if(params.diagnosticMode==2u) { ");
    if (features->used_textures[0] || features->used_textures[1])
        write_text(&w, "return float4(fract(in.uv),0.0,1.0); ");
    else
        write_text(&w, "return float4(1.0,0.0,1.0,1.0); ");
    write_text(&w, "}\nif(params.diagnosticMode==3u) { float3 idColor=float3(0.18); ");
    if (features->used_textures[0])
        write_text(&w, "idColor=diagnosticTextureColor(params.textureId0); ");
    if (features->used_textures[1]) {
        if (features->used_textures[0])
            write_text(&w, "idColor=mix(idColor,diagnosticTextureColor(params.textureId1),0.5); ");
        else
            write_text(&w, "idColor=diagnosticTextureColor(params.textureId1); ");
    }
    write_text(&w, "return float4(idColor,1.0); }\n");
    write_text(&w, "if(params.diagnosticMode==4u) { float3 fw=max(fwidth(in.barycentric),float3(0.00001)); "
                   "float3 line=smoothstep(float3(0.0),fw*1.25,in.barycentric); "
                   "float edge=1.0-min(min(line.x,line.y),line.z); "
                   "return float4(mix(float3(0.78),float3(0.04),edge),1.0); }\n");
    write_text(&w, "if(params.diagnosticMode==5u) { float3 c=params.materialClass==3u?float3(1.0,0.2,0.85):(params.materialClass==2u?float3(1.0,0.55,0.1):(params.materialClass==1u?float3(0.2,0.75,1.0):float3(0.3))); return float4(c,1.0); }\n");
    if (features->scene_material) {
        const char *sample = features->used_textures[1] ? "mix(texel0,texel1,in.input3.a)"
                           : features->used_textures[0] ? "texel0" : "float4(1.0)";
        write_text(&w, "float4 color=");
        write_text(&w, sample);
        write_text(&w, "*in.input1*in.input2*float4(in.input3.rgb,1.0);\n");
        if (features->opt_texture_edge)
            write_text(&w, "if(color.a<0.5) discard_fragment();\n");
        if (!features->scene_blend) write_text(&w, "color.a=1.0;\n");
        if (features->opt_fog)
            write_text(&w, "color=float4(mix(color.rgb,in.fog.rgb,in.fog.a),color.a);\n");
        if (material_source) {
            write_text(&w, "return shader64_material(color,");
            write_text(&w, (features->used_textures[0] || features->used_textures[1]) ? "in.uv" : "float2(0.0)");
            write_text(&w, ",in.pos,params.materialClass,shader64Parameters); }\n");
        } else {
            write_text(&w, "return color; }\n");
        }
        return w.ok;
    }
    if (features->opt_alpha && !features->color_alpha_same) {
        write_text(&w, "float4 color=float4(");
        write_formula(&w, features, 0, false, false);
        write_text(&w, ", ");
        if (features->opt_const_alpha_one) write_text(&w, "1.0");
        else write_formula(&w, features, 1, true, true);
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
    if (features->opt_alpha && features->opt_noise)
        write_text(&w, "color.a*=floor(randomValue(floor(float3(in.pos.xy,params.frameCount)))+0.5);\n");
    if (material_source) {
        write_text(&w, "return shader64_material(color,");
        write_text(&w, (features->used_textures[0] || features->used_textures[1]) ? "in.uv" : "float2(0.0)");
        write_text(&w, ",in.pos,params.materialClass,shader64Parameters); }\n");
    } else {
        write_text(&w, "return color; }\n");
    }
    return w.ok;
}

bool gfx_metal_build_shader(uint32_t shader_id, char *out, size_t out_size,
                            struct CCFeatures *features) {
    return gfx_metal_build_shader_with_material(shader_id, NULL, 0, out, out_size, features);
}
