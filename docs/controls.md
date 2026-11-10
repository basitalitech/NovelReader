# Controls audit and verification

## Audit before implementation

All hardware polling is in [main.cpp](../src/main.cpp); [controls.cpp](../src/controls.cpp) owns stick
hysteresis, directional repeat and touch recognition. Screen widgets share focus
and touch rectangles, but footer and sidebar were deliberately excluded.

| Context | Existing assignments and findings |
| --- | --- |
| Home / About | Cross activates content; Circle returns; L/R changes sidebar destination. START and Triangle both open Options; L+R is a third Options shortcut outside sidebar screens. |
| Library | Cross opens details; Circle clears search first, then returns Home. Triangle and Square both open sorting/filtering. Select opens Online. Import's Triangle refresh is intercepted by the generic Triangle handler. |
| Book details | Cross reads (or activates focused Bookmark); Circle returns. Triangle duplicates START. |
| Bookmarks | Cross opens; Square removes; Circle returns. Left/Right physically pages via focus, but touch swipes are missing. Returning from Reader bookmarks leaves sidebar selection on Bookmarks. |
| Reader | Physical Cross opens controls, internal Cross opens Contents; physical Square opens Settings, internal Square saves bookmark. Triangle opens Bookmarks; Select jumps to bookmark. START duplicates Cross. Left/Right pages, Up/Down steps, EPUB/online L/R changes chapter, TXT L/R pages. |
| Contents / illustrations | Cross opens, Circle returns, directions select/page. Contents Square opens illustrations. Triangle Jump is intercepted by Options. |
| Image | Cross zooms in, Square out, directions pan, L/R chooses image. Triangle Fit is intercepted by Options. Either stick continuously pans. |
| Settings | Directions select/adjust; Square applies; Cross on a row does nothing. Circle cancels, Select resets. |
| Online | Sources Cross browses; Catalog Square searches, Triangle next/reload, Select initial catalog. Novel Cross chapters, Square saves, Triangle refreshes, Select reads. Chapter list Cross reads, Square downloads one, Select downloads all, Triangle refreshes. Physical Triangle actions are intercepted by Options. |
| Modals / keyboard | Cross activates focused tile; Circle closes; menu L/R pages. START can replace a dialog. Keyboard completion consumes its dismissing press. |

Touch acts on release, with movement thresholds, multitouch rejection and live
image pan/pinch. Problems: footer/sidebar are inert; panels cannot close/page by
touch; Settings cannot apply/cancel/reset by touch; Bookmarks cannot swipe; image
gestures check only the latest position; taps lack an origin hit check. Physical
repeat persists across transitions and a held input can collide with touch.

Intentional alternatives to preserve: D-pad and analog navigation; either stick
for handedness; TXT shoulder page turns for comfortable one-handed reading;
touch equivalents and menus exposing shortcuts for discoverability. The generic
Triangle/START/L+R Options aliases and Library's two filter buttons have no
distinct purpose.

## Final mapping

Cross activates focused content, Circle returns/cancels, and START toggles the
screen's Options/Controls popup. Options and Library's Triangle Filter/Sort menu
share a compact vertical popup anchored at the bottom-right above the bottom bar.
Up/Down selects enabled rows; Cross confirms; Circle closes or returns to the
parent submenu. START closes the complete popup stack. Active sorting and
filtering choices have checkmarks. Background shortcuts are consumed.
Error and busy overlays continue to own input exclusively.

| Screen | Contextual controls |
| --- | --- |
| Home / About | D-pad or either stick selects content; Cross continues/opens Library or activates the selected credits/license item. Home omits the unused Back hint. |
| Library | Cross opens focused book details or header action; Circle clears an active search first, then returns Home; Triangle filters/sorts; Square searches; Select opens Online. |
| Import browser | Cross opens/imports; Circle parent/cancel at root; Triangle refreshes; Square sorts. |
| Book details | Cross (Select) starts/resumes reading; Circle returns to the preserved Library selection and scroll. Save bookmark remains available in Options. |
| Bookmarks | Cross opens position; Square removes; directions select/page; Circle restores the caller and its sidebar destination. |
| Reader | Cross Contents; Circle saves and returns to Library/online chapters; Triangle Bookmarks; Square Settings; Select saved bookmark; START Reading controls. Left/Right pages, Up/Down steps; L/R changes EPUB/online chapters or pages TXT. Save bookmark and narration remain in Reading controls. |
| Contents / illustrations | Cross opens; Circle returns; directions select/page. Contents Triangle jumps by number; Square opens current chapter illustrations. |
| Image viewer | Cross zooms in; Square zooms out; Triangle fits; D-pad pans; either stick pans continuously; L/R chooses image; Circle restores Reader or illustration list, according to origin. |
| Settings | Up/Down selects; Left/Right adjusts; Cross applies; Circle discards; Select resets preview. Square has no assignment. |
| Online sources / catalog | Cross selects; Circle parent. Catalog Square searches; Triangle next/reload; Select initial catalog. |
| Online details / chapters | Details Cross (Select) activates focused Read/Chapters/Download all/Refresh; the Select hint dims on the description. Square (Add to Library) favorites and dims once saved; Select reads. The description accepts focus for Up/Down scrolling. Chapters Cross reads, Square downloads one, Select downloads all. Triangle refreshes on both. |

