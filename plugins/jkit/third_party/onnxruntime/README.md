# ONNX Runtime C API header

`onnxruntime_c_api.h` from ONNX Runtime 1.22.0 (the `Microsoft.ML.OnnxRuntime` NuGet package,
`build/native/include`), MIT licensed (see `LICENSE`, Copyright (c) Microsoft Corporation).

JKit compiles against this header only. It doesn't link ONNX Runtime: the runtime library is
downloaded the first time JKit generates a sound and loaded at run time (see `src/gen/ort.cpp`).
