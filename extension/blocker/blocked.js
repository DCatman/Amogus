document.getElementById('back').addEventListener('click', () => { if (history.length > 1) history.back(); else window.close(); });
document.getElementById('close').addEventListener('click', () => window.close());
