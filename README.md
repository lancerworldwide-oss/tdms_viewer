# tdms_viewer

MinGW window that opens a TDMS capture and plots one numeric channel.

## Download

Each push to `main` builds the release executable and replaces the file on the current GitHub release. Download it at:

https://github.com/lancerworldwide-oss/tdms_viewer/releases/latest/download/tdms_viewer.exe

The release executable has the MinGW runtime linked in.

## Requirements

- CMake 3.28 or newer
- Git
- MSYS2 UCRT64 at `C:/msys64/ucrt64`, providing `g++` and `ninja`
- On WSL, MinGW-w64 x86_64 with the posix thread model (`x86_64-w64-mingw32-g++-posix`) and `ninja`

## Architecture

GLFW 3.3.8 creates the window. The context is OpenGL 3 with GLSL 130. Dear ImGui `v1.92.4` draws the interface through its GLFW and OpenGL 3 backends. ImPlot `v1.0` draws the selected channel. Those versions are the ones in the FetchContent example shipped with ImPlot v1.0.

[TDMSpp](https://github.com/rubdos/TDMSpp) commit `67a22fd35129ae7d21698c1083cd73db7bd10527` reads the capture. Configure builds only the library in TDMSpp `src`. FetchContent applies `cmake/tdmspp.patch` with `git apply` after cloning that commit. The patch changes `fopen` in `src/tdms_file.cpp` from `"r"` to `"rb"`. MinGW opens `"r"` as text, so the TDMS bytes would not be read unchanged. It also clones the previous segment's channel list in `src/tdms_segment.cpp` instead of sharing it. A later segment can change a channel's sample count; sharing that list makes the earlier segment copy the later count and write past the channel buffer. Nothing else in TDMSpp is modified.

`TDMS::file` keeps the channel bytes for as long as the plot uses them.

File > Open is the Win32 common file dialog, owned by the GLFW window. The filters are `*.tdms` and all files. The chosen path is a narrow string, which is what `TDMS::file` passes to `fopen`. A successful open sets the window title to `tdms_viewer - ` followed by that path. A failed open leaves the title unchanged. Before a file is opened the title is `tdms_viewer`.

File > Export Range and File > Export Range to CSV are Win32 save dialogs owned by the same window. Export Range writes one TDMS channel, limited to the last confirmed View > Set Range indexes, and copies that channel's properties. Export Range to CSV writes those samples with one value on each line.

The left pane lists every object path in the file. The right pane shows the selected path, data type, and value count. The plot is a line of that channel against the sample index. View > RMS can add a second line, the rolling RMS of the indexes visible on the X axis. View > AVG(ABS(Y)) can add another line, the running mean of the absolute value over its own window. Tools > Duration can mark two samples on that line and label the time between them from the channel's `wf_increment` property. Plotted types are the numeric arrays TDMSpp stores in full:

- `tdsTypeI8`, `tdsTypeI16`, `tdsTypeI32`
- `tdsTypeU8`, `tdsTypeU16`, `tdsTypeU32`, `tdsTypeU64`
- `tdsTypeSingleFloat`, `tdsTypeDoubleFloat`

Float and double samples are taken from the file buffer. Integer samples are copied to double precision when the selection changes. A value count that does not fit in an ImPlot count is not plotted.

`tdsTypeI64` is not plotted. TDMSpp reads it with a 32-bit integer into an 8-byte slot, so the samples are not the file's int64 values. TDMSpp does not implement timestamp values. It also does not implement string data, booleans, extended floats, unit-wrapped floats, big-endian data, interleaved data, or DAQmx raw data. A file that needs one of those can make `TDMS::file` throw. The window shows that message and leaves the previous capture in place.

A capture with no objects says `There are no objects.` Until a channel is selected, the plot pane says `Select a channel.`

## Dependencies

Configure downloads these with CMake FetchContent:

- [GLFW](https://github.com/glfw/glfw) `3.3.8`
- [Dear ImGui](https://github.com/ocornut/imgui) `v1.92.4`
- [ImPlot](https://github.com/epezent/implot) `v1.0`
- [TDMSpp](https://github.com/rubdos/TDMSpp) `67a22fd35129ae7d21698c1083cd73db7bd10527`

## Build

Debug:

```sh
cmake --preset mingw
cmake --build --preset mingw


```

The executable is `.build/mingw/tdms_viewer.exe`.

Release:

```sh
cmake --preset mingw-release
cmake --build --preset mingw-release


```

The executable is `.build/mingw-release/tdms_viewer.exe`.

WSL debug:

```sh
cmake --preset mingw-wsl
cmake --build --preset mingw-wsl

```

```sh
# run it 
.build/mingw-wsl/tdms_viewer.exe
```

The executable is `.build/mingw-wsl/tdms_viewer.exe`.

WSL release:

```sh
cmake --preset mingw-wsl-release
cmake --build --preset mingw-wsl-release

```

The executable is `.build/mingw-wsl-release/tdms_viewer.exe`.

## Use

Start `tdms_viewer.exe`. Choose File > Open and select a TDMS file. The window title becomes `tdms_viewer - ` followed by the opened path. Select a channel in the left pane to plot it.

View > Reset fits both axes to the whole channel. View > Set Range opens a dialog titled Set Frame, with Start frame and Stop frame text boxes. Those are inclusive 0-based sample indices: the first sample is 0 and the last is one less than the value count. The boxes start at the sample indexes nearest the current X-axis minimum and maximum. A value halfway between two indexes rounds away from zero. Each index is limited to 0 through the last sample. If the plot has not finished a frame, or either axis limit is not finite, the boxes start at 0 and the last sample. OK sets the X axis to that range, fits Y to the finite samples in it, and stores that channel path with those indexes. The full channel is still drawn. Cancel closes the dialog and leaves the view unchanged. If the text is not a whole number in that range, start is after stop, or the range has no finite sample, the dialog stays open and shows `Start and stop must be whole numbers from 0 through the last sample, and start must not be after stop.` The mouse wheel over the plot zooms both axes toward the pointer. Holding Ctrl while scrolling the wheel scales the Y axis around zero and leaves the X axis unchanged. Scroll up zooms in. Pan, zoom, and this scale do not change the stored indexes. Opening another file successfully clears them.

View > RMS is a check, enabled only when the selected channel is plotted. Turning it on opens a dialog titled RMS with a Window length box. The box starts at the last accepted length, or 1 if none has been accepted. OK accepts a positive whole number and draws a second line, labeled RMS, in the next plot color. Each point is the RMS of that many samples centered on it: the square root of the mean of the squares. An even length takes one extra sample before the center. The whole window has to lie inside the sample indexes shown on the X axis. A window that contains a non-finite sample is omitted, and the remaining points are connected in index order. Float and double samples come from the channel buffer. Integer samples come from the double copy drawn for the channel. A view shorter than the window draws no RMS line. Cancel closes the dialog and leaves the line off. Any other text leaves the dialog open, leaves the line off, keeps the last accepted length, and shows `Window length must be a positive whole number.` Turning the check off removes the line and keeps that length. The line follows the X axis through pan and zoom. Set Range still fits Y to the finite channel samples in the typed range, so an RMS value outside that span is clipped until the view changes. Reset fits both axes to the channel and the RMS points, so a negative channel can extend Y upward to the RMS line. Opening a file, a failed open, and changing the selection leave the check and the accepted length in place. The line is drawn only for a plotted channel.

View > AVG(ABS(Y)) is a check after RMS, enabled only when the selected channel is plotted. Turning it on opens a dialog titled AVG(ABS(Y)) with a Window length box. The box starts at the last accepted length for this line, or 1 if none has been accepted. OK accepts a positive whole number and draws a line labeled AVG(ABS(Y)) in the next plot color, after the channel and after RMS when that line is on. Each point is the mean of the absolute value of that many samples centered on it. An even length takes one extra sample before the center. The whole window has to lie inside the sample indexes shown on the X axis. A window that contains a non-finite sample is omitted, and the remaining points are connected in index order. Float and double samples come from the channel buffer. Integer samples come from the double copy drawn for the channel. A view shorter than the window draws no AVG(ABS(Y)) line. Cancel closes the dialog and leaves the line off. Any other text leaves the dialog open, leaves the line off, keeps the last accepted length, and shows `Window length must be a positive whole number.` Turning the check off removes the line and keeps that length. The RMS check and its length stay independent, and both lines can be on. The line follows the X axis through pan and zoom. Set Range still fits Y to the finite channel samples in the typed range, so an average outside that span is clipped until the view changes. Reset fits both axes to the channel, any RMS points, and these points. Opening a file, a failed open, and changing the selection leave the check and the accepted length in place. The line is drawn only for a plotted channel.

Tools > Duration is a check. It is enabled when the selected channel is plotted and that channel's `wf_increment` is usable, and also when Duration is already on. `wf_increment` is usable when that property is on the selected channel, stored as a float or a double of that type's size, finite, and greater than 0. It is not taken from a parent group. Turning Duration on does not move the axes and does not change RMS, AVG(ABS(Y)), or the stored range. It starts with no markers. Turning it off clears the markers. There is no dialog.

Click the plot once to set the start sample and again to set the end sample. Each click snaps to the finite sample closest to the pointer. The point is drawn on that sample, where the click landed. The click counts only when the plot is hovered, the button press and the release are both inside the plot, and the movement stays inside the drag threshold. A drag still pans, and the wheel zoom is unchanged. A click with no finite sample is ignored. The second click can be the same sample as the first. After both samples are set, further clicks on that channel do nothing until Duration is turned off. A click on another plotted channel with a usable `wf_increment` starts a new measurement on that channel. Changing the selection leaves the markers stored; they are drawn only while that channel is selected. Opening a file successfully clears the markers and leaves the check as it was. A failed open keeps both.

Before the end sample is set, the plot shows that point, with no legend entry and no time. With both samples, it draws both points and a line between them, labeled Duration, in the next plot color. The points are circles on the samples that were clicked. The midpoint is labeled with the time. The text uses the interface text color and stays inside the plot. That label is the only duration readout. The line and the points are left out of Reset and the first fit. The X axis stays the sample index. The time is the absolute difference of the two sample indexes times `wf_increment`, in seconds, so the order of the clicks does not change it. Timestamp values are not read. The label uses `ns` below 0.000001 seconds, including 0, `us` below 0.001 seconds, `ms` below 1 second, `s` below 60 seconds, and `min` at 60 seconds and above. The number is `%.6g` of the value in that unit, then a space, then the unit. A value just below a boundary can show as 1000 of the smaller unit. The unit is not raised after rounding. If either stored sample is non-finite when the plot is drawn, the points and the line are not drawn. If `wf_increment` is not usable, clicks do nothing and the points and the line are not drawn.

File > Export Range and File > Export Range to CSV stay disabled until that stored range belongs to the selected plotted channel. Export Range asks for a `*.tdms` file and writes the channel samples from the stored start through the stored stop, including the channel properties. Groups named in the channel path are included without samples or properties. Export Range to CSV asks for a `*.csv` file and writes one sample value per line, with no header. Cancel leaves the open capture unchanged. A failed export shows the error in the window and leaves the capture in place.
