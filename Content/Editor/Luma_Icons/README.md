# Luma Engine — Reference-Style Icon Library (150)

This second edition corrects the earlier thin-outline treatment. Every icon is a distinct editable SVG rebuilt with glossy metallic fills, beveled-looking highlights, and dark-outline depth to resemble the **visual style** of the user's 150-icon reference sheet. It is an approximation, not pixel-identical artwork.

## Included

- `SVG/`: 150 true vector SVG master files, each on a 64×64 design grid (256px default intrinsic dimensions).
- `PNG/16x16/` through `PNG/256x256/`: 150 lossless alpha-transparent PNG icons at seven sizes, 1,050 PNG files.
- `SOURCE_POSTER_REFERENCES/`: 150 cropped, transparent image fragments from the supplied *raster reference poster* for visual comparison only. Original poster art is only ~30–45px across, so these are NOT high-resolution assets and are not vector masters. Some crops may have image artifacts or misalignment; use as reference.
- `ICON_MANIFEST.csv`: numbering, names, categories, palette designations.
- `Luma_150_Reference_Style_Contact_Sheet.png`: preview of the actual newly exported SVG icons.

## Color / UI notes

The SVG files use named gradient fills: `gold`, `orange`, `steel`, `silver`, `blue`, `green`, `red`, `purple`, and `dark` to reflect the color coding in the reference image. To change color programmatically, replace the gradient stops, or replace fills/strokes with `currentColor` for monochrome icons. Arbitrary flat tinting will not preserve multi-tone metallic shading. All backgrounds are alpha transparent.

For precise pixel-for-pixel icon duplication at 128–256px, individual higher-resolution reference icons or hand-redrawing are required: a 1536×1024 raster contact sheet does not contain vector source information.
