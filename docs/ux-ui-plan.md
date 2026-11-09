# NovelReader UX and UI plan

Status: approved for implementation by the user. Implemented in the native app;
Vita hardware readability, rendering, memory, and control-feel checks remain open.
See the [native-size layout reference](./ux-ui-preview.html). Button surfaces are
flat throughout; shared gradient artwork may be supplied later.

Implementation detail: Settings uses a column of 56-pixel rows beside a wrapped
reading preview, so font family, size, spacing, and theme can be seen together.
Directional neighbors are derived from the shared control rectangles with region
and boundary rules; header/footer/modal controls use the same activation registry.

## 1. Intended experience

NovelReader should feel comfortable to read and predictable to navigate on the
Vita's 960 × 544 screen. A user must be able to complete every task with the
D-pad and Cross/Circle, without discovering a hidden shortcut or using touch.
Touch and shortcuts should provide additional ways to perform the same actions.

The next implementation should follow one agreed design rather than continue
adjusting individual screens independently.

Required outcomes:

- Home contains Continue Reading, without Your Library or Add Book.
- Add Book belongs in Library and remains available for empty and filtered lists.
- Buttons, text, focus, navigation, and return behavior follow shared rules.
- Reader typefaces include Literata and Atkinson Hyperlegible Next again.
- The sidebar highlight stays flat until replacement PNG artwork is supplied.
- Open and Back stay together in the footer; Options stays at the far right.

## 2. Problems verified in the current app

| Finding | Consequence | Planned correction |
| --- | --- | --- |
| Library Search and Filter use 38 × 38 icon targets; Search has no direct D-pad focus path. | Discoverability and controller access differ from touch. | Labeled 56-pixel-high actions in a focusable action row. |
| Book details has 38–46-pixel-wide secondary controls and only handles Cross/Circle directly. | Bookmark, remove, and more actions cannot be selected spatially. | A focusable row of labeled actions; secondary actions move into Options if space is insufficient. |
| Sidebar, content selection, and footer actions use separate navigation assumptions. | Some visible buttons never receive focus. | One explicit focus model for each screen, backed by shared rules. |
| Cover progress uses 14-pixel text; other metadata and page hints use roughly 16–18 pixels. | Essential information is hard to read at device size. | A defined text scale and less information per screen. |
| Library displays a grid control with no corresponding action. | Decorative elements look interactive. | Remove the control until another view actually exists. |
| Library's L/R hint says Change filter, while input changes sidebar destination. | Displayed instructions conflict with behavior. | Generate hints from the actual action mapping. |
| Reader fonts exist in the repository, but font loading, reading rendering, and Settings force Source Sans 3. | Previously available reading choices are unavailable. | Restore font roles, packaging, selection, preview, and persistence together. |

Evidence: [frontend](../src/main.cpp), [geometry](../include/ui.hpp),
[font loading](../src/vita_fonts.cpp), [font inventory](../assets/fonts/README.md),
and [current product description](../README.md).

## 3. Shared visual rules

These are proposed starting dimensions to validate in native-resolution mockups
and on hardware before freezing the implementation.

| Element | Proposed rule |
| --- | --- |
| Application bar | 44 pixels high; identity and screen context only. |
| Footer | 64 pixels high, occupying y=480–544. |
| Sidebar | Retain 208-pixel width; navigation rows at least 56 pixels high. |
| Main content | x=232–936 on sidebar screens; 24-pixel outer margins elsewhere. |
| Spacing | 8-pixel base; 16-pixel control gaps; 24-pixel section separation. |
| Primary buttons and list targets | Minimum 56-pixel height, with at least 12 pixels of horizontal padding. |
| Icon controls | At least 56 × 56; use text labels for unfamiliar or important actions. |
| Screen titles | 30–34 pixels, Source Sans 3 Semibold. |
| Button labels and primary UI text | 24 pixels; 22 pixels only where the native preview demonstrates sufficient readability. |
| Secondary text and page counts | At least 20 pixels; no essential information below this size. |
| Reading text | Retain the existing 22/26/30-pixel choices initially, independent of UI text. |

