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

`floating_bridge.json` combines `floating_behavior` with
`floating_bridge_render`; objects are paired in detection order and their
bridge is drawn behind the object fills. Each bridge uses the two contour
vertices that are farthest apart on the axis perpendicular to the line between
the paired object centers. Paired motion uses shared and mirrored displacement,
with reciprocal scale changes, instead of independent noise motion.

ver2 stores its GUI settings and crash marker under
`Application Support/GUxSID_Human_Graphics_ver2`, independently from the
original project.
