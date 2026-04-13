# x6 Text Support Roadmap (dwm-compatible)

## Goal
Provide stable, real glyph text rendering for dwm on x6 using Montecarlo first, then extend toward broader X11 behavior over time.

## Current Interim State
- Userspace Montecarlo font module exists (`user/user_font.c`, `include/graphics/user_font.h`).
- x6 supports `DRAW_TEXT` and renders 1bpp glyphs to framebuffer.
- x11 shim exposes `XDrawString` by forwarding to `DRAW_TEXT`.
- aux drw backend uses Montecarlo metrics and draw calls for bar/title text.

## Phase 1 (Land + stabilize)
1. Validate bar/title text readability under real workloads.
2. Confirm layout math parity (`drw_fontset_getwidth`, `drw_font_getexts`) for tags and titles.
3. Add clipping in x6 text draw for drawable/window bounds.
4. Add lightweight counters/logging to track text draw frequency and failures.

## Phase 2 (Protocol hardening)
1. Add `MEASURE_TEXT` command to x6 protocol so width comes from server-side metrics.
2. Keep `DRAW_TEXT` and `MEASURE_TEXT` semantics ASCII-first, deterministic, and fast.
3. Version-gate text features via `HELLO` response capability flags.

## Phase 3 (Font selection + style)
1. Add `SET_FONT` command (name + size) scoped per client or GC.
2. Keep Montecarlo default and add fallback list support.
3. Support bold/underline style bits as optional draw flags.

## Phase 4 (UTF-8 and fallbacks)
1. Decode UTF-8 in x11 shim and x6 path consistently.
2. Add missing-glyph fallback and width-correct substitution.
3. Ensure no regressions in dwm truncation and ellipsis behavior.

## Phase 5 (Performance)
1. Glyph cache in x6 for hot codepoints.
2. Batched row writes for text spans to reduce per-pixel I/O.
3. Optional dirty-region compositor path for cursor + text overlay updates.

## Non-goals for now
- Full Xft/fontconfig parity.
- Advanced shaping/ligatures/complex scripts.
- Full upstream Xlib font object semantics.

## Acceptance Criteria
- dwm bar text fully readable with Montecarlo.
- tag/title widths are stable and not jittering.
- cursor + text interaction remains responsive.
- no startup regressions (`x6 did not become ready`) and no input regressions.
