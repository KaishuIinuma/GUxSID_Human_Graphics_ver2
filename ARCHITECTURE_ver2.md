# GUxSID Human Graphics ver2 architecture

ver2 keeps the current visual behavior while separating the runtime into five
stages. Each stage receives plain data and can be replaced without changing the
stages before it.

```text
Camera / Video
  -> SourceCrop (fit the output aspect, no stretching)
  -> PersonSegmenter (Detection: Core ML direct, or ONNX Runtime)
  -> ObjectTracker
  -> GeometryProcessor
       -> VertexRemapper
       -> LooseContourProcessor (optional)
       -> OffsetProcessor
  -> SceneComposer
       -> merge / grouping
       -> SceneObject
  -> MergeEvent
       -> StandardMergeEvent
  -> SceneLayout
  -> SceneBehavior
  -> BackgroundGradientPass
  -> PaperShadowPass (shadow of everything below)
  -> RenderRecipe
       -> BasePass
       -> StrokePass
       -> ShapePainter
  -> BridgeStrokePass (optional)
  -> SoundOscSender (OSC to the sound app)
```

Each `SceneObject` also owns an `AppearanceComponent` with independent Base
and Outline Material slots. `Solid` and `LinearGradient` are currently
available; materials are assigned from the palette without repeating their
primary color on the same screen.

## Ownership

- `core/`: data and enums shared across stages.
- `PersonSegmenter` / `CoreMLYoloSession`: person instance segmentation (YOLO11-seg).
- `SourceCrop.h`: chooses the part of the camera/video frame that is sent to detection.
- `tracking/`: assigns stable IDs to detection results.
- `geometry/`: performs per-object vertex remapping, optional loose contour,
  and optional offset.
- `scene/`: owns drawable objects, transforms, instances, and composition.
- `event/`: reacts after composition has produced a real merge. The default
  `standard_event` leaves the composed SceneObjects unchanged.
- `scene/SceneLayout`: performs deterministic placement such as recursive
  screen subdivision without changing detection data.
- `scene/SceneBehavior`: updates time-based transforms such as floating motion
  and loosely coupled scale.
- `render/`: turns SceneObjects into pixels. It does not run detection or edit
  source geometry.
- `sound/`: sends per-person data to the sound app over OSC. It never changes the graphics.
- `HumanGraphicsScene.*`: connects the stages and translates existing GUI settings into
  stage-specific settings.
- `ofApp.*`: inputs (camera 1 / camera 2 / video), Controls window, presets,
  Dual Output, projection margin, and export.

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
the active Recipe through `Recipe_ID`. These two are the only Recipes; an
unknown ID falls back to `standard_render`.

Presets live in `bin/data/presets/` (`default`, `fill`, `mix_render`,
`stroke_Poly`, `stroke_Round`).

## Person detection

`PersonSegmenter` runs YOLO11-seg and returns one contour per person, so people
stay separate even when they overlap. It picks the backend when the model is
loaded:

1. If a `.mlmodelc` with the same name as the ONNX model exists
   (`yolo11n-seg.mlmodelc`), `CoreMLYoloSession` runs it directly with Core ML
   on the Neural Engine (about 2 ms on M5).
2. Otherwise, or if Core ML fails, ONNX Runtime with the CoreML execution
   provider on the GPU (about 8 ms). The runtime is loaded from `bin/data/onnxruntime`.

Both backends return the same tensors, so decoding (boxes, NMS, mask
prototypes, contours) is shared. Environment variables for testing:
`GUXSID_YOLO_BACKEND=onnx` forces ONNX Runtime, and `GUXSID_COREML_UNITS`
(`CPUAndNeuralEngine` / `CPUAndGPU` / `ALL` / `CPUOnly`) chooses the device.
`YOLO Input 320` appears in Controls only when `yolo11n-seg-320.onnx` exists.
The active backend is logged together with the average inference time.

Realtime detection runs one frame at a time on a background thread
(`detectionFuture`); the GUI waits for it before changing detection settings.

Before detection, `sourceCrop()` takes the largest rectangle of the input that
matches the output aspect, so the picture is never stretched. Its position
inside the spare margin is `cropPositionX / Y` (center by default).

## Loose contour

