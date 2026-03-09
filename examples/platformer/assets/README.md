# Platformer Example — Placeholder Assets

These are minimal placeholder assets for the Cactus Platformer example. Each file is a solid-color PNG rectangle. Replace them with production art to make the game look great!

## Asset Inventory

### Sprites (`sprites/`)

| File | Size | Color | Purpose |
|------|------|-------|---------|
| `player.png` | 32×48 | Cornflower blue (#6495ED) | Player character sprite. Replace with a multi-frame sprite sheet for idle, run, jump, and fall animations. |
| `enemy.png` | 32×32 | Red (#CC3333) | Enemy character sprite. Replace with patrol/attack animation frames. |
| `gem.png` | 16×16 | Gold (#FFD700) | Collectible gem/lum sprite. Replace with a rotating gem animation or different gem types (blue, red, gold). |

### Tiles (`tiles/`)

| File | Size | Color | Purpose |
|------|------|-------|---------|
| `platform.png` | 64×16 | Brown (#8B6914) | Floating platform tile. Replace with a textured platform (wood, stone, etc.). Tile horizontally for wider platforms. |
| `ground.png` | 64×32 | Green (#5B8C3E) | Ground/floor tile. Replace with grass-topped earth texture. Tile horizontally for the ground strip. |

### Backgrounds (`backgrounds/`)

| File | Size | Color | Purpose |
|------|------|-------|---------|
| `sky.png` | 256×256 | Sky blue (#87CEEB) | Far parallax layer (slowest scroll). Replace with a gradient sky or painted clouds. Tiles horizontally. |
| `mountains.png` | 256×256 | Slate gray (#778899) | Mid parallax layer (medium scroll). Replace with mountain silhouettes or forest treeline. Tiles horizontally. |

## Replacement Guidance

### Recommended Formats
- **PNG** with transparency (RGBA) for sprites and tiles
- **PNG** without transparency (RGB) for backgrounds
- Raylib supports PNG, BMP, TGA, JPG, and GIF

### Recommended Dimensions
- **Sprites**: Power-of-2 widths preferred (32, 64, 128). Height can vary.
- **Tiles**: 32×32 or 64×64 for grid-based levels. Must tile seamlessly on the horizontal axis.
- **Backgrounds**: At least 1280px wide (window width) for seamless scrolling. Height should match or exceed 720px.

### Sprite Sheets
For animated characters, use a horizontal strip layout:
- Each frame is the same width/height
- Frames are arranged left-to-right
- Update the game code to specify frame count and animation speed

### Regenerating Placeholders
Run the generator script to recreate all placeholder assets:
```bash
python assets/gen_assets.py
```
