# GUxSID Human Graphics ver2 architecture

ver2 keeps the current visual behavior while separating the runtime into five
stages. Each stage receives plain data and can be replaced without changing the
stages before it.

```text
PersonSegmenter (Detection)
  -> ObjectTracker
  -> GeometryProcessor
       -> VertexRemapper
       -> OffsetProcessor
  -> SceneComposer
       -> merge / grouping
       -> SceneObject
  -> MergeEvent
       -> StandardMergeEvent
  -> SceneLayout
  -> SceneBehavior
  -> RenderRecipe
       -> BasePass
       -> StrokePass
       -> ShapePainter
```

Each `SceneObject` also owns an `AppearanceComponent` with independent Base
and Outline Material slots. `Solid` and `LinearGradient` are currently
available; materials are assigned from the palette without repeating their
primary color on the same screen.

## Ownership

- `core/`: data and enums shared across stages.
- `tracking/`: assigns stable IDs to detection results.
- `geometry/`: performs per-object vertex remapping and optional offset.
- `scene/`: owns drawable objects, transforms, instances, and composition.
- `event/`: reacts after composition has produced a real merge. The default
  `standard_event` leaves the composed SceneObjects unchanged.
- `scene/SceneLayout`: performs deterministic placement such as recursive
  screen subdivision without changing detection data.
- `scene/SceneBehavior`: updates time-based transforms such as floating motion
  and loosely coupled scale.
- `render/`: turns SceneObjects into pixels. It does not run detection or edit
  source geometry.
- `HumanGraphicsScene.*`: connects the stages and translates existing GUI settings into
  stage-specific settings.

## Extension points

- Add a visual style by implementing `RenderRecipe`.
- Add partial strokes or decorations through reusable material and painter operations
  and dedicated RenderPass classes.
- Move or clone a detected object through `SceneObject::transform` and
  `SceneObject::clone()` without changing detection data.
- Replace `ObjectTracker` with a more advanced tracker while keeping ObjectId.
- MIDI and network synchronization should target SceneObject commands and
  parameter IDs, not call RenderPass classes directly.

## Compatibility contract

`StandardRenderRecipe` (`standard_render`) is the reference Recipe and
reproduces the previous Base-then-Stroke drawing. The
existing offset algorithm, contact merge behavior, inset-line preservation,
palette assignment, GUI values, and video export flow are retained.

`MixRenderRecipe` (`mix_render`) draws a palette-colored solid Base first,
then overlays a solid Stroke using the current background color. Presets select
the active Recipe through `Recipe_ID`.

`floating_bridge.json` uses `floating_bridge_render` with the static
`standard_behavior`. Objects are drawn like `standard_render`, following the
normal Base / Stroke / Offset / Merge toggles. Behind them, one open stroke
connects the centers of all objects on screen, ordered left to right by X
(bottom first when X is equal), so the line never moves back to the left.

## Paper shadow

The graphics are treated as sheets of colored paper lifted slightly off the
background. `render/PaperShadowPass` runs before every Recipe: it draws the
same Recipe once into a half-resolution FBO, blurs its alpha, and lays two
shadows under the paper. A tight contact shadow gives the sheet an edge, and a
wide soft ambient shadow gives it height. Lengths scale with canvas height, so
preview and export match. Shadow alpha is composited separately so transparent
export keeps it. GUI: `Paper Shadow`, `Paper Shadow Lift`, `Paper Shadow Angle`
(0 = right, 90 = down), `Paper Shadow Opacity`.

## Background gradient

`render/BackgroundGradientPass` keeps the background from being a flat fill.
Over the background color it draws four soft, blurred ellipses of off-white
(about 230–240 on a 255 background) that drift slowly, like a mesh gradient.
Overlaps are combined so they never get much darker than one ellipse, and
sub-step noise prevents banding. Drift uses the Scene's elapsed time, so video
export reproduces it; transparent export leaves the background out. GUI:
`Background Gradient`, `Background Gradient Depth`, `Background Gradient Speed`.

ver2 stores its GUI settings and crash marker under
`Application Support/GUxSID_Human_Graphics_ver2`, independently from the
original project.

## Sound OSC output

`sound/SoundOscSender` sends per-person data over OSC (UDP, `127.0.0.1`,
port 7400, fixed by `ofApp::kSoundOscPort`) so an external sound engine (TouchDesigner)
can react. It is header-only and needs no addon. Positions and size are
normalized to 0–1 of the canvas; speed is canvas units per second.

| Address | Arguments | When |
|---|---|---|
| `/gux/slot/0`…`/gux/slot/15` | active x y size speed group | every frame, all 16 slots (for OSC In CHOP) |
| `/gux/enter` | id slot x y size | a person appears |
| `/gux/exit` | id slot | a person has been missing for 0.4 s |
| `/gux/person` | id x y size speed group | every frame, per person |
| `/gux/count` | people groups | every frame |
| `/gux/merge` | idA idB x y | two people stay in one merged shape for 0.15 s |
| `/gux/split` | idA idB x y | a merged pair stays apart for 0.15 s |
| `/gux/clear` | — | sending starts or stops |
| `/gux/note/0`…`/gux/note/7` | active level motion x y id | every frame, all 8 notes (ド〜ド, C4–C5) |
| `/gux/noteon` | note midi id | a note is assigned to a person |
| `/gux/noteoff` | note midi id | that person left |

Each person gets the lowest free note of the C major scale (ド レ ミ ファ ソ ラ
シ ド) when they appear. `motion` is the fastest contour vertex, in canvas
heights per second: every vertex is matched to the nearest vertex of the
previous contour (vertex order can shift between detections) and the largest
distance is divided by the time between detections. `level` maps motion
between `motionFloor` (0.15) and `motionFull` (1.5) to 0–1, rising in 0.05 s and
falling in 0.6 s. After the person leaves, `active` becomes 0 and the note is
freed once `level` has fallen to 0.

Each person keeps one slot while visible, so CHOP channels never appear or
disappear. A freed slot keeps its last position with `active` 0, and the
slot that has been free longest is reused first, so a fading voice is not
taken over at once. `group` is the smallest person ID in the merged shape. Membership is found by
testing each person's contour against the composed shapes, because merged
SceneObjects carry every source ID.
