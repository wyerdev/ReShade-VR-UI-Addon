# ReShade Dependency Snapshot

This file records the ReShade source used by the local ReShade VR UI Add-on build. It is
informational, not a version pin. setup-reshade.ps1 -Action Update fetches
the latest commit on the checkout's upstream default branch and refreshes this
record.

- Repository: https://github.com/crosire/reshade.git
- Checkout: .deps\reshade
- Branch: main
- Commit: 358c345ca2fe64f86e67c694f8379c356627adcb
- Commit date: 2026-09-03T00:35:18+02:00
- Snapshot recorded: 2026-09-05T08:18:06.0392178Z

## Submodules

~~~text
 9e393d6d8a3b30dcc6f2806ef604ec16a27b0d7e deps/d3d12 (v1.608.2b-150-g9e393d6)
 925796543b9d26b8edfcdcecd94c1dac280f29fc deps/fpng (1.0.5-23-g9257965)
 27bed1181560211b55e39a9b132fef8c5846aae5 deps/glad (v0.1.11a-307-g27bed11)
 3912b3d9a9c1b3f17431aebafd86d2f40ee6e59c deps/imgui (v1.92.5-docking)
 8dc970fc771e35239db55dfbce8f46f83f8e9b73 deps/jxl_simple_lossless (remotes/origin/headers-and-color-profile)
 8fda4f5481fed5797dc2651cd91e238e9b3928c6 deps/minhook (v1.3.3-1-g8fda4f5)
 288d3a7ebc1ad959f62d51da75baa3d27438c499 deps/openxr (release-1.0.34)
 7845730cab6ebbdeb621e7349b7dc1a59c3377be deps/spirv (1.5.4)
 28d546d5eb77d4585506a20480f4de2e706dff4c deps/stb (28d546d)
 819011bb01628fe1aa2f1da9f2c842a48fd5680b deps/utfcpp (v2.3.4-149-g819011b)
 1076b348abd17859a116f4b111c43d58a588a086 deps/vma (v2.1.0-989-g1076b34)
~~~

The plugin repository does not copy, modify, bundle, or publish ReShade. The
checkout is used only as a local build dependency and official ReShade remains
the runtime dependency for installed releases.
