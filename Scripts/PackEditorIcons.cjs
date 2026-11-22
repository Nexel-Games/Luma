// Rebuild the checked-in Slate texture after changing IconSources.inc.
// If needed: npm install --prefix Content/Editor/Icons/ionicons --no-save --package-lock=false sharp@0.35.3
// The editor loads the checked-in PNG directly.
const fs = require("node:fs/promises");
const path = require("node:path");
const icons = path.resolve(__dirname, "../Content/Editor/Icons");
const sharp = require(path.join(icons, "ionicons/node_modules/sharp"));
const manifest = path.resolve(__dirname, "../Engine/UI/Slate/include/Luma/Slate/IconSources.inc");
const cell = 32;

async function pack() {
    const lines = (await fs.readFile(manifest, "utf8")).split(/\r?\n/);
    const layers = [];
    for (const line of lines.filter(line => line.trim() && !line.startsWith("//"))) {
        const match = /^LUMA_ICON\(\w+, "([^"]+)"\)$/.exec(line);
        if (!match) throw new Error("Invalid icon source: " + line);
        const source = match[1];
        // Luma icon pack sources live beside Icons/ (Content/Editor/Luma_Icons, Luma_Icons_V2).
        const sourceRoot = source.startsWith("ThirdParty/") ? path.resolve(__dirname, "..")
                         : source.startsWith("Luma_Icons") ? path.resolve(icons, "..") : icons;
        const tile = sharp(path.join(sourceRoot, source))
            .resize(cell - 2, cell - 2, { fit: "contain", background: "#00000000" })
            .ensureAlpha();
        // Ionicons' path silhouettes take their foreground colour from Slate.
        if (source.startsWith("ionicons/")) tile.linear([0, 0, 0, 1], [255, 255, 255, 0]);
        layers.push({ input: await tile.png().toBuffer(), left: layers.length * cell + 1, top: 1 });
    }
    await sharp({ create: { width: layers.length * cell, height: cell, channels: 4,
                            background: "#00000000" } })
        .composite(layers).png().toFile(path.join(icons, "SlateIcons.png"));
}

pack().catch(error => { console.error(error); process.exitCode = 1; });