L/R changes sidebar destinations on Home, Library, Bookmarks, Settings, and
About. These destination changes are edges only. Other directional movement
keeps the existing 350 ms initial / 140 ms repeat timing. Reader/viewer shoulders
also repeat. Simultaneous opposing shoulders have no
shortcut; release them before using a shoulder again. Cancel takes priority over
confirm, and one physical action is dispatched per sample.

Both analog sticks remain deliberate alternatives to D-pad navigation for
handedness and accessibility. TXT shoulder page turns remain an ergonomic
alternative to Left/Right. Menu entries retain direct shortcuts' functionality
for discoverability and touch access; these are shared actions, not additional
physical aliases. Popup hints show Cross to select and Circle to close/back.

## Touch and event changes

- Visible sidebar destinations and footer icons/labels accept taps using the
  same navigation/actions as buttons. Footer gutters remain inactive. Focus
  still stays in content; footer geometry, styling and gradients are preserved.
- Popup rows activate on tap; the bottom bar stays visible with Select and Close.
  Submenus show a tappable left arrow in their top-left corner to return to the
  parent menu; Circle and the bottom-bar Close also return from a submenu.
  Tapping outside dismisses the entire popup without activating the background.
  Popup opening/dismissal is immediate and retains idle redraw suppression;
  fonts and textures are reused. Settings supports touch Apply,
  Cancel and Reset; busy requests support touch Cancel. Background controls
  cannot receive modal taps or shortcuts.
- Tap release must remain on the original target. Book grids retain clipped
  hit regions; swipes scroll without opening covers. Bookmarks now supports
  swiping across its three-row pages.
- Reader page swipes reject ambiguous diagonals. Body taps cannot cross action
  zones, holds share the menu's bookmark operation, and the immersive reader's
  bottom reading area no longer hits an invisible footer. Horizontal Settings
  swipes adjust the row touched. Image transforms must start in the image area.
- The existing recognizer still tracks touch-down, movement and release, and
  processes live image transforms. Button sampling exposes held/released state
  alongside press edges and navigation repeat. Screen, import, keyboard, busy
  and modal transitions suppress held inputs until release, without a cooldown.
  Hardware holds cancel active touch even between repeat events.
- Input polling retains reserved touch storage, lightweight integer context
  comparisons and VBlank idle waits. No dependency or worker was introduced.

## Verification

Desktop CMake build and all **13 CTest suites pass**. The Vita Release build
produces `build/vita/NovelReader.vpk` and passes the pthread-symbol packaging
check. Editor diagnostics and `git diff --check` are clean.

New and updated tests in [controls_tests.cpp](../tests/controls_tests.cpp) and
[frontend_tests.cpp](../tests/frontend_tests.cpp) cover:

- Physical-only, touch-only and mixed navigation; Library selection/scroll,
  footer hit gaps, tap-origin boundaries, search exit and keyboard cancellation.
- Held/released state, repeat timing, immediate repress after release, buttons
  held across dialogs/screens, one-time sidebar changes, and cancel priority.
- Touch modal Close/pagination, Settings apply/cancel/reset and anchor retention,
  bookmark save/open/delete/return, viewer return origins and contextual Triangle
  dispatch, including offline online-request dispatch checks.
- Reader tap/swipe/hold, diagonal rejection, immersive input, pan-origin bounds,
  multitouch cancellation and one gesture per release.
- Existing repeated book open/exit, persisted progress, cached Library/bookmark
  state, 120 unchanged idle polls, and profiling assertions against warm-screen
  I/O regressions. Other suites cover EPUB, layouts, speech, fonts and networking.

SDK input, rendering, IME and textures are mocked in frontend tests. On a real
Vita, still perform all ten requested scenarios: visit every screen using only
buttons, only touch, then mixed input; hold directions/actions; tap and scroll
Library; repeatedly open/exit books; open/close dialogs; exit search with Circle;
check every contextual hint; and confirm transition latency. Also check touch
calibration, diagonal-swipe feel, live image pinch/pan, system keyboard, online
cancel/retry, frame pacing and CPU behavior. No hardware results are claimed.
