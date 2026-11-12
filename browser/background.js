// One native message per peek: the host starts pique and exits.
chrome.runtime.onMessage.addListener((message) => {
  if (!message || typeof message !== "object") return;
  let request = null;
  if (typeof message.url === "string") request = { url: message.url };
  else if (typeof message.prefill === "string") request = { prefill: message.prefill };
  else if (message.empty === true) request = { empty: true };
  if (!request) return;
  chrome.runtime.sendNativeMessage("com.pique.peek", request, () => {
    void chrome.runtime.lastError;
  });
});
