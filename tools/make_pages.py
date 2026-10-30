#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the CHORDS site for GitHub Pages:

  index.html                    what CHORDS is, how it plays, and the ways in: try, install, download, source
  img/                          screenshots (docs/img)
  emu/                          CHORDS in the browser (build/emu, from web/emu/build.sh)
  install/index.html            the web installer (web/omni_installer.html, with fm1pkg.js, fm1ota.js and
                                the package's metadata inlined; Chrome or Edge, Web MIDI)
  firmware/chords-VERSION.fwsc    the package the installer writes; also the download

  tools/make_pages.py build/chords-0.1-beta.fwsc 0.1-beta OUT_DIR

The package must be a release build (./build.sh --release X.Y): its identity, FM-1_8XXYYZZ, is what
the installer checks the download against and what the FM-1 reports after the install."""
import html
import json
import re
import shutil
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "web"))
from make_site import product_of, strip_module  # noqa: E402  (Felucca's: the package format)

import os  # noqa: E402
REPO = os.environ.get("CHORDS_REPO") or (f"https://github.com/{os.environ['GITHUB_REPOSITORY']}" if os.environ.get("GITHUB_REPOSITORY") else "#")

LANDING = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CHORDS for the FM-1</title>
<meta name="description" content="A chord generator firmware for the M-VAVE FM-1: the scale's chords on the white keys, its notes on the black, twelve sounds, an arpeggiator, a bass and a full effects chain.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow:wght@400;600&family=Barlow+Semi+Condensed:wght@600;700&display=swap">
<style>
:root { --ground: #faf4e8; --ink: #48392f; --muted: #8a7764; --rule: #e6dccb; --accent: #5c6fd6; --teal: #26a69a; --card: #fffaf1; color-scheme: light; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --ground: #1c1814; --ink: #f2e8d8; --muted: #b3a28e; --rule: #3a322a; --accent: #8fa4ff; --teal: #4cc3b6; --card: #26201b; color-scheme: dark; }
}
:root[data-theme="dark"] { --ground: #1c1814; --ink: #f2e8d8; --muted: #b3a28e; --rule: #3a322a; --accent: #8fa4ff; --teal: #4cc3b6; --card: #26201b; color-scheme: dark; }
body { background: var(--ground); color: var(--ink); margin: 0; padding-inline: 16px; padding-block: 48px 64px;
       font: 400 17px/1.6 "Barlow", "Helvetica Neue", Arial, sans-serif; }
main { max-width: 42rem; margin: 0 auto; }
h1 { font: 700 3rem/1 "Barlow Semi Condensed", "Barlow", sans-serif; margin: 0 0 .5rem; color: var(--accent); }
h2 { font: 700 1.4rem "Barlow Semi Condensed", "Barlow", sans-serif; margin: 2.2rem 0 .6rem; }
.lede { font-size: 1.15rem; margin: 0 0 1.6rem; max-width: 36rem; }
.shots { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; margin: 0 0 1.8rem; }
.shots img { width: 100%; height: auto; border-radius: 14px; image-rendering: pixelated; box-shadow: 0 1px 0 var(--rule), 0 6px 18px rgba(72, 57, 47, .12); }
.status { border: 1px solid var(--rule); background: var(--card); border-radius: 14px; padding: 12px 16px; margin: 0 0 1.6rem; }
.ways { display: grid; gap: 12px; margin: 0 0 1rem; }
.ways a { display: block; text-decoration: none; color: var(--ink); background: var(--card); border: 1px solid var(--rule);
          border-radius: 14px; padding: 14px 18px; }
.ways a:hover, .ways a:focus-visible { border-color: var(--accent); outline: none; }
.ways strong { font: 600 1.25rem "Barlow Semi Condensed", "Barlow", sans-serif; color: var(--accent); display: block; }
.ways span { color: var(--muted); font-size: .95rem; }
table { border-collapse: collapse; width: 100%; font-size: .95rem; }
td { border-top: 1px solid var(--rule); padding: 8px 10px 8px 0; vertical-align: top; }
td:first-child { font-weight: 600; white-space: nowrap; color: var(--teal); }
p.small { color: var(--muted); font-size: .9rem; }
a { color: var(--accent); }
code { font-size: .9em; }
ul.pages { padding-left: 1.2rem; margin: 0 0 1rem; }
ul.pages li { margin: 0 0 .4rem; }
ul.pages b { color: var(--teal); }
.video { position: relative; aspect-ratio: 16 / 9; max-width: 100%; margin: 0 0 1.4rem; border-radius: 14px; overflow: hidden; background: #000; }
.video iframe { position: absolute; inset: 0; width: 100%; height: 100%; border: 0; }
</style>
</head>
<body>
<main>
<h1>CHORDS</h1>
<p class="lede">A chord generator for the M-VAVE FM-1. Pick a scale and a root; the white keys play the scale's
chords, I, ii, iii and on up the keyboard, and the black keys play its notes. Twelve sounds, voicings from dyads to
11ths, HOLD, an arpeggiator, a bass under the chord and the whole effects chain of the web app it comes from.</p>
<div class="shots">
  <img src="img/screen-play.png" width="240" height="240" alt="The screen: the chord C, its numeral I, the seven chords of C major below">
  <img src="img/screen-7ths.png" width="240" height="240" alt="The screen with 7ths: D minor 7 playing">
  <img src="img/screen-arp.png" width="240" height="240" alt="The arpeggiator running up the C chord">
</div>
<div class="status"><strong>Version __VERSION__.</strong> It installs and uninstalls the way Felucca, X0X and FoMni
do, and the installer can put M-VAVE's own firmware back. Installing is at your own risk.</div>
<nav class="ways" aria-label="Get CHORDS">
  <a href="emu/"><strong>Try it in the browser</strong><span>The same code the FM-1 runs, with sound. No FM-1 needed.</span></a>
  <a href="install/"><strong>Install</strong><span>From Chrome or Edge (Chrome on Android works too, with a USB-OTG cable), with the FM-1 connected by USB.</span></a>
  <a href="firmware/__PKG__"><strong>Download __PKG__</strong><span>For the command-line installer: <code>python3 tools/fm1_install.py __PKG__</code></span></a>
  <a href="__REPO__"><strong>Source</strong><span>GPL-3.0. Built on FoMni and Felucca (Hügelton Instruments).</span></a>
</nav>

<h2>Playing</h2>
<table>
<tr><td>White keys</td><td>The scale's chords, one per key: I, ii, iii... and on into the next octave.</td></tr>
<tr><td>Black keys</td><td>The scale's notes, an octave above the chords.</td></tr>
<tr><td>REC</td><td>Swaps the rows: notes on the white keys, chords on the black.</td></tr>
<tr><td>ENV</td><td>Held while you play a chord: one more note in it.</td></tr>
<tr><td>LFO</td><td>Held: chords play single notes (their root). Press it twice quickly to lock single notes on (LFO lights); twice again for chords.</td></tr>
<tr><td>PLAY</td><td>HOLD: chords and notes keep sounding until the next one. Off lets them go.</td></tr>
<tr><td>ARP</td><td>The arpeggiator on and off (its mode, rate, tempo and gate are on the Arp page). Its Melody mode plays the chord with a generated phrase over it.</td></tr>
<tr><td>OCT− · OCT+</td><td>The octave (2 to 5).</td></tr>
<tr><td>SELECT</td><td>Scale: Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Harmonic and Melodic minor, the pentatonics, Blues, Whole tone.</td></tr>
<tr><td>ALGORITHM</td><td>Root.</td></tr>
<tr><td>PRESETS</td><td>Sound: Piano, E. Piano, Organ, Brass, Strings, Pad, Pluck, Bell, Saw, Square, Triangle, Sine.</td></tr>
<tr><td>KNOB 1–4</td><td>The four values on the screen.</td></tr>
<tr><td>SAVE</td><td>Saves. CHORDS also saves by itself a few seconds after a change, once it's quiet.</td></tr>
</table>

<h2>The pages</h2>
<p>Each page puts four values on KNOB 1–4. EDIT steps through all ten.</p>
<ul class="pages">
  <li><b>HOME: Play.</b> Voicing (dyads to 11ths), Attack, Release, Level.</li>
  <li><b>SEQ: Arp.</b> Mode (up, down, up-down, random, melody), Rate (1/4 to 1/32, with triplets), Tempo, Gate.</li>
  <li><b>Melody: phrase.</b> For the Melody mode: Density, Length (1, 2, 4 bars), Variation (how much later bars change), New (turn: a new phrase). The phrase follows your chords and lands on chord tones on the beats.</li>
  <li><b>Melody: shape.</b> Leaps, Contour (free, rising, falling, arch, valley), Syncopate, Ends on (root, third, fifth, free).</li>
  <li><b>Melody: voice.</b> Volume (its own), Octave (−1 to +3 over the chord), Swing, Gate.</li>
  <li><b>Bass.</b> Off, one or two octaves under the chord; its tone and volume.</li>
  <li><b>FX: Effects.</b> Reverb, Delay (an 8th note), Chorus, Tremolo.</li>
  <li><b>SEL: Filter.</b> Low-pass, High-pass, Auto-filter, Vibrato.</li>
  <li><b>Drive.</b> Overdrive, Bitcrush, Waveshape, Compressor.</li>
  <li><b>GLO: Setup.</b> Arp octaves, Tune, MIDI out, Lights.</li>
</ul>
<h2>MIDI</h2>
<ul class="pages">
  <li><b>In</b> (USB and the TRS jack): notes on any channel play the current sound.</li>
  <li><b>Out</b> (USB, on by default): chords and notes on channel 1, the bass on channel 2.</li>
</ul>
<p class="small">To go back to the official firmware, use the installer's "Back to the stock firmware", or M-VAVE's
updater, M-UPGRADE, from <a href="https://www.m-vave.com/download">m-vave.com/download</a>. M-VAVE and FM-1 are
trademarks of their owners. CHORDS is not affiliated with M-VAVE.</p>
</main>
</body>
</html>
"""


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_8\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (./build.sh --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    name = f"chords-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for d in ("install", "firmware"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    page = (SRC / "web" / "omni_installer.html").read_text(encoding="utf-8")
    lib = strip_module((SRC / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((SRC / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"omni_installer.html must contain {mark} once")
    (out / "install" / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")
    shutil.copytree(SRC / "docs" / "img", out / "img")
    (out / "index.html").write_text(LANDING.replace("__VERSION__", html.escape(version)).replace("__PKG__", name)
                                    .replace("__REPO__", REPO), encoding="utf-8")
    emu = SRC / "build" / "emu"
    if not (emu / "omni.wasm").exists():
        raise SystemExit("no build/emu/omni.wasm: run web/emu/build.sh (needs Emscripten)")
    shutil.copytree(emu, out / "emu")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, install/ ({product}), emu/, firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
