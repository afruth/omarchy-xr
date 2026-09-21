# Generated environment concepts

`twilight.png` and `deep-space.png` were generated on 2026-09-20 with the built-in
image-generation tool. The tool did not expose a model selector; no specific
GPT Image model version is claimed. All eight originals are 1774 × 887 PNGs. They are
concept panoramas, not measured HDR lighting maps. Spherical seam/pole quality
has not been validated on the glasses.

`upscaled/` contains 7096 × 3548 derivatives processed locally with Upscayl's
`high-fidelity-4x` model. The installer prefers these when available; original
PNGs remain unchanged. These are AI-enhanced assets, not native high-resolution
renders. Processing adds 64 pixels of horizontal wrapped context on each side,
then crops it away at 4× scale. This avoids independently processing the panorama
boundaries, but does not repair discontinuities already present in the originals.

Engine version, model hashes, and settings are recorded in
`upscaled/provenance.json`. To reproduce with an installed Upscayl CLI:

```sh
python scripts/upscale-environments.py --models /path/to/models --gpu 1
```

Check the Vulkan device ID for your computer. The script skips existing outputs.
Upscaling runs offline during asset preparation, never inside the XR renderer.

Prompt specifications:

- Twilight: one full-frame 2:1 equirectangular latitude-longitude panorama covering
  360° × 180°, quiet twilight above distant soft clouds, subtle blue-violet
  atmosphere and muted warmth at a level mid-height horizon. Low contrast for
  long coding sessions; no sun disc, bright stars, foreground objects, text,
  monitors, people, framing or watermark. Matching left/right boundaries,
  uniform zenith/nadir, with appropriate polar stretching. Texture only, not a
  sphere mockup. Runtime dimming is separate.
- Deep space: one full-frame 2:1 equirectangular panorama covering 360° × 180°,
  deep navy/charcoal, sparse tiny softly luminous stars and faint broad blue/violet
  dust. Understated and low contrast; no planets, sun, bright galactic core,
  foreground objects, text, UI, borders, watermark or starbursts. Matching
  left/right boundaries and nearly uniform poles; texture only, not a mockup.

Six additional environments: Omarchy Dusk, Alien Shore, Harbor Skyline, Open Sea,
Aurora, and Forest Lake. Their exact generation prompts are in `prompts.json`.

The Viz-People skies were removed from the local library at the user's request.
They are not bundled here; the original downloaded archive was retained.
