# In-game menu format

The in-game menu is a tabbed overlay. Keep each option on the page that owns
it: File, Display, Graphics, Controller, Cheats, Music or Developer. A page
may be omitted when it has no rows. Use `XBOX_MENU_ITEM.section` to group
related rows, and keep rows for each section together in their registration
order. Use `enabled()` for actions that require a particular game state; the
row stays visible with its explanation while its control is disabled.

## Labels and values

- Use short, player-facing labels. Put a restart marker at the end as
  `(Restart)` only when the change is saved now but takes effect on the next
  game start.
- Use checkboxes for on/off settings and a choice control when exactly one
  value is active. The selected value stays visible beside its label.
- Keep page order predictable: common display, graphics and controller
  settings first; diagnostics and experimental options on Developer.
- Use section names that describe the group, such as `Presentation`,
  `Image quality`, `Camera`, `Logging` or `Push-buffer diagnostics`.

## Descriptions and confirmations

- Give each actionable row a `tip` in plain language. Say what changes, what
  the selected value does, and any performance or restart consequence. Aim
  for one or two short sentences.
- Confirm actions that restart the game, exit, or can affect progress. State
  the consequence in the confirmation text; keep ordinary reversible
  toggles confirmation-free.
- About text uses a heading on its own line, details beginning with `-`, and
  a blank line between sections. Put build or version information first in
  its section.

## Settings behavior

Rows and `options.txt` are two views of the same setting. A row callback
must use the same read and write convention as the option it represents.
Some engine switches are enabled by their presence, so writing `=0` can
turn one on; do not infer a row's state from its numeric value without
checking its parser.
