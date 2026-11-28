# drawing-rule-batching Specification

## Purpose

Define the cost promise of drawing rules: the rows of one rule are drawn in batches whose number does not grow with the row count, with the same picture as drawing them one by one, and with counters that let tests check it.

## Requirements

### Requirement: Rows of a drawing rule are drawn in batches
In one viewport, a drawing rule SHALL issue at most one draw call for each run of consecutive drawn rows, in the rule's draw order, that sample the same textures. A rule that samples no texture, or one texture for all rows, SHALL issue one draw call however many rows it has. A rule with no drawn rows SHALL issue none.

#### Scenario: Many rows, no texture
- **WHEN** a drawing rule with no `sample` call has 10,000 rows
- **THEN** it issues one draw call per viewport

#### Scenario: Two textures in two runs
- **WHEN** a rule's rows in draw order use texture A for the first half and texture B for the second
- **THEN** it issues two draw calls per viewport

#### Scenario: Rows filtered by where
- **WHEN** a rule visits 10,000 rows and its `where:` passes 5,000 of them
- **THEN** it draws 5,000 quads in one draw call

#### Scenario: Empty domain
- **WHEN** a drawing rule has no rows
- **THEN** it issues no draw call

### Requirement: Batching does not change the picture
The picture a batched drawing rule produces SHALL equal the picture of drawing its rows one at a time in the rule's draw order. Each row SHALL be drawn with its own values, and an `int`, `bool` or enum row value SHALL reach the stage exactly.

#### Scenario: Overlap order
- **WHEN** two covering quads of one rule overlap and the rule declares `order by: depth asc`
- **THEN** the quad with the higher `depth` covers the other, batched or not

#### Scenario: Row values
- **WHEN** rows of one batch have different positions, colors and ages
- **THEN** each quad shows its own row's values

#### Scenario: Large integer row value
- **WHEN** a row's `int` field is 16,777,217 and the fragment stage compares it with that constant
- **THEN** the comparison is true

### Requirement: A drawing rule reads a bounded number of row values
A drawing rule SHALL read at most 14 distinct row values across its two stages. A row value is a field of the row or a value reached through a link of the row; named entities, constants and textures do not count. A rule that reads more SHALL be a compile error that names the rule, the count and the limit.

#### Scenario: Within the limit
- **WHEN** a drawing rule reads 14 distinct row values
- **THEN** the program compiles

#### Scenario: Over the limit
- **WHEN** a drawing rule reads 15 distinct row values
- **THEN** compilation fails with an error that names the rule, 15 and the limit 14

### Requirement: Drawing-rule counters
In test builds the perf counters SHALL count the draw calls of drawing rules beside their rows, quads, uniforms and texture binds. The render debug state SHALL report, for each drawing rule that ran in the last frame, the rows drawn and the draw calls issued, summed over viewports.

#### Scenario: Counters after a frame
- **WHEN** a frame draws a rule with 500 rows in one run in one viewport
- **THEN** the debug state reports 500 rows and 1 draw call for that rule
