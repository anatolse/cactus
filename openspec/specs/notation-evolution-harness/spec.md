## Purpose

Define the evidence-isolation, resumability, judging, and reporting guarantees
required before results from the Cactus notation-evolution harness are admitted
as experiment findings.

## Requirements

### Requirement: Candidate evidence is isolated

The harness SHALL store each kata's notation-independent contract separately
from every candidate expression. Token measurement, residue extraction, gate
evaluation, and blind comprehension SHALL receive candidate expressions without
their contracts, titles, stress labels, source ranges, or expected answers.

The comprehension grader MAY receive the contract after the blind reader has
finished, but the reader SHALL NOT receive it. The ancestor and every challenger
SHALL be measured from the same artifact shape.

#### Scenario: Ancestor enters blind comprehension

- **WHEN** the ancestor is evaluated by the blind comprehension reader
- **THEN** the reader receives only the notation reference and the selected
  ancestor expressions
- **THEN** no selected kata contract or expected answer is present in that input

#### Scenario: Candidate token totals are compared

- **WHEN** the ancestor and a challenger receive kata token totals
- **THEN** both totals are computed only from their expression text
- **THEN** headings, contracts, provenance, and source-range notes contribute no
  tokens to either total

### Requirement: Structured measurements are complete and validated

Every structured measurement SHALL report each requested identity exactly once.
Missing, duplicate, unknown, or malformed rows SHALL invalidate that measurement
rather than improve or partially score the candidate.

Residue occurrences SHALL identify a known pattern and kata, SHALL quote an exact
substring of that kata's expression, and SHALL be deduplicated by canonical
pattern, kata, and source span before counting. Accepted occurrence sets and
their arithmetic totals SHALL be persisted so rescoring unchanged evidence
produces the same count without another extraction call.

#### Scenario: Comprehension grader omits a sample

- **WHEN** the grader returns fewer unique rows than the requested comprehension
  sample
- **THEN** the comprehension measurement is invalid
- **THEN** the candidate does not receive a score calculated over the smaller
  denominator

#### Scenario: Residue quote is not in the expression

- **WHEN** an extracted residue occurrence quotes text that is not an exact
  substring of its named kata expression
- **THEN** the occurrence is rejected and the residue measurement is invalidated
  with an actionable diagnostic

#### Scenario: Unchanged residue evidence is rescored

- **WHEN** the same accepted occurrence set is scored more than once
- **THEN** every score uses the persisted canonical set and produces the same
  per-pattern and total counts

### Requirement: UI scoring cannot reward missing work

The mechanical fenced-code line count SHALL be the authoritative UI line count.
Model-supplied counting MAY be recorded as audit evidence but SHALL NOT replace
the mechanical value in ranking.

A candidate that declares any load-bearing part of measure or arrange
inexpressible SHALL receive no line-count credit and SHALL NOT be credited with
zero manual walks, dispatch flags, or reductions merely because the work is
absent. Externalized work SHALL remain subject to the extern escape discount.

#### Scenario: Candidate cannot express layout

- **WHEN** a candidate's UI artifact says that a load-bearing part of measure or
  arrange cannot be expressed
- **THEN** the candidate receives no terseness or structural-simplicity credit
  for the missing implementation

#### Scenario: Model and mechanical line counts differ

- **WHEN** a model-supplied UI line count differs from the fenced-code line count
- **THEN** ranking uses the fenced-code count
- **THEN** the disagreement is recorded for audit

### Requirement: Checkpoints are compatible and complete

Every checkpoint SHALL identify the seed, genotype, run mode, generation,
client family, model, prompt set, output schemas, scoring rules, and judge
configuration that produced it. Resume SHALL reuse a stage only when every
identity field relevant to that stage matches the requested run.

Dry-run output SHALL never be reusable as real evidence. A finished checkpoint
SHALL contain structurally valid generated artifacts and all required completed
measurements. Incompatible checkpoints SHALL be rejected with their mismatch
reported; they SHALL NOT be silently migrated or adopted.

Every model prompt, raw response, validated structured result, partial score,
duel presentation order, duel decision, and duel reason SHALL be durably written
before dependent work begins. Checkpoint writes SHALL not expose credentials.

#### Scenario: Real run resumes a dry run

- **WHEN** a real run points `--resume` at checkpoints produced with `--dry-run`
- **THEN** the harness rejects those checkpoints as incompatible
- **THEN** no dry-run expression, score, or duel enters the real ranking

#### Scenario: Scoring stops after residue extraction

