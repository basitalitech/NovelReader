# Library and online novels

[README](../README.md) · [Controls](controls.md) · [Interface](interface.md)

## Importing local books

Open **Library → Add Book → Import books**, browse `ux0:`, toggle supported files
with Cross or a tap, then press **Square / Import selected**. Selections persist while browsing
folders. The last directory is remembered for the session. Circle moves to the
parent folder, then the storage list, then Library.

Only `ux0:` is exposed by the importer. Listing is not recursive and is limited
to 20,000 entries per folder. Dot-prefixed entries and unsupported files are
excluded. TXT, EPUB, and saved `.nvr` indexes can be imported; importing an index
does not copy or relocate its chapter cache.

Import records file references and leaves originals in place. Copying files into
`ux0:data/NovelReader/books/` does not automatically add them to Library. Same-named
books in different folders have separate identities. Renaming or moving an
imported file changes its save identity; restore missing files to their original
locations to use the existing reference.

Metadata is retained across screen visits. **Triangle / Refresh folder** in the
import browser rereads that directory and refreshes metadata for imported books
there. Restart reloads library metadata. Missing or unreadable members remain
visible with an error.

## Reading status and bookmarks

Library supports title search, Title/Recently read/Reading progress sorting,
and All books/Reading/Finished filters. Preferences persist across launches.
Reading includes started books; Finished means the final page was displayed.
Going back to earlier pages changes the status. EPUB percentages weight chapters
equally; TXT progress uses normalized text offsets. The bundled guide appears
last under All books and is excluded from Reading/Finished.

Each book has one reading position and one bookmark. Saving another bookmark
replaces that book's previous bookmark. Font or spacing changes reflow around
the reading anchor. The Bookmarks screen shows a passage when its chapter is
locally available and uses the normal download/retry flow for missing online
chapters.

## Online sources and downloads

The packaged Lua adapters support **Novgo**, **Asura Novels**, and **Beast Novels**.
Asura's adapter covers novels, not comics. Enter through **Library → Add Book →
Online novels**, or Library's **Start → Add a new book → Online novels** submenu.

1. Browse or search a catalog with Square.
2. Open a novel for its description, reading action, chapters, and download actions.
3. Press **Square / Add to Library** to save a favorite. Browsing and downloading
   do not automatically add one.
4. Read a chapter, download the selected chapter with Square in the chapter list,
   or use Select / Download all for public chapters.

Reading fetches a missing chapter before opening it. Successfully cached chapters
work offline. Requests run on one background worker. Downloads are sequential;
errors and cancellation retain completed chapters. Retry skips valid cached text,
but restarts an interrupted individual chapter. Cancellation during connection
setup can wait for the network timeout.

Cover artwork is optional and cached separately. Failed artwork downloads leave
the novel readable. Refresh chapters can obtain covers for older favorites.
Online chapter text is plain UTF-8 with paragraph breaks; remote inline images,
account login, paid chapters, and browser-verification bypasses are not supported.
Locked chapters are skipped by Download all.

Sources can change their HTML/API, restrict search, or become unavailable. Errors
offer Retry when appropriate. HTTPS uses certificate and hostname verification
with the packaged CA bundle; check Wi-Fi and the Vita's date/time when diagnosing
connection failures. Limits include 4 MiB per response, 2 MiB per chapter text,
20,000 chapter entries, and 400 chapter-list pages per novel.

## Saved data and removal

| Location | Contents |
| --- | --- |
| `ux0:data/NovelReader/library.txt` | Imported membership and online favorites |
| `ux0:data/NovelReader/settings.txt` | Reader and library preferences |
| `ux0:data/NovelReader/progress/` | Per-book reading position and bookmark |
| `ux0:data/NovelReader/books/Online/` | Online indexes, cached covers, and hidden chapter directories |

Local books can live elsewhere on `ux0:`; the library index refers to those paths.
Online `.nvr` indexes, their `.cover` files, and hidden `.chapters` directories
must stay together. Back up the app data directory **and** externally located
books, preserving hidden directories and paths. Stable cached chapter paths let
online progress follow chapter reordering; a removed bookmarked chapter disables
that bookmark.

**Options → Remove from library** asks for confirmation. For a local book it
removes membership, saved progress, and its bookmark, leaving the original file.
For an online favorite it also deletes the downloaded index, artwork, and chapters.
The bundled guide cannot be removed. A damaged library index is reported and is
not silently overwritten by an add operation.
