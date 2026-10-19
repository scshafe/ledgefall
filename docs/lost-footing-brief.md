<!-- Copied from scshafe/lost-footing docs/brief.md on 2026-10-18: the rules ledgefall keeps (see docs/brief.md for what changes). -->

# Lost Footing: the owner's brief

The owner's request, 2026-10-15 (lightly edited for reading):

> A game where multiple players compete in a fighter (like Super Smash Bros)
> on a stage map, except they can't directly hit one another. Instead, the
> stage is entirely made up of those Mario Bros sort of platforms that let you
> stand on them, but if you don't move quickly, they begin to fall (and then
> quickly respawn). Characters' moves should be based around placing objects
> that interfere with mobility, and potentially destroying platforms (to
> eliminate safe spots). There could also be sturdy platforms, but something
> would need to make them intermittently dangerous (like objects flying
> across). Sturdy platforms can still be destroyed (not just by standing on
> them). We should consider letting characters interact: moving into each
> other along a surface should not interfere, but contact from a vertical
> direction should. All the moving is done with keys.
>
> The core theme is a **survival fighter, through indirect means**. The
> dynamic should inspire and reward creative gameplay.

## What this means for the design

These are the fixed points; everything else is the designer's to decide and
record in `docs/design.md`.

- **No direct damage.** No move hits, damages or knocks back another player
  directly. A player is eliminated only by the stage: falling off the bottom
  (or another blast zone) or a stage hazard. Every way of hurting an opponent
  is indirect: change the ground under them, block their path, or steer a
  hazard.
- **Crumbling platforms are the stage.** Most of the stage is platforms that
  hold you briefly: standing still starts them shaking, then falling; they
  respawn quickly. Staying alive means staying in motion.
- **Moves place things and break things.** Each character's kit is built from
  placing objects that interfere with mobility (walls, slick patches, sticky
  patches, springs, and so on) and from destroying or weakening platforms to
  remove safe spots.
- **Sturdy platforms exist but are never safe for long.** Intermittent
  hazards (objects flying across, for example) make them dangerous on a
  rhythm, and they can be destroyed by moves, not only by standing on them.
- **Body contact is vertical only.** Players pass through each other
  side to side on a surface; contact from above or below matters (landing on
  someone, bumping up into them).
- **Keyboard only.** Several players share one keyboard, each with their own
  key cluster; bots can fill empty seats.
- **Creativity is rewarded.** Combinations of placed objects, platform timing
  and hazards should produce plays the designer did not script.
