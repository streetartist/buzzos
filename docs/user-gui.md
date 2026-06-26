# BuzzOS User GUI Example

BuzzOS includes a small user-space GUI sample at `src/user/bin/guidemo.c`.
The build embeds the sample ELF, and the kernel seeds it into the writable
filesystem as:

```text
/fs/apps/guidemo
```

The kernel also seeds:

```text
/fs/apps/guidemo.txt
/fs/apps/guidemo.app
/fs/apps/notes
/fs/apps/notes.txt
/fs/apps/notes.app
/fs/apps/notes.readme
/fs/apps/forms
/fs/apps/forms.cfg
/fs/apps/forms.app
/fs/apps/forms.readme
/fs/apps/calc
/fs/apps/calc.cfg
/fs/apps/calc.app
/fs/apps/calc.readme
```

The GUI app manager filters `/fs/apps` by ELF magic, so text and manifest files
can live beside executable user apps without appearing as launchable programs.
Run `gui` and select `GUIDEMO` in App Manager; the detail panel shows the
manifest metadata, path, size, state file, and README path before launching it.
The app list and detail panel both support mouse wheel scrolling.
`NOTES` is a second user GUI app in the same directory. It provides a multiline
text input area and saves text to `/fs/apps/notes.txt`.
`FORMS` demonstrates a more application-like form with multiple focused
single-line text boxes, mouse focus, Tab/Enter focus movement, live preview,
Left/Right/Home/End/Delete editing, and saved state in `/fs/apps/forms.cfg`.
`CALC` demonstrates a compact tool-style GUI with two focused text input boxes,
operation buttons, keyboard editing, result feedback, and saved state in
`/fs/apps/calc.cfg`.

## Unified UI Style

The seeded user GUI apps share a small style helper in
`src/user/libc/gui_style.h`. It keeps the apps visually consistent without
turning the GUI layer into a large framework:

```text
ui_topbar
ui_panel
ui_button
ui_field
ui_textbox
ui_pointer
```

New small GUI apps should use these shared helpers for top bars, panels,
buttons, text boxes, focus/hover borders, pointer drawing, and status colors.
App-specific layout and behavior still live in each app source file.

## Run It

From the host, open the GUI manager or a specific seeded app directly:

```sh
make run-gui
make run-guidemo
make run-notes
make run-forms
make run-calc
```

From the text shell:

```text
apps
apps info forms
help apps
help gui
help edit
guidemo
notes
forms
calc
```

or:

```text
exec /fs/apps/guidemo
```

From the GUI:

```text
gui
```

Then select `GUIDEMO` in App Manager and press `RUN`.
`NOTES`, `FORMS`, and `CALC` appear in the same App Manager list.
The built-in GUI shell accepts the same compact help topics, for example
`help apps`, `help gui`, `help files`, `help proc`, and `help edit`.

## Runtime State

`GUIDEMO` is intentionally not just a drawing sample. It includes a focused
text input box with keyboard editing and a blinking cursor, then reads and
writes that text through a state file:

```text
/fs/apps/guidemo.cfg
/fs/apps/forms.cfg
/fs/apps/notes.txt
/fs/apps/calc.cfg
```

Click the text box or press `I`, type some text, press Enter to leave the field,
then click `SAVE`. Exit and open the app again; the sample restores its click
count, toggle state, selected color, and text from the writable filesystem.
In `FORMS`, each field is its own text box; press Tab or Enter to move focus,
use Left/Right/Home/End/Delete for cursor editing inside a field, then use
`SAVE`, `LOAD`, `CLEAR`, or `SUBMIT`.
In `CALC`, press Tab to move between the two numeric fields, Enter to compute,
or click the operation buttons. It uses the same textbox editing pattern in a
more tool-like workflow.

## App Manifest

Each app can include a simple `key=value` manifest beside its executable:

```text
/fs/apps/forms
/fs/apps/forms.app
```

Supported manifest keys:

```text
name=FORMS
kind=user-gui
version=1.0
summary=Multi-field form
exec=/fs/apps/forms
state=/fs/apps/forms.cfg
source=src/user/bin/forms.c
readme=/fs/apps/forms.readme
```

The App Manager currently uses `name`, `kind`, `version`, `summary`, `state`,
`source`, and `readme` for the detail panel. Unknown keys are ignored, so the
format can grow without breaking older app manifests.

At build time, app metadata lives beside the app source:

```text
src/user/bin/forms.app
src/user/bin/forms.readme
src/user/bin/forms.seed
```

`tools/gen_app_registry.py` turns those sidecar files into
`src/kernel/app_registry.h`, which the kernel uses to seed `/fs/apps` at boot.
The generated registry is intentionally checked in like `initrd.h`, making the
boot image reproducible and easy to inspect.

## Create A New App

Create a small GUI app scaffold:

```sh
make new-app APP=todo
```

or preview the files first:

```sh
python tools/new_app.py todo --dry-run
```

The scaffold writes:

```text
src/user/bin/todo.c
src/user/bin/todo.app
src/user/bin/todo.readme
```

The generated C app enters graphics mode, draws a focused text input box,
handles mouse clicks and keyboard editing, and saves its text state to
`/fs/apps/todo.cfg`. To make it part of the boot image, add the app name to
`GUI_APP_NAMES` in `Makefile`. Add `src/user/bin/todo.seed` if the app should
ship with default saved state.

Regenerate the kernel app registry:

```sh
make app-registry
```

Validate app packaging without running QEMU:

```sh
make app-check
python tools/check_project.py --list-apps
```

## APIs Used

The sample uses only user-space libc syscall wrappers:

```c
gfx_mode(1);                 /* enter 320x200x256 graphics */
gfx_clear(18);
gfx_fill_rect(x, y, w, h, color);
gfx_text(x, y, "TEXT", fg, bg);
mouse_get(&mouse);
read(0, &key, 1);
open/read/write/close;        /* persistent state in /fs/apps */
sleep_ms(16);
gfx_mode(0);                 /* return to text mode */
```

That is the intended pattern for small user GUI programs: enter graphics mode,
draw each frame, poll input, and return to text mode on exit.
