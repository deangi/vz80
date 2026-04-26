# ui/ — Touch panel + modals

- `touch_panel.{h,cpp}` — top 48 px strip with three 48x48 buttons:
  `SETUP` (left), `KBD` (next to it), `SCROLL` (right). The middle 176 px
  shows the title + a two-line status area (wifi line + telnet line).
- `setup_modal.{h,cpp}` — popup brought up by `SETUP`. Three buttons:
  `REBOOT`, `MOUNT`, `CLEAR`. (Was four, including `BT`, until M6 was
  removed — see top of `vZ80.ino`.)
- `mount_modal.{h,cpp}` — full-screen overlay for picking a `.dsk`/`.hdd`
  from the SD card root, with drive tabs A..D and OK/UNMNT/CANCEL.
- `keyboard_modal.{h,cpp}` — M8 on-screen US-QWERTY keyboard. Modal
  overlay, full screen width × 144 px tall, sticky one-shot SHIFT/CTRL.
  Toggled by the `KBD` button on the touch panel.
