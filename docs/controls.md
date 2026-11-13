# Controls

[README](../README.md) · [Interface](interface.md) · [Library and online novels](library-and-online.md)

Button names use the Vita's labels. **Cross** activates the focused action;
**Circle** returns or cancels. **Start** opens Options, or Reading controls in the
reader. Home and About have no Options menu.

## Navigation and menus

- D-pad and either stick move focus within content. On Home, Library, Bookmarks,
  Settings, and About, **L/R** switches sidebar destinations once per press.
- Sidebar destinations and visible footer hints accept touch. They are outside
  D-pad focus; use their labeled button shortcuts or tap them.
- In popups, **Up/Down** selects enabled rows and **Cross** confirms. **Circle**
  returns to a parent submenu or closes the popup; **Start** closes the entire stack.
  Tap a row to activate it, the submenu arrow to go back, or outside to dismiss.
- Dialogs and the system keyboard own input while open. Error dialogs offer
  Dismiss and Retry when available; multi-page messages use Left/Right or their
  Previous/Next buttons. Background actions are blocked.

## Screen mappings

| Screen | Controls |
| --- | --- |
| Home | Cross resumes the recent available book, or opens Library if none exists |
| Library | Cross opens a cover or Add Book; Square searches titles; Triangle sorts/filters; Circle clears an active search before returning Home |
| Import browser | Cross opens a folder or toggles a book; Square imports the selection; Triangle refreshes; Circle goes to parent, storage list, then Library |
| Local book details | Move focus to Read or Chapters and press Cross; Circle returns to Library |
| Bookmarks | Cross opens the saved passage; Start → Remove asks for confirmation; Circle returns to the caller |
| Reader | Left/Right turns pages; Up/Down steps by line; Cross opens Chapters; Square saves the bookmark; Select jumps to it; Start opens Reading controls; Circle saves and returns to Library or online chapters |
| Reader shoulders | L/R changes EPUB or online chapters; for TXT, turns pages |
| Chapters | Up/Down selects across pages; Left/Right browses five entries; Cross opens; Triangle jumps by number; Start → Illustrations opens current-chapter images; Circle returns |
| Illustrations | Up/Down selects; Left/Right browses four entries; Cross views; Circle returns to Chapters |
| Image viewer | Cross zooms in; Square zooms out; Triangle fits; D-pad pans; L/R selects the previous/next illustration; Circle returns to the opening screen |
| Settings | Up/Down selects a row; Left/Right adjusts; Cross applies; Circle cancels; Select resets the preview |
| Online sources | Up/Down selects; Cross opens the catalog; Circle returns to Library |
| Online catalog | Directions select/page; Cross opens details; Square searches; Triangle loads next page or reloads; Select returns to the initial catalog; Circle returns to Sources |
| Online details | Cross activates the focused reading/chapter/download/refresh action; Up/Down scrolls a focused description; Square adds to Library; Select starts/resumes reading; Triangle refreshes chapters; Circle returns |
| Online chapters | Directions select/page; Cross reads; Square downloads the selected chapter; Select downloads all public chapters; Triangle refreshes; Circle returns to details |
| Online request | Circle cancels; download cleanup after removal has no Cancel action |
| About | Directions select Check for Updates, Credits, or License; Cross activates; Circle returns Home |

On the normal reading page, **Triangle has no shortcut**. Settings and narration
are accessed through Reading controls. Bookmarks has no Square-delete shortcut,
and Library has no Select-online shortcut.

## Analog sticks

On text pages, either stick turns pages horizontally and moves by line vertically.
When an illustration is visible in Reader, the sticks are reserved for image
controls: **right stick up** enters inspection, **right stick up/down** zooms,
and **left stick** pans. Right stick down to fit, Triangle, Circle, or a tap can
leave inspection. D-pad and shoulder buttons retain their reader actions.

In the separate image viewer, the **left stick pans** and the **right stick
up/down zooms**. D-pad still pans in fixed steps. Zoom ranges from fit to 4×.

Directional repeat starts after 350 ms, then repeats every 140 ms. Confirm,
cancel, and sidebar switching do not repeat. After a screen or modal transition,
release buttons and return sticks to center before using them again. Opposing
shoulders together have no shortcut.

## Front touchscreen

| Area | Gesture |
| --- | --- |
| Sidebar, footer, buttons, lists | Tap an enabled target to activate its labeled action |
| Library | Tap a cover for details; swipe vertically to scroll rows |
| Paged lists and Bookmarks | Tap an entry; swipe to browse pages |
| Reader body | Tap left/right zones to turn pages; tap center to show/hide reading bars |
| Reader body | Drag horizontally to preview a page turn, then release to complete or settle back; drag vertically to scroll and settle onto a line |
| Reader body | Hold and release to save the bookmark |
| Reader illustration | Tap to inspect; drag/pinch to pan/zoom; tap to return |
| Image viewer | Drag to pan; pinch to zoom; tap the image to toggle fit/2× |
| Settings | Tap selectors/sliders or swipe horizontally on a row; tap footer Apply, Cancel, or Reset |

Tap release must remain on the original target. Swiping does not activate the row
under the finger. Physical button/stick input takes priority over touch. Rear
touch is not used.

## Verification

[Input tests](../tests/controls_tests.cpp) and
[frontend tests](../tests/frontend_tests.cpp) cover dispatch, focus, modal ownership,
return state, repeat/held input, and touch boundaries. SDK input, keyboard, and
rendering calls are mocked in the frontend harness. Touch calibration, gesture
feel, IME behavior, and rendering still require checks on Vita hardware; see the
[validation checklist](performance.md#hardware-validation).
