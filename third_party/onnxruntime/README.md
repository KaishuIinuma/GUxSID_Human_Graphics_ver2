# ONNX Runtime for macOS arm64

The app loads `bin/data/onnxruntime/libonnxruntime.1.30.0.dylib` at runtime for YOLO inference with the CoreML execution provider. The C API headers in `include/` and the dylib come from the official `onnxruntime-osx-arm64-1.30.0.tgz` release:

https://github.com/microsoft/onnxruntime/releases/download/v1.30.0/onnxruntime-osx-arm64-1.30.0.tgz

Archive SHA-256: `6ebb5062a934537c352937821f9fe9718e7de1a2db1122a93dd363ffd53a7012`.

License and third-party notices are included here. The runtime is arm64 only and uses `MLProgram`, `CPUAndGPU`, and disabled ONNX Runtime CPU execution-provider fallback. CoreML chooses devices within the model. On the development Apple M5, `ProfileComputePlan` reported all 356 CoreML operations on GPU. Device placement and speed on an M1 Pro still need measurement on that machine. Image preprocessing, segmentation mask reconstruction, and rendering remain CPU/OpenGL work.

Set `GUXSID_COREML_PROFILE=1` before launching the app to log the CoreML compute plan on the current machine.
