# Notation evolution operator runbook

Run every command from the repository root. Claude uses the locally signed-in
Claude Code/Agent SDK session. Codex judging uses the locally signed-in `codex`
CLI. The harness does not use API keys.

## 1. Build a fresh versioned seed

Use a new directory. Do not overwrite an earlier experiment seed.

```powershell
python tools/evolve_notation.py scaffold-seed --out experiments/notation-v2-seed
```

## 2. Test and rehearse offline

```powershell
python tools/test_evolve_notation.py
python tools/evolve_notation.py run --seed experiments/notation-v2-seed --out experiments/notation-v2-offline --generations 0 --population 12 --workers 4 --seed-rng 1 --dry-run
python tools/evolve_notation.py run --seed experiments/notation-v2-seed --out experiments/notation-v2-offline --generations 0 --population 12 --workers 4 --seed-rng 1 --dry-run --resume
```

The dry run exercises generation, validation, stage checkpoints, both
deterministic mock judge families, batch fitting, reporting, and resume. It does
not call a model and cannot create valid qualification evidence.

## 3. Preflight the two local model clients

This makes one small structured canary call to each judge family. It checks the
local executables, client versions, login, model access, quota, and structured
output. It does not load a seed or develop or score a candidate.

```powershell
python tools/evolve_notation.py preflight --out experiments/notation-v2-preflight.json --claude-model claude-opus-5 --codex-model gpt-5.6-sol
```

Inspect `experiments/notation-v2-preflight.json`. Both clients must be present
and `status` must be `success`.

## 4. Calibrate the ancestor

This is the first full scoring spend. It measures only the repository-derived
ancestor through every non-tournament stage. It does not develop a challenger,
mutate, breed, or run duels.

```powershell
python tools/evolve_notation.py calibrate --seed experiments/notation-v2-seed --preflight experiments/notation-v2-preflight.json --out experiments/notation-v2-calibration --claude-model claude-opus-5 --codex-model gpt-5.6-sol
```

Inspect `experiments/notation-v2-calibration/calibration.json`. Continue only
when `status` is `success` and the recorded seed, clients, models, prompts,
schemas, scoring, and judge configuration are current.

## 5. Run paid generation zero

Do this only after explicitly approving the model spend.

```powershell
python tools/evolve_notation.py run --seed experiments/notation-v2-seed --out experiments/notation-v2-pilot --generations 0 --population 12 --workers 4 --seed-rng 1 --claude-model claude-opus-5 --codex-model gpt-5.6-sol --preflight experiments/notation-v2-preflight.json --calibration experiments/notation-v2-calibration/calibration.json
```

If quota stops the run, repeat the byte-for-byte configuration with `--resume`.
Completed compatible stages and duels are reused.

## 6. Stop and inspect

```powershell
Get-Content -Raw experiments/notation-v2-pilot/run-manifest.json
Get-Content -Raw experiments/notation-v2-pilot/gen00/tournament/tournament-report.json
Get-Content -Raw experiments/notation-v2-pilot/gen00/ranking.json
Get-Content -Raw experiments/notation-v2-pilot/allele_associations.json
```

Do not continue if the inspector reports legacy, dry-run, stale, incompatible,
or incomplete evidence. Review the family agreement and missing-record fields
before authorizing later generations.

## 7. Resume through later generations

Keep the seed, output directory, population, worker count, random seed, model
ids, preflight, and calibration identical. Only the terminal generation may
increase.

```powershell
python tools/evolve_notation.py run --seed experiments/notation-v2-seed --out experiments/notation-v2-pilot --generations 5 --population 12 --workers 4 --seed-rng 1 --claude-model claude-opus-5 --codex-model gpt-5.6-sol --preflight experiments/notation-v2-preflight.json --calibration experiments/notation-v2-calibration/calibration.json --resume
```
