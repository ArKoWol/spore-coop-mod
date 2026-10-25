# In-game UI style (rule for every window of the mod)

Every window the mod shows in the game looks like the CO-OP window (`mod/CoopMod/game/CoopMenu.cpp`), which copies the
game's own star card. A new window reuses that file's drawing helpers (`FillRound`, `Gloss`, `Text`, `AddButton`,
`AddPlayButton`) instead of drawing its own way; move them to a shared file when the second window appears.

## How a window is made
- UTFWin has no text window we can create from code, so a window is a **picture**: GDI+ draws it into a PNG, the PNG goes
  into a small package in `%TEMP%\CoopModUI` (`Models::WritePackage`), a `UTFWin::Window` with an `ImageDrawable` shows it.
- Every change is a new picture with a new resource key, followed by `Invalidate()` (without it the window keeps the old
  picture until the mouse touches it).
- Clicks are hit-tested against the rectangles drawn; `kMsgMouseDown`/`Wheel` over the window are consumed so the galaxy
  does not get them; `kMsgMouseMove` drives the hover look.
- The game's 3D models (the planet of a star card) are drawn over all UI: do not try to cover them; a window next to the
  planet may let it overlap its left edge (intended look).
- Never start game actions straight from a click handler; set a flag and act in the frame update. Do not press the game's
  own buttons with synthetic messages (it ignores them); let the player press them and point at them.

## Look
- **Plate**: rounded rectangle, radius 26, a soft shadow (4 layers, black at alpha 22), a dark rim (92,104,122 to
  58,66,82) and a light glossy body (250,251,253 to 196,204,214) with a white gloss over the upper half. Opaque.
- **Title**: Segoe UI Bold 30 px, orange (255,196,64) with a brown outline (92,44,0), like the star name on the card.
- **Text**: dark ink (32,48,72) for main lines, grey-blue (92,106,126) for second lines, 12.5 to 18 px.
- **Rows** (lists): rounded 16, light gradient (214,222,232 to 236,240,246) with a thin border (168,180,196).
- **Buttons**: glossy pills with a dark rim and white bold label with an outline. Primary (do it) green
  (150,230,80 to 60,160,30), secondary (back, cancel, other) blue (140,175,215 to 70,105,150). **Hover is lighter**
  (green 200,255,140 to 90,200,50; blue 190,220,250 to 100,140,190).
- **Progress**: dark groove, blue glossy fill (130,220,255 to 30,130,215).
- **Success** text green (60,150,30); **error** title red (200,50,40).
- No Close buttons on windows tied to a game element: they close with it (the CO-OP window closes with the star card).
- Text is English for now; GDI+ draws any language, so Russian is possible later.
