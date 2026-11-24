# Asset Inspector, Placeable-Asset Transforms and Prefabs — Design

**Status:** Approved by the owner, 2026-11-23 (override model: option 1, per-property overrides).
**Order:** Part 1 (Asset Inspector + placeable transforms), then Part 2 (Prefabs). Nested prefabs come after Part 2.

## Intent

Assets are first-class in the editor, as in Unity's Inspector. Clicking any asset in the Content Browser
shows its properties and information. Anything that can be placed in a level carries a default Transform.
Reusable objects (prefabs) are made by dragging objects out of the level, and they stay linked to what
they came from.

## Part 1 — Asset Inspector

- **Selection:** clicking an asset in the Content Browser makes the Inspector show that asset. Clicking a
  level object (viewport or Outliner) switches back. The most recent click wins. Deleting or renaming the
  asset clears or follows the selection.
- **Common header (every asset):**
  - thumbnail, name, type, virtual path, file size, last modified, asset ID
  - **Referenced by** (assets and levels that use it)
  - **Open**, **Browse**, **Reimport** (where the asset has a source file)
- **Per type:**
  - **Material:** domain, blend mode, shading model, two-sided; parameters editable (shared defaults; same
    rows as the Inspector slot parameters).
  - **Material Instance:** parent; parameters editable with override markers and reset.
  - **Mesh:** preview, vertices/triangles, sections with their default materials (editable slots), LODs,
    bounds (metres), import settings + Reimport.
  - **Texture:** preview, resolution, format, sRGB, mips, memory size; import settings + Reimport.
  - **Sound:** duration, channels, sample rate, waveform, play/stop.
  - **Scene:** entity count, last saved; Open.
  - **Other:** common header only.
- **Placeable assets carry a Transform** (meshes and prefabs, and any future placeable type):
  - position offset, rotation (degrees), scale, stored in the asset's `.meta`
  - edited in the asset Inspector
  - dropping the asset into the level composes it with the drop placement (drop point = translation; the
    asset transform's offset, rotation and scale apply on top)
  - default = identity; existing assets are unchanged until edited

## Part 2 — Prefabs

- **Create:**
  - drag an object (with its children) from the Outliner onto a Content Browser folder or empty area
  - this writes `<Name>.lprefab`: the object subtree serialized through the component registry, so plugin
    components are included and unknown ones preserved
  - the original object becomes a linked instance of it
- **Place:** drag a prefab into the viewport or Outliner. This creates a linked instance, using the prefab
  asset's placeable Transform rules from Part 1.
- **Edit the asset:**
  - click the prefab in the Content Browser; the Inspector shows its root object's components, editable
    exactly like a level object
  - saving updates every linked instance in open levels
- **Link data:**
  - each instance root has a prefab link component: prefab asset ID, plus a stable per-object ID map for
    its children, plus the list of overridden properties
  - levels save the link and the overrides, not a full copy
  - prefab changes made while a level is closed appear when it opens
- **Overrides (per property, Unity model):**
  - editing a property on an instance marks that property overridden (bold label + marker in Inspector)
  - overridden properties keep their value when the prefab changes; everything else follows it
  - the instance root's Transform is always instance-owned (placing it is not an override)
  - adding or removing a component on an instance is recorded as an override too
- **Instance actions** (Inspector header; Outliner shows instances in blue with a prefab icon):
  - **Select Asset**
  - **Apply** (push overrides into the prefab), per property or all
  - **Revert** (drop overrides), per property or all
  - **Unpack** (break the link, keeping the current values)
- **Missing prefab:** an instance whose prefab is missing keeps its last values and shows
  "Missing prefab"; nothing is deleted.
- **Undo:** integrates with the editor-wide undo system (T-0002) when it exists.

## Non-goals (now)

Nested prefabs and prefab variants (after Part 2), runtime prefab spawning API (with the runtime work, F1).
