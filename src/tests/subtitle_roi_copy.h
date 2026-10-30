#pragma once
#include <libplacebo/cache.h>
#include "gpu.h"

struct subtitle_roi_case {
    const char *name;
    pl_rect3d src, dst;
    int width, height;
};

static uint16_t subtitle_roi_value(int x, int y, int c)
{
    switch (c) {
    case 0: return 0x3000 | (x & 0x3ff);
    case 1: return 0x3800 | (y & 0x3ff);
    case 2: return 0x2800 | ((x + 3*y) & 0x3ff);
    default: return 0x3c00;
    }
}

static void subtitle_roi_copy_tests(pl_gpu gpu)
{
    const int sw=3840, sh=2160;
    const uint16_t sentinel=0x2400;
    const bool expect_shader=getenv("PL_TEST_ROI_EXPECT_SHADER_COPY") != NULL;
    pl_fmt fmt=pl_find_fmt(gpu, PL_FMT_FLOAT, 4, 16, 16,
        PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_BLITTABLE | PL_FMT_CAP_HOST_READABLE);
    REQUIRE(fmt && !fmt->emulated && fmt->texel_size == 8);
    uint16_t *pixels=malloc((size_t)sw*sh*8);
    REQUIRE(pixels);
    for(int y=0;y<sh;y++) for(int x=0;x<sw;x++) for(int c=0;c<4;c++)
        pixels[((size_t)y*sw+x)*4+c]=subtitle_roi_value(x,y,c);
    pl_tex source=pl_tex_create(gpu, pl_tex_params(
        .w=sw, .h=sh, .format=fmt, .sampleable=true, .blit_src=true,
        .initial_data=pixels));
    REQUIRE(source);
    free(pixels);
    pl_cache cache=pl_cache_create(pl_cache_params(.log=gpu->log));
    REQUIRE(cache);
    pl_gpu_set_cache(gpu,cache);
    printf("ROI_PROBE version=%s api=%d format=%s source=%dx%d expected=%s\n",
        pl_version(),PL_API_VER,fmt->name,sw,sh,expect_shader?"shader-copy-baseline":"direct-copy");
    const struct subtitle_roi_case cases[]={
        {"logged-before", {1457,1603,0,2378,1864,1}, {0,0,0,921,261,1},921,261},
        {"logged-after", {1440,1603,0,2395,1864,1}, {0,0,0,955,261,1},955,261},
        {"source-origin", {1441,1603,0,2396,1864,1}, {0,0,0,955,261,1},955,261},
        {"destination-origin", {1440,1603,0,2395,1864,1}, {7,5,0,962,266,1},969,273},
        {"small-partial-groups", {2,3,0,35,20,1}, {3,4,0,36,21,1},40,28},
    };
    for(int i=0;i<PL_ARRAY_SIZE(cases);i++) {
        const struct subtitle_roi_case *t=&cases[i];
        const size_t words=(size_t)t->width*t->height*4;
        uint16_t *initial=malloc(words*sizeof(*initial));
        uint16_t *actual=malloc(words*sizeof(*actual));
        REQUIRE(initial && actual);
        for(size_t j=0;j<words;j++)initial[j]=sentinel;
        pl_tex target=pl_tex_create(gpu,pl_tex_params(
            .w=t->width,.h=t->height,.format=fmt,.blit_dst=true,.host_readable=true,
            .initial_data=initial));
        REQUIRE(target);
        const int before=pl_cache_objects(cache);
        const uint64_t signature_before=pl_cache_signature(cache);
        const pl_clock_t started=pl_clock_now();
        pl_tex_blit(gpu,pl_tex_blit_params(.src=source,.dst=target,
            .src_rc=t->src,.dst_rc=t->dst,.sample_mode=PL_TEX_SAMPLE_NEAREST));
        const double elapsed_ms=1000.0*pl_clock_diff(pl_clock_now(),started);
        const int after=pl_cache_objects(cache);
        const uint64_t signature_after=pl_cache_signature(cache);
        REQUIRE(pl_tex_download(gpu,pl_tex_transfer_params(.tex=target,.ptr=actual)));
        size_t mismatches=0;
        for(int y=0;y<t->height;y++) for(int x=0;x<t->width;x++) for(int c=0;c<4;c++) {
            const bool inside=x>=t->dst.x0 && x<t->dst.x1 && y>=t->dst.y0 && y<t->dst.y1;
            const uint16_t expected=inside?subtitle_roi_value(
                x-t->dst.x0+t->src.x0,y-t->dst.y0+t->src.y0,c):sentinel;
            mismatches+=actual[((size_t)y*t->width+x)*4+c]!=expected;
        }
        printf("ROI_CASE name=%s bytes=%zu mismatches=%zu cache=%d->%d signature=%"PRIx64"->%"PRIx64" cpu_ms=%.3f\n",
            t->name,words*sizeof(*actual),mismatches,before,after,signature_before,signature_after,elapsed_ms);
        REQUIRE(mismatches==0);
        if(expect_shader) REQUIRE(after>before && signature_after!=signature_before);
        else REQUIRE(after==before && signature_after==signature_before);
        pl_tex_destroy(gpu,&target);
        free(initial);free(actual);
    }
    // The fast path must not take over scaling or either flip. Compare with
    // the existing compute implementation, preserving its current behavior.
    const struct subtitle_roi_case fallback[]={
        {"scale",{10,20,0,26,36,1},{0,0,0,32,32,1},32,32},
        {"flip-x",{26,20,0,10,36,1},{0,0,0,16,16,1},16,16},
        {"flip-y",{10,36,0,26,20,1},{0,0,0,16,16,1},16,16},
    };
    for(int i=0;i<PL_ARRAY_SIZE(fallback);i++) {
        const struct subtitle_roi_case *t=&fallback[i];
        pl_tex target=pl_tex_create(gpu,pl_tex_params(.w=t->width,.h=t->height,
            .format=fmt,.blit_dst=true,.storable=true,.host_readable=true));
        pl_tex reference=pl_tex_create(gpu,pl_tex_params(.w=t->width,.h=t->height,
            .format=fmt,.blit_dst=true,.storable=true,.host_readable=true));
        REQUIRE(target && reference);
        struct pl_tex_blit_params copy={.src=source,.dst=target,
            .src_rc=t->src,.dst_rc=t->dst,.sample_mode=PL_TEX_SAMPLE_NEAREST};
        const int before=pl_cache_objects(cache);
        pl_tex_blit(gpu,&copy);
        const int after=pl_cache_objects(cache);
        copy.dst=reference;
        REQUIRE(pl_tex_blit_compute(gpu,&copy));
        const size_t bytes=(size_t)t->width*t->height*8;
        uint16_t *actual=malloc(bytes),*expected=malloc(bytes);
        REQUIRE(actual && expected);
        REQUIRE(pl_tex_download(gpu,pl_tex_transfer_params(.tex=target,.ptr=actual)));
        REQUIRE(pl_tex_download(gpu,pl_tex_transfer_params(.tex=reference,.ptr=expected)));
        const bool identical=memcmp(actual,expected,bytes)==0;
        printf("ROI_FALLBACK name=%s bytes=%zu reference_identical=%d cache=%d->%d\n",
            t->name,bytes,identical,before,after);
        REQUIRE(identical && after>before);
        free(actual);free(expected);
        pl_tex_destroy(gpu,&target);pl_tex_destroy(gpu,&reference);
    }
    printf("ROI_PROBE PASS cases=%zu fallback=%zu cache_objects=%d\n",
        PL_ARRAY_SIZE(cases),PL_ARRAY_SIZE(fallback),pl_cache_objects(cache));
    pl_gpu_set_cache(gpu,NULL);
    pl_cache_destroy(&cache);
    pl_tex_destroy(gpu,&source);
}