- **WHEN** quota exhaustion interrupts a candidate after its validated residue
  result was persisted
- **THEN** resume reuses the generated artifacts and residue result
- **THEN** only unfinished stages are called again

#### Scenario: Scoring rules change

- **WHEN** a checkpoint was produced by a different scoring-rule version
- **THEN** resume does not treat its final score as current
- **THEN** reusable upstream evidence is retained only where its own identity
  fields still match

### Requirement: Quota exhaustion stops all model work safely

Quota and rate-limit exhaustion from any configured model client or any pipeline
stage, including tournaments, SHALL stop admission of new model calls, cancel
work that has not started, persist completed evidence, and exit non-zero with a
resume instruction. Ordinary candidate-specific invalid output SHALL remain a
candidate error rather than aborting the population.

#### Scenario: Judge quota ends during a tournament

- **WHEN** either judge client reports quota exhaustion during a duel
- **THEN** no new duels are started after the exhaustion is observed
- **THEN** completed duel records remain reusable by a compatible resume
- **THEN** the run exits non-zero without publishing a partial tournament rating

#### Scenario: One candidate returns invalid structured output

- **WHEN** a candidate-specific scoring call returns invalid output without a
  quota or rate-limit error
- **THEN** that candidate records an actionable error
- **THEN** other candidates may continue

### Requirement: Generation zero is balanced and reproducible

A twelve-individual generation zero SHALL contain exactly two representatives of
each registered super-gene. The unmodified ancestor SHALL occupy one of the two
ancestor-super-gene positions. A fixed random seed SHALL reproduce the same
genomes and identifiers.

Super-gene mutation SHALL select a different registered super-gene. The harness
SHALL NOT claim that mechanical mutation can invent an unregistered paradigm;
adding a new paradigm SHALL require registering a coherent super-gene allele
before a run.

#### Scenario: Twelve individuals are seeded

- **WHEN** generation zero is created with population twelve
- **THEN** every registered super-gene appears exactly twice
- **THEN** one and only one individual is the unmodified ancestor

#### Scenario: Super-gene mutation fires

- **WHEN** super-gene mutation is selected for an individual
- **THEN** the result uses a different registered coherent super-gene
- **THEN** no undeclared super-gene identifier is produced

### Requirement: Genotypes remain auditable through development

Every developed notation SHALL account for every inherited locus. Any override,
repair, omission, or merge SHALL be returned as structured data naming the
affected loci and SHALL contribute to the repair measure. Missing repair data or
an unaccounted locus SHALL invalidate development rather than earn zero repair
cost.

#### Scenario: Developer omits the conflict report

- **WHEN** a generated notation has no valid structured account of inherited
  loci and repairs
- **THEN** the candidate is invalidated before gate or ranking work begins

#### Scenario: One repair resolves several loci

- **WHEN** a repair changes the realization of several inherited loci
- **THEN** every affected locus is named in the repair record
- **THEN** the repair measure reflects every affected locus rather than the
  formatting or number of prose bullets

### Requirement: Judge families are independent and auditable

An admissible paid pilot SHALL include at least one Claude-family judge and one
Codex-family judge. Both families SHALL receive the same blinded candidate pair,
axis, and randomized presentation order. Each family response and reason SHALL
be stored separately.

Ratings SHALL be fitted from the complete admitted batch with a deterministic
Bradley-Terry procedure or an equivalently order-independent batch method;
sequential online Elo update order SHALL NOT affect the result. The report SHALL
include family-specific ratings, combined ratings, agreement rate, comparison
counts, and incomplete-pair diagnostics.

#### Scenario: Only one judge family is available

- **WHEN** a paid pilot is requested without both required judge families passing
  preflight
- **THEN** the harness stops before candidate development spends model budget

#### Scenario: Judges disagree

- **WHEN** Claude and Codex choose different winners for the same blinded duel
- **THEN** both outcomes remain in the record
- **THEN** the disagreement contributes to the reported agreement rate

#### Scenario: Duel processing order changes

- **WHEN** the same complete set of admitted duel outcomes is fitted in a
  different input order
- **THEN** the resulting ratings and ranking are unchanged within the declared
  numerical tolerance

### Requirement: Model access uses locally authenticated clients

The harness SHALL access Claude only through the locally authenticated Claude
Code/Agent SDK transport and Codex only through `codex exec`. It SHALL NOT expose
a direct Anthropic Messages API backend, read `ANTHROPIC_API_KEY` or
`ANTHROPIC_AUTH_TOKEN`, or require the `anthropic` Python package. Model options
SHALL identify the explicit Claude and Codex models without selecting an API-key
transport.

