## ADDED Requirements

### Requirement: Assets directory with placeholder files
The `examples/platformer/assets/` directory SHALL contain placeholder art assets sufficient for the platformer example to render visually.

#### Scenario: Assets directory exists
- **WHEN** the `examples/platformer/assets/` directory is listed
- **THEN** it SHALL contain subdirectories for `sprites/`, `tiles/`, and `backgrounds/`

#### Scenario: Player sprite placeholder
- **WHEN** `assets/sprites/` is listed
- **THEN** it SHALL contain a `player.png` placeholder image (a simple colored rectangle or silhouette, minimum 32x32 pixels)

#### Scenario: Enemy sprite placeholder
- **WHEN** `assets/sprites/` is listed
- **THEN** it SHALL contain an `enemy.png` placeholder image

#### Scenario: Collectible sprite placeholder
- **WHEN** `assets/sprites/` is listed
- **THEN** it SHALL contain a `gem.png` placeholder image

### Requirement: Tileset placeholders
The `assets/tiles/` directory SHALL contain placeholder tile images for platforms and ground.

#### Scenario: Platform tile exists
- **WHEN** `assets/tiles/` is listed
- **THEN** it SHALL contain `platform.png` (a simple rectangular tile, e.g., 64x16 or 32x32)

#### Scenario: Ground tile exists
- **WHEN** `assets/tiles/` is listed
- **THEN** it SHALL contain `ground.png` (a ground/floor tile)

### Requirement: Parallax background layers
The `assets/backgrounds/` directory SHALL contain at least two background layer images for parallax scrolling.

#### Scenario: Background layers exist
- **WHEN** `assets/backgrounds/` is listed
- **THEN** it SHALL contain at least `sky.png` (far layer) and `mountains.png` (mid layer)

#### Scenario: Background images are tileable width
- **WHEN** any background PNG is inspected
- **THEN** it SHALL be at least 256 pixels wide to support horizontal tiling

### Requirement: Assets README documentation
An `assets/README.md` SHALL document all placeholder assets, their purpose, and intended replacements.

#### Scenario: README lists all assets
- **WHEN** `assets/README.md` is read
- **THEN** it SHALL list every asset file with its purpose (e.g., "player.png — placeholder player character sprite")

#### Scenario: README describes replacement guidance
- **WHEN** `assets/README.md` is read
- **THEN** it SHALL include guidance on recommended dimensions, formats, and how to replace placeholders with production art