Keep charcoal surfaces, restrained violet accents, and the moonlit Home artwork.
Place text on opaque surfaces where artwork could reduce readability. Use a
flat fill for sidebar selection, with a separate visible focus outline.

Focused, selected, and disabled are different states: a selected filter or active
sidebar destination must not look like a second focused control. Focus uses a
clear outline plus a fill change. Disabled controls have readable labels and
cannot activate. Labels wrap or the layout grows; action labels do not shrink
or truncate to fit. Long book titles may truncate on cards, with a readable full
title in details.

Use measured contrast targets of at least 4.5:1 for essential text and 3:1 for
control boundaries/focus as design checks, then verify on Vita hardware.

## 4. Home and Library

### Home

- Keep a short welcome heading and one prominent Continue Reading card.
- Show the recent book's cover, title, chapter, and progress at readable sizes.
- Treat the whole card as one action, without separate tiny links inside it.
- Remove Add Book entirely from Home, including its Home Options entry.
- Keep Library available through the sidebar.
- If there is no recent book, replace the resume action with a clear Browse
  Library action. Avoid an enabled Continue button that has nothing to resume.
- Leave breathing room around the card and preserve the artwork on the right.

### Library

Use this proposed arrangement:

```text
Application bar
Sidebar | Library                          Page 1 of 8
        | [ Add Book ] [ Search ] [ Filter ] [ Online ]
        |
        | [ Book 1 ] [ Book 2 ] [ Book 3 ] [ Book 4 ]
        |   title      title      title      title
        |   progress   progress   progress   progress
Footer: [ Open ] [ Back ]   [ Previous ] [ Next ]    [ Options ]
```

- Place the action row at approximately y=120–176. Four labeled buttons fit
  within 704 pixels using widths of 172/156/156/172 and 16-pixel gaps.
- Recommend **four books per page**, replacing eight crowded covers. Four
  164-pixel-wide cards with 16-pixel gaps fit the content width. Reserve about
  y=200–440 for the covers and readable title/progress areas.
- Show page count with the screen heading, rather than squeezing tiny text
  between the grid and footer.
- Add Book opens the existing import browser. Successful import returns to
  Library with the imported book focused. Cancel restores the prior collection
  state. Preserve the existing successful-import behavior that reveals the book
  under All books; do not silently reset sorting.
- Empty Library keeps the action row and explains how to add a book.
- Empty search/filter results offer a focusable Clear search/reset filter action.
- Continue in the sidebar remains a Reading-filtered Library view. Its action
  row and focus behavior match Library; Add Book remains accessible.
- Search opens the system keyboard. Returning restores Search focus and keeps
  the query visible. Filter opens labeled sort/filter choices with active values.
- Do not show a grid/list toggle until both views are implemented.

Four books per page is the main density tradeoff in this proposal: more paging
in exchange for larger covers, readable metadata, and room for collection actions.

## 5. D-pad navigation contract

### General behavior

Every interactive control has a stable identity, rectangle, enabled state,
activation action, and explicit directional neighbors. Drawing, touch, and
controller activation use the same control definitions.

- Show exactly one focused control when controller navigation is active.
- D-pad moves focus in the visible direction; Cross activates the focused action.
- Focus movement alone never opens a screen or changes a setting.
- Circle closes a dialog or returns to the parent screen. It never jumps Home
  unexpectedly. Closing a dialog restores its invoking control.
- Remember focus, page, query, and filter when returning from details, Reader,
  import, Settings, or online browsing where applicable.
- Clamp at boundaries rather than wrapping unexpectedly. Explicit paging
  controls change pages and preserve the nearest valid item position.
- Disabled actions are skipped; the remaining focus graph stays connected.
- Touch uses the same action and updates focus so the next D-pad press starts
  from a predictable place.
- Shortcuts remain optional. A command is never accessible only through a
  shortcut, a touchscreen icon, or a multi-button combination.

### Sidebar screens

