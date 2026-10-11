# VideoProcessor libplacebo fork

Branch: `codex/vp-master-beta` (experimental). Upstream pinned to
`1dd022687b9ac1dd996855b51433bf770ae76aab` (API 375).
The VP fork uses API 377 to give its extended structures and DLL a distinct ABI.

VP patches:
- Analysis crop for peak detection, including master scene-average/maxRGB
  measurements used by dynamic target. Excluded pixels do not affect statistics.
- D3D11 timer allocation and diagnostic suspension controls, recovered from
  the exact shipped release source archive.
- `pl_color_map_params.dynamic_target` / `dynamic_target=yes|no` option.
  Defaults to enabled. Disabling bypasses virtual target/exposure adaptation;
  peak detection, film response and physical display luminance stay independent.

- `pl_color_map_params.vp_gamut_compression` / `vp_gamut_compression=yes|no`.
  Defaults to disabled: the original master mapping path is unchanged. When
  enabled during tone mapping (excluding inverse tone mapping), a continuous
  residual correction follows the complete selected original gamut stage.
  Physical target RGB is normalized by its nominal black and peak. Positive
  overflow uses a common gain with a saturation-dependent 2% shoulder reserve;
  negative channels receive a common neutral lift with a continuous cap at the
  positive original luminance. This avoids creating light from negative-Y input.
  Legal interior pixels bypass the additional transform exactly. Near-boundary
  saturated colors may move by up to about 0.586% of target range. Color and
  brightness tradeoffs remain, particularly for negative-channel blues.
  This replaces the earlier fixed-Y color-volume LUT experiment. No additional
  texture/LUT or environment switch is used; original gamut choices remain active.
  Physical P3 inside a BT.2020 signal container is handled with explicit matrices.
  Alpha, tone curve and dynamic-target controls remain independent. The option
  does not govern later calibration/custom LUT output. Default-on, real-content,
  temporal, physical swapchain/dither and performance qualification remain open.

  Research validation: 105 matched D3D11 linear cases at 50/100/500 nits plus
  nine actual PQ-output controls; exact original/off restoration, no material
  target-bound violations or nominal exposure-Y reversals. Source-code-neighbor
  and negative-luminance controls are included. Small GPU floating-point errors
  require a numerical tolerance; these results are not a universal quality claim.

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
