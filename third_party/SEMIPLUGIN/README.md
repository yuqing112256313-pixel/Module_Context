# SEMIPLUGIN Integration Notes

`SEMIPLUGIN` is the temporary algorithm plugin layer used by Task Flow worker
hosts. The current public interface is intentionally small:

- `api/include/iplugin.h`: common algorithm plugin lifecycle.
- `api/include/iplugin_tgv_etching.h`: temporary TGV etching capability
  interface. This can be replaced in the intranet with the real interface.
- `modules/tgv_etching`: sample implementation used by the E2E package.

## Closed Vendor SDK Layout

When the real algorithm depends on a closed SDK that only provides
`include + lib + dll`, keep it under this directory instead of scattering it
through the project:

```text
third_party/SEMIPLUGIN/vendor/<VendorName>/
  include/
    vendor_api.h
  lib/
    vs2015/x64/Debug/vendor.lib
    vs2015/x64/Release/vendor.lib
  bin/
    vs2015/x64/Debug/vendor.dll
    vs2015/x64/Release/vendor.dll
```

If the SEMIPLUGIN public interface header includes vendor headers, both sides
must be able to include that SDK:

- The algorithm plugin implementation target needs the vendor include path and
  links the vendor `.lib`.
- The worker target needs the vendor include path if it includes the public
  algorithm interface directly.
- The vendor `.dll` must be next to the final algorithm plugin `.dll` at runtime
  or otherwise be on `PATH`.

The top-level CMake file already exposes optional cache variables for this:

```powershell
cmake -S . -B build `
  -DMC_BUILD_TASK_FLOW_E2E=ON `
  "-DMC_SEMIPLUGIN_VENDOR_INCLUDE_DIRS=C:/path/to/vendor/include" `
  "-DMC_TGV_ETCHING_VENDOR_LIBRARIES=C:/path/to/vendor/lib/vs2015/x64/Release/vendor.lib" `
  "-DMC_TGV_ETCHING_VENDOR_RUNTIME_DLLS=C:/path/to/vendor/bin/vs2015/x64/Release/vendor.dll"
```

For VS2015, use the vendor `.lib/.dll` built for the same architecture and CRT
model as the Qt/worker process. Do not mix x86 and x64, Debug and Release, or
different C++ runtime ABIs unless the vendor explicitly supports that ABI.

## Replacement Checklist

1. Replace or extend `api/include/iplugin_tgv_etching.h` with the real
   capability interface.
2. Replace `modules/tgv_etching/src/plugin_tgv_etching_impl.cpp` and matching
   header with the real adapter implementation.
3. Keep the exported C symbols stable or update `Worker.Algorithm.CreateFunc`
   and `Worker.Algorithm.DestroyFunc` in `TaskFlowFieldConfig.psd1`.
4. If the interface requires a BMP path, set
   `Worker.Algorithm.PersistInputImages = $true`.
5. If the interface accepts memory or SDK image objects, keep
   `PersistInputImages = $false` and adapt the worker-side call site in
   `examples/task_flow/worker_sim.cpp`.
6. Confirm whether the vendor algorithm instance is thread-safe. The worker
   calls the plugin from many task threads by default.