- Sidebar Up/Down moves between destinations; Cross opens the destination.
- Right opens the highlighted destination and enters its content, restoring
  that screen's previous focus. On first entry, use the primary action or first
  book; empty Library starts on Add Book.
- Left from the leftmost content control returns to the sidebar. Moving Up/Down
  within the sidebar highlights a destination without opening it.
- Up from the first content row enters the screen's action row where present.
- Down from the last visible content row enters the footer, even if more pages
  exist. This makes footer actions reachable without traversing an entire book
  collection. Use Next/Previous to browse pages.
- Footer Left/Right moves between actions; Up returns to the last content
  position. Down from the last sidebar item also reaches the footer.
- L/R may continue cycling sidebar destinations as shortcuts; labels must match.
- In Settings, Left/Right adjusts the focused value and clamps at its limits;
  Up from the first settings row returns to the sidebar.

Library's concrete route is Sidebar ↔ action row ↔ books ↔ footer. Horizontal
movement traverses each row; vertical movement chooses the nearest column.
Selecting Next/Previous changes the page without opening a book.

### Reader and image viewer

D-pad retains page-turning/scrolling in Reader and panning in the image viewer.
To avoid conflicting meanings, Cross opens Reading controls and Start opens
viewer controls. These panels expose every action as a D-pad-focusable button.

The reading/viewing surface shows shortcut hints rather than touch-only button
rows that controller focus cannot reach. Interactive buttons appear in the
control panel, where D-pad means focus movement. Circle closes the panel and
returns to the same text/image position. Reading controls include Contents,
Illustrations, Bookmarks, Save bookmark, Reading Settings, reading-view toggle,
and return to the originating collection. The illustration list supports
controller selection of every image.

Footer and panel paging/Close buttons also receive focus; they are not merely
keyboard shortcuts. Multi-page menus retain a connected navigation path.

## 6. Remaining screen coverage

| Screen | Planned layout and focus behavior |
| --- | --- |
| Local book details | Readable cover/title/author/progress; labeled Read/Resume and Bookmark actions. Remove lives in Options with a confirmation focused on Keep book by default. |
| Import browser | Large folder/file rows; focusable Refresh, page controls, and Back. Circle goes to the parent folder, then Library at the import root. |
| Contents | Prefer fewer rows if needed to meet 56-pixel targets. Jump and Illustrations are labeled focusable actions. Returning preserves the chapter selection. |
| Bookmarks | Readable book/chapter/passage rows; Open and Remove available through focused actions. Empty state provides a route to Library. |
| Online sources/catalog | Large rows plus labeled Search and page/reload actions; preserve catalog/query position on return from details. |
| Online novel/chapters | Labeled Read/Resume, Save, Chapters, Download, and Refresh actions, grouped without squeezing six buttons into one footer. Description scrolling is separate from action focus. |
| Reading Settings | Focusable typeface, size, spacing, theme, and reading-view rows; Left/Right adjusts values. Explicit Apply, Cancel, and Reset actions. Circle cancels uncommitted changes. |
| About | Readable app information and labeled Credits/Licenses controls, both reachable by D-pad. |
| Loading/error/confirmation panels | Focus moves into the panel; background actions stop responding. Cancel, Retry, Dismiss, and confirmation choices have explicit focus paths. Closing restores focus. |

The Settings Apply/Cancel rule intentionally replaces today's implicit apply on
Circle. Reset affects reading preferences only, previews the result, and waits
for Apply. No silent preference loss when leaving the screen.

## 7. Restore the reader fonts

- Keep Source Sans 3 Regular/Semibold for application UI.
- Restore **Literata** and **Atkinson Hyperlegible Next** for publication text,
  with Vita system font retained as the existing optional fallback choice.
- Preserve the saved identifiers: 0 = Literata, 1 = Atkinson, 2 = Vita system.
  Default new installations to Literata; honor existing saved preferences.
- Both bundled font files and their licenses already exist. Restore their VPK
  entries and load them as distinct reading font roles.
- Make Typeface selectable with D-pad Left/Right and touch. The preview must
  use the actual selected face, size, spacing, and page theme.
