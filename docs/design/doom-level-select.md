# KEUR — DOOM "Choose your Difficulty" level select (design + proven pipeline)

Status: pipeline PROVEN (2026-09-25), C++ TUI NOT yet built. This captures the
verified approach so it can be implemented without re-discovery.

## What we're building

A `keur` skill-level chooser modelled on DOOM's "Choose Skill Level" screen:

```text
        <KEUR logo in coloured braille, scaled to terminal width>

                    Choose your Difficulty:

              I'm Too Young to KEUR
            > Hey, Not Too Sure          <- selected row (arrow keys)
              KEUR Me Plenty
              Ultra Compliance
              H4x0r c0d3fl3x

     Level 2 — Managed.
     A practical baseline of checks for repeatable development.
```

- Logo on top (coloured braille, DOOM look), levels below, arrow-key select,
  a marker on the selected row + a description block under it.
- Maps DOOM's 5 skills onto 5 CMMI maturity levels (ties into ADR-026 rule
  maturity). Names (final, from the user):
  1. I'm Too Young to KEUR — Level 1 Initial (essential checks only)
  2. Hey, Not Too Sure — Level 2 Managed (practical baseline)
  3. KEUR Me Plenty — Level 3 Defined (standard KEUR experience)
  4. Ultra Compliance — Level 4 Quantitatively Managed (stricter + measured)
  5. H4x0r c0d3fl3x — Level 5 Optimizing (maximum guardrails)

## Proven conversion pipeline (image -> coloured braille)

Reproduction of rkristelijn/braille-ascii (its React canvas algorithm), headless
via ffmpeg. Script: `.tmp/img2braille.js` (works, tested). Algorithm:

- Braille 2x4 dot cell -> one U+28xx glyph; dot bit order
  `[[0,0],[0,1],[0,2],[1,0],[1,1],[1,2],[0,3],[1,3]]`, bit i = 2^i added to 0x2800.
- A dot is ON when the source pixel passes the brightness threshold
  (`--invert` because the logo is light-on-dark: ON = brighter than threshold).
- Colour = average RGB of the cell's ON dots -> ANSI 256 via rgbToAnsi
  (grayscale ramp 232..255, else 6x6x6 cube 16+36r+6g+b). Emitted as
  `\x1b[38;5;{n}m{glyph}` per char, `\x1b[0m` at line end.

Source image: `keur-logo-chatgpt.png` (1672x941, black background — best of the
three; black bg makes the letters the bright ON pixels).

Raw pipeline (no browser, no canvas npm dep):

```bash
W=360
ffmpeg -y -loglevel error -i keur-logo-chatgpt.png -vf "scale=${W}:-1" \
       -pix_fmt rgba -f rawvideo logo.rgba
H=$(( $(stat -f%z logo.rgba) / (W*4) ))
node .tmp/img2braille.js logo.rgba $W $H --cols <N> --invert --threshold <T> --color
```

### Responsive scaling (sm/md/lg/xl by terminal width)

Terminal cols (from `tput cols` / COLUMNS) pick a bucket; each bucket is a
pre-baked asset at a different braille width. VERIFIED look:

| bucket | terminal cols | logo braille cols | threshold | note |
|--------|---------------|-------------------|-----------|------|
| sm     | <= 60         | 48                | ~120      | solid filled letters — very legible small |
| md     | 61..100       | 72                | ~135      | filled, the default |
| lg     | 101..140      | 96                | ~140      | |
| xl     | > 140         | 128               | ~150      | |

KEY finding: LOWER threshold = solid filled glyphs = far more legible than a
high-threshold thin outline (which falls apart at small sizes). Prefer filled.
`--cols` now maps 1:1 to output width (bug fixed: bw = cols exactly).

Trim leading/trailing blank braille rows (ANSI-aware) after generating:

```bash
awk 'BEGIN{s=0}{ln[NR]=$0;g=$0;gsub(/\033\[[0-9;]*m/,"",g);
  if(g ~ /[^ ⠀]/){if(!s){f=NR;s=1} l=NR}} END{for(i=f;i<=l;i++)print ln[i]}'
```

## How it fits keur's architecture (ADR-002: one binary, no runtime image lib)

Generate the braille assets OFFLINE (this pipeline), then BAKE them into the C++
binary as static strings — exactly like `manager/catalog_generated.cpp` bakes the
rule catalogue. A `scripts/gen-logo.sh` runs ffmpeg+node and writes
`src/keur/render/logo_generated.cpp` with the 4 buckets x {mono,color}. The gate
never shells out to ffmpeg/node at runtime.

## C++ implementation plan (NOT yet built)

1. `scripts/gen-logo.sh` — the pipeline above for the 4 buckets, writes
   `render/logo_generated.{h,cpp}`: `const char* keur_logo(Bucket, bool color)`.
   Add a Makefile rule like the `catalog` target (regenerated, git-ignored or
   committed — match catalog's choice).
2. `render/logo.{h,cpp}` — `Bucket bucket_for_width(int cols)` (pure, testable);
   picks sm/md/lg/xl from the table above.
3. `cmd/level.{h,cpp}` — `keur level` subcommand:
   - detect terminal width (COLUMNS env or platform ioctl in adapters/, keep I/O
     at the edge — ADR-003), pick bucket, print logo.
   - render the 5 levels; arrow keys (reuse `common/keyinput`) move a `>` marker;
     Enter selects; print the level's CMMI description under it.
   - on select: persist the chosen level to keur.toml `[policy] level = N` (or a
     dedicated key) so the gate's strictness follows it — the ADR-026 tie-in.
4. Difficulty -> gate strictness mapping (ADR-026): level N scales block-at /
   which categories block. e.g. L1 blocks only high-confidence secrets; L5 blocks
   everything at a low threshold. Wire through `policy_evaluate` inputs, NOT a
   second code path (ADR-014).
5. Tests: `bucket_for_width` table test; `keur level --print <N>` non-interactive
   mode renders a bucket deterministically (e2e snapshot); level->threshold map.

## Title + level text: render as TEXT, not braille

The "Choose your Difficulty" title and the 5 level names render as coloured
TEXT (red, like the DOOM menu), NOT as braille — braille-of-text is unreadable at
terminal width. Only the LOGO is braille. The user's 282b...png (red DOOM-font
menu) is the visual reference for colour/attitude, not a braille source.

## Assets / scratch

- `.tmp/img2braille.js` — the converter (keep; it is the generator's core).
- `keur-logo-chatgpt.png` — chosen logo source (black bg).
- Sibling repo: `~/git/hub/braille-ascii` (rkristelijn/braille-ascii) — original.
