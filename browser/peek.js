// pique's hook in the page. Two ways in, both read the page at that moment,
// so nothing is tracked in between:
//
// - Alt+Shift+click on a link peeks it instead of following it.
// - Super+Shift+L / Super+Shift+click: Hyprland takes those chords, and
//   pique-peek then sends this window key code 196, which pages see as F18
//   (a key nothing uses). The answer is
//   the link under the pointer, else the text selected right now, else an
//   empty peek.

function peekable(href) {
  return typeof href === "string" && (href.startsWith("http://") || href.startsWith("https://"));
}

function linkIn(nodes) {
  for (const node of nodes) {
    if (node && node.closest) {
      const anchor = node.closest("a[href]");
      if (anchor && peekable(anchor.href)) return anchor.href;
    }
  }
  return "";
}

function send(message) {
  chrome.runtime.sendMessage(message);
}

// ---- Alt+Shift+click ----------------------------------------------------------

function isPeekClick(event) {
  // isTrusted: a page can't fake the click to open pique on its own.
  return event.isTrusted && event.button === 0 && event.altKey && event.shiftKey &&
    !event.ctrlKey && !event.metaKey;
}

function onClick(event) {
  if (!isPeekClick(event)) return;
  const href = linkIn(event.composedPath ? event.composedPath() : []);
  if (!href) return;
  event.preventDefault();
  event.stopImmediatePropagation();
  if (event.type === "click") send({ url: href });
}

// mousedown too, so the page's own handlers (and Shift+click's selection)
// don't act on the press before the click arrives.
window.addEventListener("mousedown", onClick, true);
window.addEventListener("click", onClick, true);

// ---- F18 from pique-peek --------------------------------------------------------

// The last pointer position in this page, kept here only (never sent): a
// backup for :hover, which some embedders don't keep current.
let pointerX = -1;
let pointerY = -1;
window.addEventListener("mousemove", (event) => {
  pointerX = event.clientX;
  pointerY = event.clientY;
}, { capture: true, passive: true });

function hoveredLink() {
  // The deepest element the pointer is over, then out to its link.
  const hovered = document.querySelectorAll(":hover");
  const link = linkIn(hovered.length ? [hovered[hovered.length - 1]] : []);
  if (link || pointerX < 0) return link;
  return linkIn([document.elementFromPoint(pointerX, pointerY)]);
}

function selectedText() {
  const selection = window.getSelection();
  return selection ? selection.toString().trim().slice(0, 2000) : "";
}

window.addEventListener("keydown", (event) => {
  if (!event.isTrusted || event.code !== "F18") return;
  event.preventDefault();
  event.stopImmediatePropagation();
  const link = hoveredLink();
  const text = link ? "" : selectedText();
  send(link ? { url: link } : text ? { prefill: text } : { empty: true });
}, true);
