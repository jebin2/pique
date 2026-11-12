# pique

![pique peeking at a page](docs/screenshot.png)

**The peek browser for Omarchy: look at a link without paying for it with a tab.**

Arc's best idea wasn't Spaces or the sidebar — it was Peek: links you're not
sure about open as a quick preview, and you either close them or keep them.
pique brings that to Hyprland as one small floating window:

- **Ephemeral** — off-the-record profile: no history, no cookies on disk.
  Close the window and the peek never happened.
- **Summonable** — one shortcut over any link or highlighted text, and it opens in about 0.1 s. Every peek is its own window.

## Install

On Omarchy (or any Arch system running Hyprland), install the release package:

    sudo pacman -U https://github.com/jebin2/pique/releases/download/v0.1.0/pique-0.1.0-1-x86_64.pkg.tar.zst

pacman pulls in the Qt libraries it needs from the Arch repositories.

Or build that same release from source yourself:

    git clone https://github.com/jebin2/pique
    cd pique/aur
    makepkg -si

Either way the package sets everything up for you: the two peek shortcuts in
your Hyprland config, a rule that makes the peek window float, and a small
extension for Brave and Chromium. **Restart your browser once** so it loads the
extension.

pique will be on the AUR (`omarchy pkg aur add pique`) as soon as AUR account
registration reopens.

## Use

| Shortcut | What it does |
| --- | --- |
| `Super+Shift+L` | Peek the link under the pointer. No link: the text you highlighted there (an address opens; other text waits in the search bar, Enter searches it). Nothing: an empty search bar |
| `Super+Shift+click` | Same as `Super+Shift+L` |
| `Alt+Shift+click` on a link, in Brave or Chromium | Peek that link |

Inside a peek, the icons above the window are: reader mode, copy the URL, open
it in your browser (and close the peek), tile the window into the workspace,
and close. `Ctrl+W` closes the peek, `Esc` closes an empty one or leaves a
fullscreen video. Links that want a new tab open in the peek itself.

pique also works from a terminal:

    pique example.com
    pique "how far is the moon"     # becomes a DuckDuckGo search
    pique                           # opens an empty peek
    pique --prefill="some text"     # empty peek with that text in the search bar

## Change the shortcuts

Edit `~/.config/pique/shortcuts`:

    key=SUPER + SHIFT + L
    click=SUPER + SHIFT + mouse:272

then run `pique-hypr install`. Upgrades keep your file.

## Remove

    sudo pacman -R pique

This also takes the shortcuts, the float rule and the browser extension out
again.

## How it works

**What "highlighted there" means** depends on the window under the pointer:

- **Brave and Chromium, and pique itself** answer from their own page. pique-peek sends the window a spare key (key code 196, F18 to a page) that nothing uses, and the page reports the link under the pointer or its current selection. For browsers this is a small extension, installed for whichever of Brave and Chromium is present; restart the browser once after installing. Alt+Shift+click on a link also works, straight from the page. The extension sends nothing at any other time.
- **Apps on the accessibility bus** (GTK and Qt apps, such as omawrite) are asked for the link at the pointer and their current selection.
- **Everything else** (terminals): what you highlighted in that window. Wayland has only one highlighted-text buffer: it keeps text after you unhighlight it, and a highlight in another window replaces it, even though the first window still shows its own. So a small watcher (`pique-watch`, a few hundred lines of C using about 150 kB, started by the Hyprland block) keeps the last highlight of each window, and a plain click in a window (the click that unhighlights) forgets that window's. Peek over any window that still shows a highlight and you get that text; after you click it away the peek is empty. The text stays in the watcher's memory only (at most 32 windows); just the window addresses are written out (`$XDG_RUNTIME_DIR/pique-primary`, for the click hook).

**Opening at once.** Starting pique (Qt, QML and the web engine) takes about a second, so one copy always waits hidden and already started (`pique --standby`, no CPU). It waits with only Qt and its window ready, and after a few seconds asks the kernel to swap itself out: with zram that is about 8 MB of RAM, and waking reads it back in milliseconds (without swap it stays at about 31 MB). The web engine starts when it is woken, right after the window appears, which keeps the waiting copy small and adds about 0.1 s before a link's page loads. The Hyprland block starts it; a peek hands it the address or text over `$XDG_RUNTIME_DIR/pique-standby` (private to you), it shows its window in under 0.1 s, and then starts the next spare (also when the peek is closed sooner). Without a spare (just after login, or two peeks at once) a peek simply starts the slow way. An upgrade or removal makes the waiting spare quit; open peeks stay.

## Building from source

    bin/build     # qmake6 + make into build/pique and build/pique-watch
    bin/test      # offscreen Qt Test run
    bin/install   # build this checkout into a package and install it

## Notes

- Nothing is persisted, on purpose. A peek is a peek.
- No Widevine: DRM video (Netflix and friends) won't play. Read, don't binge.
- Some sites refuse non-Chrome browsers; QtWebEngine occasionally gets
  blocked by Google sign-in.
- NVIDIA trouble (blank pages, renderer crashes)? Try
  `QTWEBENGINE_CHROMIUM_FLAGS="--disable-gpu" pique <url>`.
- Danish is a draft translation (`src/i18n/da.json`); corrections welcome.

## License

MIT. `src/theme.*` and `src/translator.*` are derived from
[Omacut](https://github.com/omacom-io/omacut) and
[Monologue](https://github.com/omacom/monologue) (MIT, David Heinemeier
Hansson).
