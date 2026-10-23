/* Amogus YouTube channel allow-list. Handles are exact and profile-scoped. */
(() => {
  'use strict';
  if (window.top !== window) return;

  let enabled = false;
  let allowedHandles = new Set();
  let scheduled = false;
  let lastUrl = location.href;
  const ownShieldId = 'amogus-yt-player-shield';

  const normalizeHandle = value => String(value || '').trim().replace(/^@/, '').replace(/\/$/, '').toLowerCase();
  const allowed = handle => {
    const normalized = normalizeHandle(handle);
    return !!normalized && allowedHandles.has(normalized);
  };
  const isYouTubeVideoLink = href => /(?:^|\/)watch(?:\?|$)/.test(href || '') || /(?:^|\/)shorts\//.test(href || '');

  function handleFromHref(href) {
    if (!href) return '';
    try {
      const url = new URL(href, location.origin);
      const match = url.pathname.match(/^\/@([^/]+)(?:\/|$)/);
      return match ? normalizeHandle(match[1]) : '';
    } catch { return ''; }
  }

  function extractCardHandle(card) {
    // Prefer a real @handle URL. Display names are intentionally never used as authorization.
    const anchors = card.querySelectorAll('a[href*="/@"]');
    for (const anchor of anchors) {
      const handle = handleFromHref(anchor.getAttribute('href'));
      if (handle) return handle;
    }
    // Some YouTube layouts expose the channel in an avatar link's aria-label but omit a handle.
    // That information is not sufficient for allow-list authorization, so fail closed.
    return '';
  }

  function candidates() {
    return document.querySelectorAll([
      'ytd-rich-item-renderer', 'ytd-video-renderer', 'ytd-compact-video-renderer',
      'ytd-grid-video-renderer', 'yt-lockup-view-model', 'ytd-reel-item-renderer',
      'ytd-compact-radio-renderer'
    ].join(','));
  }

  function blackoutCard(card, handle) {
    card.classList.remove('amogus-yt-allowed');
    card.classList.add('amogus-yt-blackout');
    card.dataset.amogusYtState = 'blocked';
    // Use a non-interactive overlay while retaining the card's original layout dimensions.
    let overlay = card.querySelector(':scope > .amogus-yt-blackout-overlay');
    if (!overlay) {
      overlay = document.createElement('div');
      overlay.className = 'amogus-yt-blackout-overlay';
      overlay.setAttribute('aria-label', 'Film zablokowany przez Amogus');
      const label = document.createElement('span');
      label.textContent = 'AMOGUS · FILM ZABLOKOWANY';
      overlay.appendChild(label);
      card.appendChild(overlay);
    }
    overlay.dataset.handle = handle ? `@${handle}` : 'brak uchwytu @';
  }

  function allowCard(card) {
    card.classList.remove('amogus-yt-blackout');
    card.classList.add('amogus-yt-allowed');
    card.dataset.amogusYtState = 'allowed';
    const overlay = card.querySelector(':scope > .amogus-yt-blackout-overlay');
    if (overlay) overlay.remove();
  }

  function filterCards() {
    for (const card of candidates()) {
      // Avoid styling channel shelves, ad shells, and containers without a video/short link.
      const videoLink = card.querySelector('a[href*="/watch"], a[href*="/shorts/"]');
      if (!videoLink) continue;
      const href = videoLink.getAttribute('href') || '';
      if (!isYouTubeVideoLink(href)) continue;
      const handle = extractCardHandle(card);
      if (handle && allowed(handle)) allowCard(card);
      else blackoutCard(card, handle);
    }
  }

  function ensurePlayerShield(handle) {
    const player = document.querySelector('#movie_player, #player, ytd-player, #player-container-outer') || document.querySelector('video')?.parentElement;
    if (!player) return;
    let shield = document.getElementById(ownShieldId);
    if (!shield) {
      shield = document.createElement('div');
      shield.id = ownShieldId;
      const heading = document.createElement('strong'); heading.textContent = 'FILM ZABLOKOWANY';
      const message = document.createElement('p'); message.textContent = 'Ten film nie pochodzi z kanału dozwolonego w aktywnym profilu Amogus.';
      const channel = document.createElement('span'); channel.className = 'amogus-yt-shield-channel'; channel.dataset.role = 'channel';
      shield.append(heading, message, channel);
      player.appendChild(shield);
    }
    const channel = shield.querySelector('[data-role="channel"]');
    if (channel) channel.textContent = handle ? `Kanał: @${handle}` : 'Kanał nie udostępnia zatwierdzonego uchwytu @';
    const video = document.querySelector('video.html5-main-video, #movie_player video, video');
    if (video) { try { if (video.dataset.amogusWasMuted === undefined) video.dataset.amogusWasMuted = video.muted ? '1' : '0'; video.pause(); video.muted = true; video.removeAttribute('autoplay'); } catch {} }
  }

  function removePlayerShield() {
    document.getElementById(ownShieldId)?.remove();
    const video = document.querySelector('video.html5-main-video, #movie_player video, video');
    if (video && video.dataset.amogusWasMuted !== undefined) { video.muted = video.dataset.amogusWasMuted === '1'; delete video.dataset.amogusWasMuted; }
  }

  function evaluateWatchPage() {
    const owner = document.querySelector('#owner #channel-name a, ytd-watch-metadata #channel-name a, ytd-video-owner-renderer a, #upload-info #channel-name a, ytd-reel-video-renderer #channel-name a, ytd-reel-player-overlay-renderer #channel-name a, #channel-name a[href^="/@"]');
    const handle = owner ? handleFromHref(owner.getAttribute('href')) : '';
    if (handle && allowed(handle)) removePlayerShield();
    else ensurePlayerShield(handle);
  }

  function clearOwnedStyles() {
    document.querySelectorAll('.amogus-yt-blackout, .amogus-yt-allowed').forEach(card => {
      card.classList.remove('amogus-yt-blackout', 'amogus-yt-allowed');
      delete card.dataset.amogusYtState;
    });
    document.querySelectorAll('.amogus-yt-blackout-overlay').forEach(node => node.remove());
    document.querySelectorAll('ytd-rich-section-renderer.amogus-yt-hidden-shorts, ytd-reel-shelf-renderer.amogus-yt-hidden-shorts').forEach(node => node.classList.remove('amogus-yt-hidden-shorts'));
    removePlayerShield();
  }

  function run() {
    scheduled = false;
    if (!enabled || !location.hostname.endsWith('youtube.com')) { clearOwnedStyles(); return; }
    if (location.pathname.startsWith('/watch') || location.pathname.startsWith('/shorts/')) evaluateWatchPage();
    else removePlayerShield();
    filterCards();
    document.querySelectorAll('ytd-rich-section-renderer, ytd-reel-shelf-renderer').forEach(section => {
      if (section.querySelector('a[href*="/shorts/"], a[href*="/reel/"]')) section.classList.add('amogus-yt-hidden-shorts');
    });
  }

  function schedule() {
    if (scheduled) return;
    scheduled = true;
    setTimeout(run, 120);
  }

  function readSettings() {
    try {
      chrome.storage.local.get({ youtubeFilteringActive: false, youtubeAllowedHandles: [] }, state => {
        enabled = state.youtubeFilteringActive === true;
        allowedHandles = new Set((Array.isArray(state.youtubeAllowedHandles) ? state.youtubeAllowedHandles : []).map(normalizeHandle).filter(Boolean));
        schedule();
      });
      chrome.storage.onChanged.addListener((changes, area) => {
        if (area !== 'local' || (!changes.youtubeFilteringActive && !changes.youtubeAllowedHandles)) return;
        if (changes.youtubeFilteringActive) enabled = changes.youtubeFilteringActive.newValue === true;
        if (changes.youtubeAllowedHandles) allowedHandles = new Set((Array.isArray(changes.youtubeAllowedHandles.newValue) ? changes.youtubeAllowedHandles.newValue : []).map(normalizeHandle).filter(Boolean));
        schedule();
      });
    } catch { enabled = false; schedule(); }
  }

  // YouTube is a single-page app; these events cover route changes without reloading the document.
  window.addEventListener('yt-navigate-finish', () => { lastUrl = location.href; schedule(); });
  window.addEventListener('yt-page-data-updated', schedule);
  window.addEventListener('popstate', () => { lastUrl = location.href; schedule(); });
  const observer = new MutationObserver(records => {
    if (records.every(record => record.target?.closest?.('.amogus-yt-blackout-overlay, #amogus-yt-player-shield'))) return;
    schedule();
  });
  if (document.documentElement) observer.observe(document.documentElement, { childList: true, subtree: true });
  else document.addEventListener('DOMContentLoaded', () => { if (document.documentElement) observer.observe(document.documentElement, { childList: true, subtree: true }); schedule(); }, { once: true });
  readSettings();
})();
