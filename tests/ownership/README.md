# Ownership regressions

An ASan/UBSan harness that checks how the plugin owns 4D strings, variables, pictures and objects. It compiles these real sources:

- `4DPlugin-opencv-qr.cpp` (unity-included by `harness.mm`)
- `support/4DPlugin-JSON.cpp`
- `C_TEXT.cpp`
- the bundled SDK `4DPluginAPI.c`

It runs them against `fake_engine.cpp`, a synthetic 4D engine installed as `gCall4D`. Input pictures are synthetic QR codes generated with OpenCV, so no camera or user data is used. The engine counts leaks itself, because LSan is unavailable on macOS. It counts picture references, so a leaked retain also counts as a leak. The compiled-mode `CONVERT PICTURE` path is modelled two ways: once replacing the variable's picture, and once converting it in place.

    tests/ownership/run.sh [SOURCE_ROOT] [expected-failure ...]

`SOURCE_ROOT` defaults to this checkout. To run the same harness against an older tree, extract it, for example with `git archive <sha> | tar -x -C dir`. Then pass that directory with the names of the tests expected to fail there.

Test prefixes:

- `live_`: reachable from the plugin commands.
- `dormant_`: helpers with no caller.
- `hardening_`: not proven to occur in 4D.
- `control_`: must pass both before and after a fix.
