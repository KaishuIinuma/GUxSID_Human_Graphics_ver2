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
`standard_behavior`. `ProximityPairing` pairs nearby objects (with a small bias
toward keeping the previous pairs) and orders them so each pair is adjacent;
merging is disabled while this Recipe is active. Each bridge is drawn behind
the object fills and uses the two contour vertices that are farthest apart on
the axis perpendicular to the line between the paired object centers. Both
objects of a pair are split by that perpendicular through their own center:
the outer half takes the left object's color and the inner half the right
object's color.

ver2 stores its GUI settings and crash marker under
`Application Support/GUxSID_Human_Graphics_ver2`, independently from the
original project.

## Sound OSC output

`sound/SoundOscSender` sends per-person data over OSC (UDP, `127.0.0.1`,
port `Sound OSC Port`, default 7400) so an external sound engine (TouchDesigner)
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

Each person keeps one slot while visible, so CHOP channels never appear or
disappear. A freed slot keeps its last position with `active` 0, and the
slot that has been free longest is reused first, so a fading voice is not
taken over at once. `group` is the smallest person ID in the merged shape. Membership is found by
testing each person's contour against the composed shapes, because merged
SceneObjects carry every source ID.