#### Scenario: Anthropic API key is present

- **WHEN** the operator environment contains `ANTHROPIC_API_KEY`
- **THEN** the harness does not read or use that value
- **THEN** Claude calls still use the authenticated local Claude Code session

#### Scenario: Operator requests the removed API backend

- **WHEN** the operator passes the former `--backend api` option
- **THEN** argument parsing rejects the unsupported option before any model call

### Requirement: Allele reports state what the data supports

Allele reporting SHALL control for super-gene and generation when comparing
scores and SHALL report sample count, distinct super-gene count, distinct
generation count, the estimator used, and uncertainty or an explicit
insufficient-evidence result.

An unadjusted mean score among carriers SHALL be labeled as a descriptive
association and SHALL NOT be called a marginal contribution. A causal or
marginal-effect label SHALL be used only for a design and estimator that identify
that effect independently of linked loci and selection history.

#### Scenario: Allele appears in one super-gene only

- **WHEN** a free-locus allele lacks the cross-super-gene support required by the
  configured estimator
- **THEN** the report marks its adjusted effect as insufficient evidence
- **THEN** it does not place that effect in the actionable ranked queue

#### Scenario: Descriptive carrier mean is emitted

- **WHEN** the report includes the raw mean score of candidates carrying an
  allele
- **THEN** the field and report label it as descriptive association
- **THEN** adjusted evidence and sample coverage are presented separately

### Requirement: Capacity remains a scored dimension

The hard semantic contract for timed enemy spawning and destruction SHALL cover
the observable spawn and removal behavior without requiring a declared live
entity bound. Capacity information SHALL remain scored but non-disqualifying in
the information-sufficiency assessment.

#### Scenario: Candidate has unbounded spawn capacity

- **WHEN** a candidate expresses the timed spawning and destruction behavior but
  supplies no declared live entity bound
- **THEN** that kata remains expressible
- **THEN** the candidate receives no capacity-information credit

### Requirement: Paid evidence requires a current calibration

Dry-run rankings and results produced before this capability's checkpoint and
measurement contracts SHALL be marked non-comparative. Before a paid generation
zero may start, the harness SHALL pass its unit and offline end-to-end tests and
produce a real ancestor calibration with the current seed, prompts, schemas,
scoring rules, clients, and model configuration.

The harness SHALL expose `preflight` and `calibrate` as separate commands.
Preflight SHALL make one minimal structured call to each required judge family,
using a fixed blinded canary duel, and SHALL validate executable availability,
client version, local authentication, explicit model access, quota, and schema
support. It SHALL persist no credential and SHALL perform no candidate
development or scoring.

Calibration SHALL require a compatible successful preflight and SHALL evaluate
only the repository-derived ancestor through every non-tournament scoring stage.
It SHALL NOT develop a notation, create challengers, mutate, breed, or run a
tournament. A real generation-zero run SHALL require compatible successful
preflight and calibration records; a dry run SHALL NOT create either record.

#### Scenario: Preflight succeeds

- **WHEN** both locally authenticated model families return valid structured
  results for the fixed canary duel
- **THEN** the harness writes their credential-free client, model, schema, usage,
  and result identities to the qualification record
- **THEN** no candidate artifact has been generated or scored

#### Scenario: Preflight fails

- **WHEN** an executable, login, model, quota, or structured schema is unavailable
- **THEN** preflight exits non-zero with an actionable diagnostic
- **THEN** calibration and a paid run reject that qualification record

#### Scenario: Calibration is requested

- **WHEN** a compatible preflight exists and the operator runs `calibrate`
- **THEN** only the current ancestor enters the gate and measurement pipeline
- **THEN** no development, challenger, mutation, breeding, or tournament call is
  made

#### Scenario: Paid pilot lacks qualification

- **WHEN** a real generation-zero run has no compatible successful preflight or
  ancestor calibration record
- **THEN** it stops before candidate development spends model quota

#### Scenario: Existing pilot directory is inspected

- **WHEN** an existing directory contains dry-run or incompatible checkpoints
- **THEN** the harness identifies it as non-comparative
- **THEN** it cannot be used to satisfy the real pilot or calibration gate

#### Scenario: Current ancestor calibration completes

- **WHEN** the real ancestor finishes every required gate and scoring stage under
  the current experiment identity
- **THEN** its artifacts and measurements are checkpointed as the calibration
  reference
- **THEN** the paid pilot may proceed only if both judge-family preflights also
  pass
