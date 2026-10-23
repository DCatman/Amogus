/*
 * Plik: content.js
 * Opis: Skrypt wykonawczy wstrzykiwany do kart przegladarki.
 *       Odczytuje reguly wprost z lokalnego storage (calkowicie odporny na Mixed Content i CSP stron HTTPS),
 *       chroni przed bledem "Extension context invalidated", filtruje YouTube oraz naklada kurtyne.
 */

(function () {
  let blockingActive = false;
  let exceptions = [];
  let isScanning = false;
  let scanDebounceTimer = null;

  function isContextValid() {
    try {
      return typeof chrome !== "undefined" && !!chrome.runtime && !!chrome.runtime.id;
    } catch (e) {
      return false;
    }
  }

  function pingBackground() {
    if (!isContextValid()) return;
    try {
      chrome.runtime.sendMessage({ action: "wakeHeartbeat" }, () => {
        if (chrome.runtime.lastError) { /* ignoruj */ }
      });
    } catch (e) { /* ignoruj */ }
  }

  setInterval(pingBackground, 2000);
  pingBackground();

  function normalize(str) {
    if (!str) return "";
    return str.toLowerCase().replace(/^@/, "").replace(/[^a-z0-9]/g, "");
  }

  function isUrlPatternMatch(pattern, url) {
    if (!pattern || !url) return false;
    const p = pattern.trim().toLowerCase();
    const u = url.trim().toLowerCase();

    if (p.startsWith("@") || p === "" || p === "*") return false;

    if (p.startsWith("*") && p.endsWith("*") && p.length > 2) {
      return u.includes(p.slice(1, -1));
    }
    if (p.startsWith("*")) {
      return u.endsWith(p.slice(1)) || u.includes(p.slice(1));
    }
    if (p.endsWith("*")) {
      return u.startsWith(p.slice(0, -1)) || u.includes(p.slice(0, -1));
    }
    return u.includes(p);
  }

  function isChannelAllowed(channelStrings) {
    if (!channelStrings || channelStrings.length === 0) return false;

    for (const rule of exceptions) {
      const r = rule.trim();
      if (!r.startsWith("@")) continue;

      const normRule = normalize(r);
      if (!normRule) continue;

      for (const rawName of channelStrings) {
        const normTarget = normalize(rawName);
        if (!normTarget) continue;

        if (normTarget === normRule || normTarget.includes(normRule) || normRule.includes(normTarget)) {
          return true;
        }
      }
    }
    return false;
  }

  function isUrlAllowed(url) {
    if (!url) return false;
    const lower = url.toLowerCase();
    if (lower.startsWith("chrome://") || lower.startsWith("edge://") || lower.startsWith("opera://") ||
        lower.startsWith("about:") || lower.startsWith("chrome-extension://") ||
        lower.includes("127.0.0.1") || lower.includes("localhost")) {
      return true;
    }
    for (const rule of exceptions) {
      if (isUrlPatternMatch(rule, url)) return true;
    }
    return false;
  }

  function extractAllChannelStrings(card) {
    const strings = new Set();
    const handleLinks = card.querySelectorAll('a[href*="/@"]');
    handleLinks.forEach(a => {
      const href = a.getAttribute('href') || '';
      const match = href.match(/@([^/?#]+)/);
      if (match) strings.add(match[1]);
      if (a.textContent.trim()) strings.add(a.textContent.trim());
    });

    const avatarBtns = card.querySelectorAll('[aria-label*="Go to channel"]');
    avatarBtns.forEach(btn => {
      const label = btn.getAttribute('aria-label') || '';
      const name = label.replace(/Go to channel\s+/i, '').trim();
      if (name) strings.add(name);
    });

    const metaTexts = card.querySelectorAll('.ytContentMetadataViewModelMetadataText, ytd-channel-name, #channel-name, #byline');
    metaTexts.forEach(el => {
      const t = el.textContent.trim();
      if (t && t.length > 1 && !t.match(/^(\d+[kmb]?|\d+:\d+|nowy|new|\d+\s*(hr|hours|days|months|min|sec|godz|dni))/i)) {
        strings.add(t);
      }
    });

    return Array.from(strings);
  }

  function resolveCardContainer(node) {
    return node.closest('ytd-rich-item-renderer') ||
           node.closest('ytd-compact-video-renderer') ||
           node.closest('ytd-video-renderer') ||
           node.closest('yt-lockup-view-model') ||
           node;
  }

  function filterYouTubeCards() {
    const candidates = document.querySelectorAll(
      'ytd-rich-item-renderer, yt-lockup-view-model, ytd-compact-video-renderer, ytd-video-renderer'
    );

    candidates.forEach(node => {
      const hasWatchLink = node.querySelector('a[href*="/watch"]');
      if (!hasWatchLink) return;

      const container = resolveCardContainer(node);
      const channelStrings = extractAllChannelStrings(container);
      if (channelStrings.length === 0) return;

      if (isChannelAllowed(channelStrings)) {
        container.classList.remove('amogus-blackout');
        container.classList.add('amogus-allowed');
      } else {
        container.classList.remove('amogus-allowed');
        container.classList.add('amogus-blackout');
      }
    });
  }

  function enforcePlayerBlock(channelText) {
    let shield = document.getElementById("amogus-yt-player-shield");
    if (!shield) {
      shield = document.createElement("div");
      shield.id = "amogus-yt-player-shield";
      const player = document.getElementById("movie_player") || document.querySelector("video")?.parentElement || document.body;
      if (player) {
        player.appendChild(shield);
      }
    }

    shield.innerHTML = `
      <div class="yt-shield-content">
        <h2>FILM ZABLOKOWANY</h2>
        <p>Ten film nie pochodzi z dozwolonego kanalu.</p>
        <div class="channel-tag">[${channelText || "Nieautoryzowany kanal"}]</div>
      </div>
    `;

    const video = document.querySelector("video");
    if (video) {
      video.pause();
      video.muted = true;
    }
  }

  function removePlayerBlock() {
    const shield = document.getElementById("amogus-yt-player-shield");
    if (shield) shield.remove();
  }

  function evaluateWatchPage() {
    const strings = new Set();
    const ownerEl = document.querySelector('#owner #channel-name a, ytd-watch-metadata #channel-name a, ytd-video-owner-renderer a');
    if (ownerEl) {
      const href = ownerEl.getAttribute("href") || "";
      const match = href.match(/@([^/?#]+)/);
      if (match) strings.add(match[1]);
      if (ownerEl.textContent.trim()) strings.add(ownerEl.textContent.trim());
    }

    if (strings.size === 0) return;
    const list = Array.from(strings);
    if (isChannelAllowed(list)) {
      removePlayerBlock();
    } else {
      enforcePlayerBlock(list[0]);
    }
  }

  function enforceWebShield(url) {
    let shield = document.getElementById("amogus-web-shield");
    if (!shield) {
      shield = document.createElement("div");
      shield.id = "amogus-web-shield";
      shield.innerHTML = `
        <div class="shield-box">
          <h1>BLOKADA AMOGUS</h1>
          <p>Ta witryna nie znajduje sie na liscie dozwolonych wyjatkow:</p>
          <div class="url-display">${url}</div>
        </div>
      `;
      const rootTarget = document.body || document.documentElement;
      if (rootTarget) {
        rootTarget.appendChild(shield);
      }
    }

    if (document.body && shield.parentElement !== document.body) {
      document.body.appendChild(shield);
    }

    document.documentElement.style.overflow = "hidden";
    document.querySelectorAll("video, audio").forEach(el => {
      try { el.pause(); el.muted = true; } catch (e) {}
    });
  }

  function removeWebShield() {
    const shield = document.getElementById("amogus-web-shield");
    if (shield) {
      shield.remove();
      document.documentElement.style.overflow = "";
    }
  }

  function runEvaluation() {
    if (!blockingActive) {
      removeWebShield();
      removePlayerBlock();
      document.querySelectorAll('.amogus-blackout').forEach(el => el.classList.remove('amogus-blackout'));
      return;
    }

    const isYouTube = window.location.hostname.includes("youtube.com");
    if (isYouTube) {
      removeWebShield();
      if (window.location.pathname.startsWith("/watch")) {
        evaluateWatchPage();
      } else {
        removePlayerBlock();
      }
      filterYouTubeCards();
    } else {
      if (isUrlAllowed(window.location.href)) {
        removeWebShield();
      } else {
        enforceWebShield(window.location.href);
      }
    }
  }

  function scheduleRun() {
    if (scanDebounceTimer) clearTimeout(scanDebounceTimer);
    scanDebounceTimer = setTimeout(() => {
      if (isScanning) return;
      isScanning = true;
      try {
        runEvaluation();
      } finally {
        isScanning = false;
      }
    }, 80);
  }

  if (isContextValid()) {
    chrome.storage.onChanged.addListener((changes, areaName) => {
      if (areaName === "local") {
        if (changes.blockingActive !== undefined) {
          blockingActive = changes.blockingActive.newValue === true;
        }
        if (changes.exceptions !== undefined) {
          exceptions = changes.exceptions.newValue || [];
        }
        scheduleRun();
      }
    });

    chrome.storage.local.get(['blockingActive', 'exceptions'], (data) => {
      if (data) {
        blockingActive = data.blockingActive === true;
        exceptions = data.exceptions || [];
        scheduleRun();
      }
    });
  }

  window.addEventListener("yt-navigate-finish", scheduleRun);
  window.addEventListener("yt-page-data-updated", scheduleRun);

  const observer = new MutationObserver(() => {
    if (blockingActive && !isScanning) scheduleRun();
  });
  observer.observe(document.documentElement, { childList: true, subtree: true });

  window.addEventListener("DOMContentLoaded", scheduleRun);
  scheduleRun();
})();
