(() => {
  const $ = (id) => document.getElementById(id);
  const still = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const scrollMode = () => matchMedia("(max-width: 760px), (orientation: portrait) and (max-width: 1100px)").matches;
  const slides = [...document.querySelectorAll(".slide")];
  const total = slides.length;
  const pad = (n) => String(n).padStart(2, "0");

  // A slide's builds: elements with data-step="n", and code blocks whose data-focus ranges
  // light up one per step (starting at data-focus-start, default 1), then clear.
  const parseRanges = (spec) =>
    spec.split("|").map((part) => {
      const lines = new Set();
      for (const piece of part.split(",")) {
        const [a, b = a] = piece.split("-").map(Number);
        for (let n = a; n <= b; n++) lines.add(n);
      }
      return lines;
    });
  slides.forEach((s, k) => {
    s.id = `s${k + 1}`;
    s.setAttribute("role", "group");
    s.setAttribute("aria-roledescription", "slide");
    s.setAttribute("aria-label", `${k + 1} of ${total}: ${s.dataset.title}`);
    let steps = 0;
    s.querySelectorAll("[data-step]").forEach((el) => (steps = Math.max(steps, +el.dataset.step)));
    s.querySelectorAll("pre.code[data-focus]").forEach((pre) => {
      pre._ranges = parseRanges(pre.dataset.focus);
      pre._start = +(pre.dataset.focusStart || 1);
      // one press per highlighted range, then one more to show the whole block again
      steps = Math.max(steps, pre._start + pre._ranges.length);
    });
    s._steps = steps;
  });

  // Progress bar: one segment per slide, a gap where a new act starts.
  const progress = $("barProgress");
  slides.forEach((s, k) => {
    const seg = document.createElement("i");
    if (k && s.dataset.act !== slides[k - 1].dataset.act) seg.className = "act";
    progress.appendChild(seg);
  });
  const segs = [...progress.children];

  let i = 0;
  let step = 0;

  function applySteps(s, st) {
    s.querySelectorAll("[data-step]").forEach((el) => el.classList.toggle("pending", +el.dataset.step > st));
    s.querySelectorAll("pre.code[data-focus]").forEach((pre) => {
      const range = pre._ranges[st - pre._start];
      pre.classList.toggle("focusing", !!range);
      pre.querySelectorAll(".ln").forEach((ln, n) => ln.classList.toggle("on", !!range && range.has(n + 1)));
    });
  }

  function render(announce = true) {
    slides.forEach((s, k) => {
      const on = k === i;
      s.classList.toggle("is-active", on);
      s.classList.toggle("is-past", k < i);
      s.setAttribute("aria-hidden", String(!on));
      s.inert = !on && !scrollMode();
    });
    applySteps(slides[i], step);
    const s = slides[i];
    $("barAct").textContent = s.dataset.act;
    $("barTitle").textContent = s.dataset.title;
    $("barCount").textContent = `${pad(i + 1)} / ${pad(total)}`;
    segs.forEach((seg, k) => {
      seg.classList.toggle("on", k < i);
      seg.classList.toggle("now", k === i);
    });
    const notes = s.querySelector(".notes");
    $("notesBody").innerHTML = notes ? notes.innerHTML : "<p>No notes for this slide.</p>";
    if (announce) $("announcer").textContent = `Slide ${i + 1} of ${total}: ${s.dataset.title}`;
    try { history.replaceState(null, "", `#${i + 1}${step ? `.${step}` : ""}`); } catch (e) {}
    syncPresenter();
    hooks.forEach((h) => h(i));
  }

  function go(n, st = 0) {
    const next = Math.min(Math.max(n, 0), total - 1);
    const changed = next !== i;
    i = next;
    step = Math.min(Math.max(st, 0), slides[i]._steps);
    render(changed);
  }
  const forward = () => (step < slides[i]._steps ? go(i, step + 1) : i < total - 1 && go(i + 1, 0));
  const back = () => (step > 0 ? go(i, step - 1) : i > 0 && go(i - 1, slides[i - 1]._steps));

  // ── notes, full screen, black screen ───────────────────
  function toggleNotes() {
    const panel = $("notesPanel");
    panel.hidden = !panel.hidden;
    $("btnNotes").setAttribute("aria-pressed", String(!panel.hidden));
  }
  function toggleFullscreen() {
    try {
      if (document.fullscreenElement) document.exitFullscreen();
      else document.documentElement.requestFullscreen?.().catch(() => {});
    } catch (e) {}
  }
  const toggleBlack = () => ($("blackout").hidden = !$("blackout").hidden);

  // ── overview ───────────────────────────────────────────
  const overview = $("overview");
  const grid = $("overviewGrid");
  {
    let section = null;
    slides.forEach((s, k) => {
      if (!section || s.dataset.act !== slides[k - 1].dataset.act) {
        section = document.createElement("section");
        section.innerHTML = `<h3></h3><ol></ol>`;
        section.querySelector("h3").textContent = s.dataset.act;
        grid.appendChild(section);
      }
      const li = document.createElement("li");
      const btn = document.createElement("button");
      btn.type = "button";
      btn.innerHTML = `<span>${pad(k + 1)}</span>`;
      btn.append(s.dataset.title);
      btn.addEventListener("click", () => { overview.close(); go(k); });
      li.appendChild(btn);
      section.querySelector("ol").appendChild(li);
    });
  }
  function openOverview() {
    const buttons = grid.querySelectorAll("button");
    buttons.forEach((b, k) => b.setAttribute("aria-current", String(k === i)));
    overview.showModal();
    buttons[i].focus();
  }
  $("overviewClose").addEventListener("click", () => overview.close());
  overview.addEventListener("keydown", (e) => {
    const buttons = [...grid.querySelectorAll("button")];
    const at = buttons.indexOf(document.activeElement);
    const move = { ArrowDown: 1, ArrowRight: 1, ArrowUp: -1, ArrowLeft: -1 }[e.key];
    if (move && at !== -1) {
      e.preventDefault();
      buttons[Math.min(Math.max(at + move, 0), buttons.length - 1)].focus();
    }
  });

  // ── presenter window ──────────────────────────────────
  let presenter = null;
  let startedAt = null;
  const clock = (ms) => {
    const sec = Math.max(0, Math.floor(ms / 1000));
    return `${Math.floor(sec / 60)}:${pad(sec % 60)}`;
  };
  const parseAt = (t) => { const [m, s] = (t || "0:00").split(":").map(Number); return (m * 60 + s) * 1000; };

  function openPresenter() {
    if (presenter && !presenter.closed) { presenter.focus(); return; }
    presenter = window.open("", "agent-cook-presenter", "width=1000,height=680");
    if (!presenter) { alert("Allow pop-ups for this page to open the presenter window."); return; }
    startedAt ??= Date.now();
    presenter.document.write(`<!doctype html><html lang="en"><meta charset="utf-8"><title>Presenter · Let the Agent Cook</title>
<style>
  body { margin: 0; background: #0e0c0b; color: #ded2c6; font: 18px/1.5 system-ui, sans-serif; display: grid; grid-template-rows: auto 1fr auto; height: 100vh; }
  header, footer { display: flex; gap: 24px; align-items: baseline; padding: 14px 22px; border-bottom: 1px solid #352c26; font-family: ui-monospace, Menlo, monospace; }
  footer { border-top: 1px solid #352c26; border-bottom: 0; color: #a8998c; font-size: 15px; }
  .big { font-size: 34px; color: #f7f0e8; font-weight: 700; }
  .act { color: #ff6b2c; text-transform: uppercase; letter-spacing: .14em; font-size: 14px; }
  .pace { margin-left: auto; font-size: 15px; }
  .ahead { color: #7fe0a6; } .behind { color: #ff7a85; }
  main { padding: 18px 26px; overflow: auto; }
  h1 { font-size: 30px; margin: 0 0 4px; color: #f7f0e8; }
  .step { color: #ffb24a; font-family: ui-monospace, Menlo, monospace; font-size: 15px; margin-bottom: 14px; }
  ul { padding-left: 22px; } li { margin: 8px 0; } code { color: #ffb24a; font-family: ui-monospace, Menlo, monospace; }
  button { font: inherit; color: #f7f0e8; background: #1a1614; border: 1px solid #4b3e35; border-radius: 6px; padding: 4px 12px; cursor: pointer; }
</style>
<header><span class="big" id="elapsed">0:00</span><span id="target"></span><span class="big" id="count"></span><span class="act" id="act"></span><span class="pace" id="pace"></span></header>
<main><h1 id="title"></h1><div class="step" id="step"></div><div id="notes"></div></main>
<footer><span>Next: <b id="next"></b></span><span style="margin-left:auto">← → navigate · R reset timer</span><button id="reset" type="button">Reset timer</button></footer></html>`);
    presenter.document.close();
    presenter.document.addEventListener("keydown", onKey);
    presenter.document.getElementById("reset").addEventListener("click", () => { startedAt = Date.now(); syncPresenter(); });
    syncPresenter();
  }
  function syncPresenter() {
    if (!presenter || presenter.closed) return;
    const d = presenter.document;
    const s = slides[i];
    const elapsed = Date.now() - (startedAt ?? Date.now());
    const target = parseAt(s.dataset.at);
    const diff = elapsed - target;
    d.getElementById("elapsed").textContent = clock(elapsed);
    d.getElementById("target").textContent = `planned ${s.dataset.at || "–"}`;
    d.getElementById("count").textContent = `${pad(i + 1)} / ${pad(total)}`;
    d.getElementById("act").textContent = s.dataset.act;
    const pace = d.getElementById("pace");
    pace.textContent = Math.abs(diff) < 30000 ? "on time" : `${clock(Math.abs(diff))} ${diff > 0 ? "behind" : "ahead"}`;
    pace.className = `pace ${Math.abs(diff) < 30000 ? "" : diff > 0 ? "behind" : "ahead"}`;
    d.getElementById("title").textContent = s.dataset.title;
    d.getElementById("step").textContent = s._steps ? `build ${step} of ${s._steps}` : "";
    const notes = s.querySelector(".notes");
    d.getElementById("notes").innerHTML = notes ? notes.innerHTML : "";
    d.getElementById("next").textContent = i < total - 1 ? slides[i + 1].dataset.title : "End";
  }
  setInterval(syncPresenter, 1000);

  // ── input ─────────────────────────────────────────────
  let jump = "";
  function onKey(e) {
    if (e.metaKey || e.ctrlKey || e.altKey) return;
    if (overview.open) return; // the dialog handles its own keys (Esc closes)
    const k = e.key;
    if (/^[0-9]$/.test(k)) { jump += k; return; }
    if (k === "Enter" && jump) { e.preventDefault(); go(+jump - 1); jump = ""; return; }
    jump = "";
    if (["ArrowRight", "ArrowDown", "PageDown", " ", "Enter"].includes(k)) {
      if (e.target.closest?.("a, button") && (k === " " || k === "Enter")) return;
      e.preventDefault(); forward();
    } else if (["ArrowLeft", "ArrowUp", "PageUp", "Backspace"].includes(k)) { e.preventDefault(); back(); }
    else if (k === "Home") go(0);
    else if (k === "End") go(total - 1);
    else if (k === "n" || k === "N") toggleNotes();
    else if (k === "f" || k === "F") toggleFullscreen();
    else if (k === "p" || k === "P") openPresenter();
    else if (k === "o" || k === "O" || k === "g" || k === "G" || k === "?") { e.preventDefault(); openOverview(); }
    else if (k === "b" || k === "B" || k === ".") toggleBlack();
    else if ((k === "r" || k === "R") && e.currentTarget !== document) { startedAt = Date.now(); syncPresenter(); }
  }
  document.addEventListener("keydown", onKey);
  $("btnPrev").addEventListener("click", back);
  $("btnNext").addEventListener("click", forward);
  $("btnGrid").addEventListener("click", openOverview);
  $("btnNotes").addEventListener("click", toggleNotes);
  $("btnPresenter").addEventListener("click", openPresenter);
  $("btnFs").addEventListener("click", toggleFullscreen);
  $("blackout").addEventListener("click", toggleBlack);

  let touchX = null;
  document.addEventListener("touchstart", (e) => { touchX = e.touches[0].clientX; }, { passive: true });
  document.addEventListener("touchend", (e) => {
    if (touchX === null || scrollMode()) return;
    const dx = e.changedTouches[0].clientX - touchX;
    if (Math.abs(dx) > 50) (dx < 0 ? forward : back)();
    touchX = null;
  });

  // ── embers on the opening and closing slides ───────────
  const hooks = [];
  function embers(canvas, slideIndex) {
    if (!canvas || still) return;
    const ctx = canvas.getContext("2d");
    let w = 0, h = 0, running = false, raf = 0;
    const sparks = Array.from({ length: 70 }, () => ({}));
    const reset = (p, anywhere) => {
      p.x = Math.random() * w;
      p.y = anywhere ? Math.random() * h : h + 10;
      p.r = 0.6 + Math.random() * 2.2;
      p.vy = 0.25 + Math.random() * 0.9;
      p.drift = Math.random() * Math.PI * 2;
      p.life = 0.4 + Math.random() * 0.6;
    };
    const size = () => {
      const r = devicePixelRatio || 1;
      w = canvas.clientWidth; h = canvas.clientHeight;
      canvas.width = w * r; canvas.height = h * r;
      ctx.setTransform(r, 0, 0, r, 0, 0);
      sparks.forEach((p) => reset(p, true));
    };
    const frame = (t) => {
      ctx.clearRect(0, 0, w, h);
      for (const p of sparks) {
        p.y -= p.vy;
        p.x += Math.sin(t / 900 + p.drift) * 0.35;
        const fade = Math.max(0, Math.min(1, p.y / h)) * p.life;
        if (p.y < -10) reset(p, false);
        ctx.beginPath();
        ctx.arc(p.x, p.y, p.r, 0, Math.PI * 2);
        ctx.fillStyle = `rgba(255, ${120 + Math.round(p.r * 30)}, 60, ${fade})`;
        ctx.shadowColor = "rgba(255, 107, 44, .8)";
        ctx.shadowBlur = 8;
        ctx.fill();
      }
      if (running) raf = requestAnimationFrame(frame);
    };
    size();
    addEventListener("resize", size);
    hooks.push((current) => {
      const want = current === slideIndex && !scrollMode();
      if (want && !running) { running = true; raf = requestAnimationFrame(frame); }
      if (!want && running) { running = false; cancelAnimationFrame(raf); ctx.clearRect(0, 0, w, h); }
    });
  }
  embers($("embers"), 0);
  embers($("embers2"), total - 1);

  const m = /^#(\d+)(?:\.(\d+))?$/.exec(location.hash);
  go(m ? +m[1] - 1 : 0, m && m[2] ? +m[2] : 0);
  // On phones every slide is on the page: a deep link scrolls to its slide.
  if (m && scrollMode()) slides[i].scrollIntoView();
  addEventListener("hashchange", () => {
    const h = /^#(\d+)(?:\.(\d+))?$/.exec(location.hash);
    if (h && (+h[1] - 1 !== i || +(h[2] || 0) !== step)) go(+h[1] - 1, h[2] ? +h[2] : 0);
  });
})();