- Use the selected reading family for body and emphasized publication text;
  do not switch bold passages into the UI font. Initially use consistent
  synthetic emphasis if matching bundled bold/italic faces are unavailable.
- Measurement and drawing must use identical font selection and glyph fallback,
  including styled spans. Reflow around the current chapter/text or image anchor.
- Preserve bookmarks and reading offsets across font changes, restart, and
  returning from Settings. Failed font loads fall back visibly and safely.

Implementation touchpoints: [font renderer](../src/vita_fonts.cpp),
[font roles](../include/vita_fonts.hpp), [Settings](../include/reader.hpp),
[settings persistence](../src/reader.cpp), [frontend](../src/main.cpp),
and [packaging](../CMakeLists.txt).

## 8. Implementation sequence after design review

1. **Approve the design specification.** Review this plan, especially four books
   per page, the Reader control-panel behavior, and explicit Settings Apply/Cancel.
2. **Create native-resolution mockups and focus maps.** Cover Home with/without
   a recent book, populated/empty Library, book details, Reader controls, and
   Settings with both reader fonts. Include long labels and focus/disabled states.
3. **Implement the shared interaction foundation.** Centralize control geometry,
   action activation, focus transitions, modal ownership, and return focus.
   Validate it on Home and Library before migrating remaining screens.
4. **Restore reader font selection and persistence.** Verify preview, chapter
   reflow, styled text, and package contents before broader visual polishing.
5. **Apply the shared layout to all screens.** Migrate details, import, contents,
   bookmarks, online screens, Settings, viewer controls, and dialogs.
6. **Validate on desktop and Vita.** Complete the checks below, fix failures,
   update product documentation, and produce the installable package.

Existing local edits must be preserved. Update the README only when the new
behavior is implemented; this proposal should not make current documentation
claim that planned features already exist.

## 9. Completion criteria

- A controller-only walkthrough reaches and activates every enabled button on
  every screen, including footer, paging, modal, Retry, and empty-state actions.
- Automated focus checks verify reachability, valid neighbors, disabled controls,
  empty/single-item/partial pages, state restoration, and modal boundaries.
- Shared geometry checks prevent overlaps, off-screen controls, mismatched touch
  targets, and buttons below the proposed minimum size.
- Native-resolution visual checks cover long titles, missing covers, large
  progress counts, errors, all themes, and both reader fonts. Essential text is
  readable without reducing the agreed type sizes.
- Font tests cover saved identifier compatibility, loading failures, styled text,
  fallback glyphs, preview consistency, reflow anchors, bookmarks, and restart.
- The Vita build and existing desktop test suite pass. Verify packaged font
  assets/licenses and measure any font/thumbnail memory impact on device.
- On hardware, complete these flows without touching the screen: import and
  open a book; resume from Home; search/filter and return; change both reader
  fonts; save/open a bookmark; select/view an illustration; browse/download an
  online chapter; cancel/retry an operation; remove a library reference safely.

Build success alone does not establish readability or usable controller navigation.

## Implementation verification

- The Vita package builds with both reader fonts and their licenses included.
- Nine desktop test suites cover the reader, publications, images, online service,
  input geometry/focus, font routing/fallback, and the native frontend itself.
  Native frontend tests exercise the actual screen registry and input dispatch
  with platform drawing/keyboard/filesystem calls mocked.
- Control checks cover reachability, minimum dimensions, overlap, screen bounds,
  empty and partial pages, disabled controls, modal ownership, import cancellation,
  safe removal defaults, Settings Apply/Cancel, and reading-anchor preservation.
- Text contrast on the default surface measures 15.15:1 for primary text,
  8.78:1 for secondary text, and 5.41:1 for disabled labels. The focus outline
  against a focused surface measures 5.81:1.
- The HTML layout reference is available, but the integrated browser could not
  open it in this environment. No browser screenshot review is claimed.
- Device checks remain open: actual font rasterization, optical readability,
  focus visibility, control feel, glyph fallback, and memory use on Vita hardware.