`geometry/LooseContourProcessor` (GUI: `Loose Contour`, `Loose Contour
Strength` as % of the person's width) redraws each contour from a
low-resolution mask with a slow distance-field deformation tied to the person
ID. It does not move vertices directly, so it does not depend on the vertex
count or order.

## Bridge stroke

`render/BridgeStrokePass` (GUI: `Graphics Bridge Stroke Enable`) draws one open
gray stroke that connects the centers of all objects on screen, ordered left
to right by X (bottom first when X is equal), so the line never moves back to
the left. It is independent of the Recipe, so it works with every Recipe and
preset. It is drawn on top of the objects and is included in the paper shadow.
Width is `max(2, Stroke Weight × 0.2)`. In Dual Output the two bands are joined
into one line (see below).

## Paper shadow

The graphics are treated as sheets of colored paper lifted slightly off the
background. `render/PaperShadowPass` runs before every Recipe: it draws the
same Recipe (and the bridge stroke) once into a half-resolution FBO, blurs its
alpha, and lays two shadows under the paper. A tight contact shadow gives the
sheet an edge, and a wide soft ambient shadow gives it height. Lengths scale
with canvas height, so preview and export match. Shadow alpha is composited
separately so transparent export keeps it. GUI: `Paper Shadow`, `Paper Shadow
Lift`, `Paper Shadow Angle` (0 = right, 90 = down), `Paper Shadow Opacity`.

## Background gradient

`render/BackgroundGradientPass` keeps the background from being a flat fill.
Over the background color it draws four soft, blurred ellipses of off-white
(about 230–240 on a 255 background) that drift slowly, like a mesh gradient.
Overlaps are combined so they never get much darker than one ellipse, and
sub-step noise prevents banding. Drift uses the Scene's elapsed time, so video
export reproduces it; transparent export leaves the background out. GUI:
`Background Gradient`, `Background Gradient Depth`, `Background Gradient Speed`.

## Projection margin

With `Projection Margin` on (default), only the centered 3840×1020 band of a
3840×2160 frame is projected, so the Scene draws only inside that band and the
outside is filled white. The Scene canvas size follows this band. `F` toggles
full screen on the Main Window.

## Dual Output (2 inputs, 2 outputs)

`Dual Output (2 Screens)` works only in Realtime mode. It runs a second,
independent pipeline for camera 2 and draws both on one 3840×2160 canvas:

| Band | Position | Size | Input | Projectors |
|---|---|---|---|---|
| upper | (0, 0) | 3550×950 | camera 1 → `humanGraphicsScene`, `personSegmenter` | left two |
| lower | (0, 1080) | 3550×950 | camera 2 → `humanGraphicsScene2`, `personSegmenter2` | right two |

The rest of the canvas is black. Each band is drawn into its own FBO (each
Scene clears its background), then both are placed on the canvas, scaled to
fit when the window is not 4K.

- Cameras are chosen per input in Controls (one toggle per detected device).
  If both inputs choose the same device, camera 2 is not opened and shares
  camera 1's frames.
- Every frame `syncSecondPipelineSettings()` copies the GUI-driven settings
  (graphics toggles, offset, stroke, materials, shadow, background, Recipe /
  Event / Layout / Behavior IDs, detection thresholds) from pipeline 1 to
  pipeline 2. Detection settings are not changed while pipeline 2 is
  inferring.
- The bridge stroke treats the two bands as one wide screen: the right edge
  of the upper band touches the left edge of the lower band. Centers of the
  lower band are shifted right by one band width, the joined list is given to
  each band in its own coordinates, and the parts outside a band are clipped
  by its FBO, so the line continues across the seam.
- Debug drawing (`D`) shows each band's own camera and detections.

## Settings

ver2 stores its GUI settings and crash marker under
`Application Support/GUxSID_Human_Graphics_ver2`, independently from the
original project.

## Sound OSC output

`sound/SoundOscSender` sends per-person data over OSC (UDP, `127.0.0.1`,
port 7400, fixed by `ofApp::kSoundOscPort`) while the human graphics scene is
shown. It is header-only and needs no addon. Positions and size are
normalized to 0–1 of the canvas; speed is canvas units per second. In Dual
Output only pipeline 1 (upper band) is sent.

The receiver is the separate app **GUxSID_SfxPlayer**
(https://github.com/KaishuIinuma/GUxSID_SfxPlayer). It plays effect sounds
from `/gux/enter`, `/gux/exit`, `/gux/merge`, `/gux/split` and the motion of
`/gux/note/*`, and also plays the 8 music scenes itself, so the show machine
does not need Logic or TouchDesigner. The earlier TouchDesigner setup remains in `td/`
(`td/sfx`, `td/set_sfx.py`).

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
