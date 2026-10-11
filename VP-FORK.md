# VideoProcessor libplacebo fork

Branch: `codex/vp-master-beta` (experimental). Upstream pinned to
`1dd022687b9ac1dd996855b51433bf770ae76aab` (API 375).
The VP fork uses API 376 to give its extended structures and DLL a distinct ABI.

VP patches:
- Analysis crop for peak detection, including master scene-average/maxRGB
  measurements used by dynamic target. Excluded pixels do not affect statistics.
- D3D11 timer allocation and diagnostic suspension controls, recovered from
  the exact shipped release source archive.
- `pl_color_map_params.dynamic_target` / `dynamic_target=yes|no` option.
  Defaults to enabled. Disabling bypasses virtual target/exposure adaptation;
  peak detection, film response and physical display luminance stay independent.

Sibling branch `codex/vp-release-7.360.1` reconstructs the full shipped release
source (analysis crop plus timers); the VP stable binary remains unchanged.

## Windows build

Use MSYS2 mingw64 gcc, shaderc, spirv-cross, lcms2 and libdovi. Initialize
submodules with `git submodule update --init --recursive`.

```sh
meson setup build-release --buildtype=release --default-library=shared \
  -Dd3d11=enabled -Dshaderc=enabled -Dglslang=disabled -Dlcms=enabled \
  -Ddovi=enabled -Dlibdovi=enabled -Dtests=true -Ddemos=false \
  -Dvulkan=disabled -Dopengl=disabled
meson compile -C build-release
meson test -C build-release --print-errorlogs
```

GPU tests cover cropped luminance/maxRGB, timer controls, and actual rendered
HDR output with dynamic target on/off/on (including reused LUT state).
Scene fixtures and visual gamut qualification are separate from these tests.
