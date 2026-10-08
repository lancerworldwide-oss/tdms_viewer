# Agent instructions

These instructions apply to every task in this repository.

## Where instructions live

- Agent instructions belong in `AGENTS.md`. When agent instructions change, update `AGENTS.md`.
- User instructions belong in `README.md`. When user instructions change, update `README.md`.

## Do not invent

Do not invent requirements, behavior, APIs, file contents, commands, or any other fact. State and implement only what the user has said or what is already in the repository.

## Clarify ambiguity

Ambiguity must be resolved by the user before you act. If a request, instruction, name, path, behavior, or choice can be read more than one way, or if something needed to proceed was not specified, stop and ask the user. Do not pick an interpretation, fill a gap, or proceed on an assumption.

## Architecture

The program is a MinGW C++20 window. Configure and build with the `mingw` or `mingw-release` preset on Windows, or the `mingw-wsl` or `mingw-wsl-release` preset on WSL. Keep the console subsystem. The `mingw-release` and `mingw-wsl-release` presets link `tdms_viewer` with `-static`, so that executable does not depend on `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, or `libwinpthread-1.dll`.

The window stack is GLFW 3.3.8, OpenGL 3, GLSL 130, Dear ImGui `v1.92.4`, and ImPlot `v1.0`. Those are the versions in the FetchContent example shipped with ImPlot v1.0. ImGui is built with `backends/imgui_impl_glfw.cpp` and `backends/imgui_impl_opengl3.cpp`. GLFW docs, tests, examples, and install stay off.

TDMSpp is commit `67a22fd35129ae7d21698c1083cd73db7bd10527`. FetchContent adds only its `src` directory, which defines the `tdmspp` target. The public include directory is `${tdmspp_SOURCE_DIR}/src`. The clone sets `core.autocrlf` to false, then FetchContent runs `git apply` on `cmake/tdmspp.patch`. That patch changes the one `fopen` in `src/tdms_file.cpp` from `"r"` to `"rb"`. It also clones inherited segment objects in `src/tdms_segment.cpp` and declares that clone in `src/tdms_impl.hpp`, so a later segment cannot change an earlier segment's sample count. Do not apply other TDMSpp changes.

File > Open is the Win32 common dialog (`GetOpenFileNameA`, `comdlg32`), owned by the GLFW window. Filters are `*.tdms` and all files. The path is a narrow string because `TDMS::file` passes it to `fopen`.

`TDMS::file` owns the channel bytes the plot reads. A failed load shows `std::exception::what()` in the window and keeps the previous capture. A successful load replaces the capture and clears the selection and the stored range.

The left pane lists every object from `TDMS::file`, labeled with `get_path()`. The right pane shows the selected object's path, `data_type()`, and `number_values()`. ImPlot draws one line against the sample index, and only for `tdsTypeI8`, `tdsTypeI16`, `tdsTypeI32`, `tdsTypeU8`, `tdsTypeU16`, `tdsTypeU32`, `tdsTypeU64`, `tdsTypeSingleFloat`, and `tdsTypeDoubleFloat`. Float and double are plotted from `data()` in place. Integers are copied to `double` when the selection changes. A count that does not fit in ImPlot's `int` is not plotted.

Do not plot other types. `tdsTypeI64` is stored by `put_le_on_heap_generator<int32_t>()` into an 8-byte value, so it is not the file's int64 data. TDMSpp timestamp reading is a stub. String, boolean, extended float, unit-wrapped floats, big-endian, interleaved, and DAQmx raw data are unimplemented and can make `TDMS::file` throw.

View > Reset fits both axes to the whole channel. View > Set Range opens a dialog titled `Set Frame`, with Start frame and Stop frame text boxes. Those are inclusive 0-based sample indices. The boxes start at 0 and the last sample. OK sets the X axis to that range and the Y axis to the finite samples in that range, and stores the channel path with those inclusive indexes. The stored indexes are the typed start and stop, not the axis limits after a flat range is expanded. The full channel is still drawn. A range whose minimum equals its maximum is expanded by 0.5 on both sides. Invalid text, a start after stop, an index outside the channel, or a range with no finite sample leaves the view and the stored range unchanged, keeps the dialog open, and shows `Start and stop must be whole numbers from 0 through the last sample, and start must not be after stop.` Cancel closes the dialog and leaves the view unchanged. Each action applies for one frame; pan and zoom still work afterward. The mouse wheel over the plot zooms both axes toward the pointer. Holding Ctrl while scrolling the wheel scales the Y axis around zero and leaves the X axis unchanged. Scroll up zooms in. Pan, zoom, and this scale do not change the stored indexes. Both menu items are enabled only when the selected channel is plotted. Changing the selection does not clear the plot limits or the stored range. A successful load clears the stored range with the selection. A failed load keeps it.

File > Export Range and File > Export Range to CSV are enabled only when the selected plotted channel is the stored path. Both open a Win32 save dialog (`GetSaveFileNameA`, `comdlg32`), owned by the GLFW window. The path is a narrow string. Cancel leaves the capture, the stored range, and the error text unchanged. Export Range filters are `*.tdms` and all files, and the default extension is `tdms`. Export Range to CSV filters are `*.csv` and all files, and the default extension is `csv`. The flags are `OFN_OVERWRITEPROMPT`, `OFN_PATHMUSTEXIST`, and `OFN_NOCHANGEDIR`.

Export Range writes one little-endian segment, version 4712, with metadata, a new object list, and raw data. The file contains `/` and each group prefix of the channel path, with no samples and no properties, then the channel at its original path and data type. Samples are the inclusive stored range, copied from `data()` at the native element width. Integer samples are not taken from the double plot copy. Channel properties are written in `std::map` name order. String properties and `tdsTypeI8`, `tdsTypeI16`, `tdsTypeI32`, `tdsTypeU8`, `tdsTypeU16`, `tdsTypeU32`, `tdsTypeU64`, `tdsTypeSingleFloat`, and `tdsTypeDoubleFloat` properties are written little-endian. Strings are a length plus bytes. Any other property type, including `tdsTypeI64` and `tdsTypeTimeStamp`, writes no file and shows `This channel has a property that cannot be exported unchanged.` A channel path that is not `/` or a `/`-separated sequence of single-quoted names writes no file and shows `Channel path cannot be exported.` A range outside the channel, or a channel type that is not one of those numeric types, writes no file and shows `The sample range cannot be exported.` The bytes are built in memory and written once. A failed open shows `File "<path>" could not be opened`. A failed write shows `File "<path>" could not be written`. Either failure leaves the capture and stored range in place. Success clears the error text.

Export Range to CSV uses the same enable rule, range checks, and error text. The file has no header. Each sample is one line ended by `\n`. Integers are decimal integers from the native sample. Finite float and double use `std::to_chars` general format with `std::numeric_limits::max_digits10`. Non-finite values are `nan`, `inf`, and `-inf`.

An empty capture says there are no objects. With no channel selected, the plot pane says to select a channel.
