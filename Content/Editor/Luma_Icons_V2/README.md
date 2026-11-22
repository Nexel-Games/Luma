# Luma Engine — Icons V2

**32 original icons.** This is the supplemental utility/UI and improved replacements pack requested after the original 150-icon core pack. The artwork is vector-reconstructed using the agreed metallic gradient icon style; it is not a pixel-for-pixel trace of the concept posters.

## Installation

- `SVG/` contains 32 standalone editable vector icons. Each SVG uses a transparent canvas and fill/stroke gradients.
- `PNG/<size>x<size>/` contains 32 transparent RGBA files at each size: 16, 24, 32, 48, 64, 128, 256 pixels.
- All icon filenames are unique and numerically prefixed. See `ICON_MANIFEST.csv` for the full mapping.
- Copy icons from the chosen PNG resolution into your application icon resource directory. SVG masters are recommended as the canonical source for later themes.

## Styling / recoloring

Gradients are embedded in each SVG and use IDs: `silver`, `dark`, `orange`, `gold`, `green`, `blue`, `red`. Change those gradient stop colors to retheme SVGs. PNG files are rasterized and cannot be recolored without affecting their shading; for precise theme variants, recolor the SVG and re-export.

## Asset counts

- 32 individual SVGs
- 224 PNGs (32 icons × 7 resolutions)
- 256 total icon assets, plus previews, manifest, README

## Usage

Original game-engine editor icon artwork for the Luma Engine project. No Unreal Engine icons or vendor-trademarked imagery copied into these files.
