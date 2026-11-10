<!-- SPDX-License-Identifier: MIT -->
# Translation UI host checks

Run `sh tests/translation_ui/run.sh`. Both display languages compile the real LVGL
view and SDK language list against an object/event model. Tests cover the native
480×480 bounds, initial allocation failures, repeated teardown, media translation,
face-to-face turn selection, simultaneous USB meeting translation, and USB capability failures.
The single `mybot_lvgl_view_create(display)` entry rejects null displays,
resolutions other than 480×480, and duplicate creation without replacing the live view.

Font checks cover GB2312, JIS X 0208, non-private-use CP932 extensions, modern and
halfwidth kana, punctuation and Latin-1, including zero-advance combining kana
voicing marks. The native font test links the real LVGL RLE decoder and generated
fonts, checks every glyph with A8 stride 1/8 and buffer guards, and compares its
visible pixels against generation-time hashes. This validates lossless decoding
without requiring Pillow or the source TTF on CI; it does not replace device
caption acceptance.

Media and face-to-face share one stored language pair. The face-to-face UI labels
that pair as my/other language, starts with microphone direction NONE, and exposes
two language buttons. Selecting a direction requests the corresponding SDK route;
only the actual direction readback changes the highlight. Selecting the same active
direction is a no-op; selecting it while paused resumes it. Pause stays unavailable
until a direction has been selected. Sound controls remain in the existing menu.
Mode and direction requests use the public nonblocking mailbox. Their controls stay
locked until selection_revision changes and the actual result or failure is read
back; volume, subtitle updates, timer expiry and an old failure code do not complete
the request. The tests also cover mailbox rejection and repeated same-code failures.

Meeting selects LISTEN_SPEAK plus USB audio atomically. Scene changes preserve the
accepted language pair, and capability loss never substitutes a local default pair.
Both routes run without face-to-face turn buttons, and pause
remains available without selecting an input direction. The live caption follows the
latest record from either route and updates its language direction without rebuilding
widgets. Tests cover source/mode acknowledgement, language mapping, USB-not-ready retry,
both caption routes and history, pause/resume/end, the absence of direction commands,
and restoring codec input when returning to media.

Language tests cover shared real settings, USB-unavailable admission, duplicate-language
rejection, delayed acknowledgement, rejected submissions and failed saves. Input
checks cover slider commit on release, actual volume readback and acknowledged
pause/resume. Lifecycle coverage includes start/cancel/late start, stop/cleanup retry,
unknown outcomes, pairing and Wi-Fi provisioning.

Caption tests exercise real copied records: shorter streaming revisions update
labels without rebuilding widgets; busy reads retry on the owned timer; each face
input direction follows its own latest record, so delayed captions from the other
route cannot replace the live sentence. History uses one 64-record SDK store and
shows each sentence's language direction; full text retains the selected sentence
and scroll container while new records arrive. Evicting that sentence preserves its
existing UI snapshot; entering an already-evicted sentence prompts a return to
history. Ended-session history remains available, a new generation clears old text,
and unavailable caption storage is distinguished from waiting for the first caption.

These checks establish view ownership and semantic callbacks. They do not establish
touch coordinates, physical LCD colors, audio routing, cloud translation or acoustic
behavior. S31 display/input tests cover their own driver boundaries. Font checks
require all static UI glyphs at 20/28/40 px, all 6,763 common Chinese caption glyphs,
Latin-1 and punctuation, the 28 px font fallback, and the caption font size budget.

The navigation and fault cases run at native 480×480 in both UI languages.
Every click and state update checks widget bounds, disjoint sibling
touch regions, and a minimum 44×44 pixel touch target. This checks the square
layout without shrinking fonts; physical panel rendering and touch calibration
still require Mosaico hardware.

Mosaico geometry checks use a 64 px corner radius for every visible label and
interactive region. The top status strip must stay between x=60 and x=419;
the home entry must be a single-line label. This guards the rounded cover glass
that the original rectangular bounds check did not model.
