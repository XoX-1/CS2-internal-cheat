# Mindcheat setup

Since you cancelled the auto-copy command earlier, you need to manually copy the vendor libraries and headers before compiling:

1. Copy `imgui-master` from the `Cs2-Internal-ai-main` example to `mindcheat/vendor/imgui`.
2. Copy `minhook-master` from the `Cs2-Internal-ai-main` example to `mindcheat/vendor/minhook`.
3. Copy `output/offsets.hpp` and `output/client_dll.hpp` to `mindcheat/sdk/`.

Once done, you can compile the project using CMake:
```cmd
cd mindcheat
cmake -B build
cmake --build build --config Release
```
