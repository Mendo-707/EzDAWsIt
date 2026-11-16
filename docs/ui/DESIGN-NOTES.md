# EzDAWsIt UI: design notes

Mockups: `project/*.dc.html` (1440×900 each), laid out by `project/canvas.json` in three rows: Arrange & editors, Mix/master/export, Start & setup.

## 1. Information architecture (one shell, every screen)

```
+--------------------------------------------------------------------------+
| App bar 40: logo · New/Open/Save · song name + Saved | Arrange|Mix|Master |
|             | Output quick-switch · Setup · Export · Easy|Pro · Help      |
+--------------------------------------------------------------------------+
| Transport 44: Home Play Rec Loop · LCD bar/time · BPM · 4/4 · Key ·      |
|               Click Count-in · Punch · Snap Undo Redo · Sync · Main meter|
+-----------+--------------------------------------------------------------+
| Browser   | Main view (Arrange / Mix / Master)                           |
| 240       |                                                              |
| category  +--------------------------------------------------------------+
| list +    | Detail panel: [context name] [Devices | Clip editor] tabs    |
| contents  | (collapsed 32, open 420)  left inspector 200 | editor        |
+-----------+--------------------------------------------------------------+
| Status bar 22: message · shortcuts · device/rate/buffer/latency · CPU    |
+--------------------------------------------------------------------------+
```

What drives what (one-way, top to bottom):

- **Selection drives the Detail panel.** Selecting a track shows its *Devices* tab (input, instrument, rack, effects, output). Selecting a MIDI clip makes the *Clip editor* tab the piano roll. A clip on a Drum Machine track shows the drum machine. An audio clip shows the audio editor (P5: warp, pitch). Double-click on a clip opens the panel. The panel header always names what it is showing (colour dot + name), so the user never wonders "editing what?".
- **The browser feeds the selection.** Double-click (later drag) on an instrument makes a new track. An effect or chain goes onto the selected track. A template goes into a new song. The browser category follows the view: Instruments in Arrange, Effects in Mix, Mastering chains in Master. The user can still change it.
- **Track data has one owner and several views.** Volume, pan, M/S/R, input/output and the effect list are the same `te::AudioTrack` state, shown in the track header, the mixer strip, the Devices inspector and the Setup routing table. Nothing is a copy.
- **The view switcher changes only the main area.** Browser, transport, detail panel and status bar keep their place, so the user does not lose their bearings.
- **Main output is one setting shown in three places**: the app-bar quick switch (fixes the "headphones are hard" problem), Setup › Audio & Routing, and the Master view footer.
- I left out a permanent right-hand inspector on purpose. Width is what the timeline needs most at 1280. The detail panel's left 200 px column is the inspector, and it changes with context (track, clip, pad).

## 2. Greying convention

- **Built** = full colour, normal controls.
- **Planned** = the element is drawn complete with real labels, then shown at 55 % opacity and desaturated. Its labels still reach about 4:1. A small phase chip (`2C`, `2D`, `2E`, `P3`, `P4`, `P5`) sits at the corner, or inline for rows. Each region gets **one chip per screen**. Repeats (meters on every track, sends on every strip) are dimmed without a chip, so the screen stays clean.
- Big planned regions (piano roll, drum machine, export body) also get a 1 px dashed outline.
- The legend is on the Main artboard, bottom right of the empty arrange area. It is styled as a light annotation card so nobody mistakes it for app UI.
- When a planned screen contains built controls (the Mixer's volume, pan, M/S/R, I/O and insert names; the Master view's main output), those controls stay live-styled inside the greyed screen.
- In JUCE: add `bool planned` to the LookAndFeel, or a `ComponentProperties` flag, and draw with `Colour::withSaturation(0).withMultipliedAlpha(0.55f)`. Then `setEnabled(false)` and put a tiny `PhaseChip` component in the corner. When a feature ships, delete the flag.

## 3. Screens and component states

| Screen | Built (live) | Planned |
|---|---|---|
| **Main (Arrange)** | App bar file buttons, song name/saved, Arrange tab, output quick-switch, Setup; full transport except punch/sync (time signature and key built in 2E, x/4 only for now; main meter with LUFS built in P3); sections lane and tempo/time/key lane (2E); track headers (name, type, device, FX n, M/S/R, volume, pan, colour strip, level meter (P3)); ruler, loop, playhead, grid, clips (selected clip outline), zoom, + Track; status bar | Automation lanes built in P3: A button / A key per track, lanes for any control (volume, pan, sends, effect and instrument controls), Read / Write in the corner (Shift+W / W); Lead Vox take lanes + Comp (P5); Mix (P3) / Master / Export (P4); Easy/Pro, Help tour (P5); browser search and preset categories (P4) |
| **PianoRoll** | Arrange at 56 px track height (Alt+wheel), Devices tab | Whole piano roll (2C): tools, grid, quantise, strength, swing, scale highlight (G minor), fold, keyboard, notes, velocity lane, clip inspector |
| **DrumMachine** | Arrange, Devices tab; built in 2D: kit and genre picker, 16 pads (pad 1 bottom-left), pad editor, patterns A-D, steps/length/swing, per-row M/S, velocity lane, browser Drum kits. "Write to clip" became "Fill clip": the steps edit the clip's notes directly, so there is nothing to write | Planned: computer-keyboard pad playing, pattern chaining, per-pad effects |
| **VocalChain** | Track inspector (input/output choosers), EQ/Compressor/Reverb cards with Open and Remove, Add effect button, and the built-in part of the Add-effect menu | Built in P3: bypass buttons, effects by category in every menu and the browser, 8 new effects (gate, de-esser, limiter, low/high cut, saturator, tune, tremolo, utility), a knob window for every built-in effect. The Vocal Chain rack card (6 macros with hints, 8 presets, the chain inside with each module on/off, Tune key, phone tone), in the Chains group of every effect menu and the browser. Planned: saving your own chains (P4); "Check input level" (P5) |
| **Mixer** | Built in P3: the view, strips with input/output choosers, inserts, sends (one knob per FX return, pre/post, mute), pan, fader with dB marks, Peak meter, M/S/R; FX returns (reverb, delay, empty) and buses as tracks with a chip; Master strip with LUFS | RMS meters, sidechain slot (P3); the mockup's Vox bus and Reverb return are now ordinary buses/returns the user adds; Mix assistant (P4) |
| **Master** | Main output chooser + Test beep | Mastering chain (EQ, multiband, stereo, limiter, dither), loudness targets, LUFS/true-peak readouts and history, reference A/B, Master assistant, Export (P4) |
| **Export** | (none: dialog is P4) | Presets list, full mix / stems / bus stems / MIDI, range, format, rate, depth, dither, loudness match, file and folder |
| **Start** | New empty song, Open, crash-recovery banner, recent songs, current audio device + Test beep + Setup | Template gallery (P4), setup wizard and tour (P5) |
| **Setup** | Audio device (driver, devices, rate, buffer, latency), Main output with Test beep per output, per-track input/output table, Plugins tab, General tab (autosave) | MIDI & Controllers tab (MC-707 / DDJ-REV1 maps, MIDI clock), Recording tab, input level check, setup wizard (P5) |

Setup is a **modal dialog**, not a panel. It is used rarely, and changing devices restarts audio anyway. It merges today's three dialogs (Audio Setup, Plugins, Routing) into one window with tabs. Export is also a modal dialog.

## 4. JUCE implementation notes

- **MainComponent** owns `AppBar`, `TransportBar`, `BrowserPanel`, `MainViewHost`, `DetailPanel` and `StatusBar`. Lay them out with `juce::FlexBox` or simple `removeFromTop/Left` rectangles. Keep the sizes in one `Layout` struct (40/44/240/32/22) so screens never fight over them.
- **MainViewHost** holds `ArrangeView`, `MixerView` and `MasterView` and shows one at a time with `setVisible`. Each keeps its own scroll position.
- **ArrangeView** is the existing timeline code. Put `TrackHeader` rows in a left `Viewport` and `TimelineView` lanes in a right `Viewport`. Sync their vertical scroll from one `ScrollBar` listener. The ruler, sections lane and tempo lane go above the lanes `Viewport`, outside it, with the same horizontal zoom/offset, so they stay visible while scrolling. Automation and take lanes are child rows of a track row. Give `TrackRow` a list of sub-lanes now, even if empty, so adding them later doesn't change layout code.
- **TrackHeader**: reuse it as is. Add a meter slot on the right edge (empty for now) and a compact layout when height is under 64 px (name row + control row).
- **DetailPanel**: a tab bar + `inspector` (200 px) + `editor` `Component*`. It listens to a single `SelectionManager` (Tracktion's `te::SelectionManager` works) and swaps in `DevicesView`, `PianoRollView`, `DrumMachineView` or `AudioClipView`. Height is user-resizable (drag the header) and remembered. Collapsed = header only.
- **DevicesView**: a horizontal `Viewport` of `DeviceCard`s, one per plugin on the track, plus an `AddEffectButton` that shows a `PopupMenu` (built-ins, VST3 submenu, Rescan). Today a card's Open button calls the existing per-effect window. The Vocal Chain later becomes a `RackDeviceCard`, a wider card showing the macro `Slider`s (RotaryVerticalDrag).
- **MixerView**: a horizontal `Viewport` of `ChannelStrip`s bound to the same track objects as `TrackHeader`. Use `te::LevelMeasurer` for meters when P3 lands.
- **BrowserPanel**: a `ListBox` of categories over a `TreeView` of items. The model is an interface, so presets can arrive later without UI changes. Hide/show with B.
- **PianoRoll / StepSequencer / Pads**: custom `Component`s inside `Viewport`s, with a shared horizontal zoom model with `TimelineView`, so the same ruler and grid code draws both.
- **Keyboard focus**: the main view owns focus by default, so transport shortcuts work everywhere. The DetailPanel takes focus when clicked, and editor keys (Q quantise, arrows on notes) apply there. Esc returns focus to the arrange. Text fields consume keys only while editing.
- **Tooltips**: one `TooltipWindow` at the root. Every icon button gets a tooltip that includes its shortcut ("Loop (L)").

## 5. Theme refinements

- **Dim text #8A8F99 → #A3A9B4.** The old value is only 4.48:1 on the panel colour. The new one is 6.2:1 on panels and 5.4:1 on raised buttons.
- **New tokens:** app bar `#202329`, raised controls `#2E323B`, borders `#3A3F4B`, status bar `#16181C`, selected row `#2F3A4A`, active toggle fill `#1D3550` with an accent border.
- **Clip colours:** each clip has a full-colour 15 px header and a darkened body (MIDI `#29493E`, audio `#283D55`), so notes and waveforms read clearly. Header text is picked by luminance: dark on green, white on blue (`Colour::contrasting`). White on the green header failed contrast (3.2:1).
- **Play button:** dark icon on `#2F9A5C`. A white icon there fails contrast.
- **Toolbar height:** the app bar is 40 and the transport is 44. Both were 34 before. This gives 28 px buttons and room for the larger position display.
- **Type:** Inter, base 12 px; names 12.5–13 px semibold; section labels 10 px uppercase with letter spacing.

## 6. Build order (shell first, features slot in)

1. **Shell refactor (no new features):** app bar + transport split, a `Layout` struct, `MainViewHost` with Arrange only, a collapsed `DetailPanel`, `BrowserPanel` with only Instruments and Effects (existing data), the new dim colour, the `planned` LookAndFeel flag with `PhaseChip`. Merge the setup dialogs into one tabbed Setup window. Add the output quick-switch.
2. **DevicesView** in the detail panel, replacing the FX popup as the main way in (keep the FX menu as a shortcut).
3. **2C** Piano roll in the Clip editor tab.
4. **2D** Drum machine as an instrument + step sequencer tab, with Drum kits in the browser.
5. **2E** Sections and tempo/key lanes above the ruler, and the time signature and key boxes in the transport.
6. **P3** Track meters, then Mix view, sends/buses, automation lanes, the new effects, then the Vocal Chain rack card.
7. **P4** Master view, Export dialog, then the presets that fill the existing browser categories, then the assistants.
8. **P5** Easy/Pro (hides routing columns, I/O selects, the chain preview and the inserts row), take lanes, punch, MIDI maps, wizard and tour.

Each step only fills a region that already exists, so the shell doesn't move again after step 1.

## Interface size and the design match (Nov 2026)

- The mockup's pixel sizes are CSS pixels. On a 2560 px wide screen at 100% Windows scaling they render tiny,
  so the whole UI scales: `UI/InterfaceScale` picks 110-175% from the screen width ("Automatic"), and the
  size button ("Aa", top bar), Ctrl+= / Ctrl+- / Ctrl+0 set it. Saved in the app settings, not the song.
- `Theme::font` never goes below `captionFontSize` (10 px); text buttons use 62% of their height (12 px on 20 px).
- Track header follows `track_header`: colour bar, type icon + name + device, FX; M/S/R ... A; fader + dB + pan + pan
  label; meter. Under 80 px tall it folds to two rows and drops read-outs before the fader gets shorter than 64 px.
- New tracks get the design's track colours in turn (buses grey). Clips follow `clip`: a coloured header
  strip with the name, a tinted body, a coloured border; audio blue and MIDI green unless the user picked a colour.
- M / S / Shift+R mute, solo and arm the highlighted track.
- Songs from before track colours get the palette when they open (outside undo); "None" is no longer kept across opens.
- Faders and knobs follow `hslider`, `strip` and `knob`: thin dark track with an accent fill and a light thumb; the mixer fader
  is a light cap with a dark centre line; knobs have a raised face, a border-coloured arc and an accent value arc.
- M on is amber, S on blue (dark text), R on red (white text), as in `msr`.
- Export window follows `export_dialog`: presets on the left (1-6 pick one), What / Range / Format / Loudness /
  Save to on the right (scrolls on short screens), estimate or progress in the footer. Enter exports, Esc stops a
  running export or closes. "Use the mastering chain" leaves the chain out of one export; "Save as preset" is P5.
  Ctrl+R or the Export button opens it.

## Master view (Nov 2026)
- Follows `master_view`: chain title row (Chain on/off, preset), the five cards EQ > Multiband > Stereo > Limiter > Dither
  (scroll sideways when narrow), Loudness (targets, four readouts, status + Match target + Reset, loudness over the song
  with sections and the target line), Reference A/B, Master assistant (P5, greyed), Main output row with Setup and Export.
- Keys: Tab / Shift+Tab cycle Arrange, Mix, Master; Ctrl+Shift+A chain on/off; Ctrl+Shift+F reference A/B.
- The mixer's master strip insert "Mastering chain" opens the Master view (right-click: open, switch on/off).
- The loudness target is the song's and is what Export starts with (also its dither choice).

## Browser: search, Samples & loops, Files (Nov 2026)
- Search (Ctrl+F) looks through instruments, effects, drum kits, Samples & loops and the open Files folder, a section
  each; Enter uses the first result, Esc clears. Picking a category clears the search.
- Samples & loops: "Add a folder of samples..." row, then My samples (Documents\EzDAWsIt\Samples), each added folder
  (right-click its header: look again, remove, show in Explorer) and the built-in drum sounds, kit by kit.
- Files: Places (Desktop, Documents, Music, Home, Downloads, drives), the open folder, "..", folders, then sounds.
- Sounds: click = preview on the main output (the speaker button in the footer turns that off), double-click = on the
  highlighted track at the playhead, drag = onto the timeline (any track, any time) or a drum pad, right-click = all of it
  plus "Load on a drum pad" and "Show in Explorer". Song templates, track presets and effect chains are still P4.

## Presets: song templates, track presets, effect chains (Nov 2026)
- Browser categories (no longer planned): a "Save ..." row on top, then Built-in and Yours. Double-click: chain on the
  highlighted track / preset as a new track / template as a new song (asks its name). Right-click: use, show in Explorer,
  delete (yours, to the Recycle Bin). Search finds them too.
- Track right-click menu (arrange and mixer): "Add an effect chain" submenu, "Save its effects as a chain...",
  "Save the track as a preset...".
- New Song (Ctrl+N) has "Start from": Empty song or any template.
- Files: Documents\EzDAWsIt\Presets\Effect chains (.ezchain), Track presets (.eztrack), Song templates (.eztemplate).

## Genres: guitars, bands, beats (Nov 2026)
- **Amp** (effect, "Guitar & bass"): Clean / Crunch / Punk / Lead / Metal / Bass, Gate (Off at the bottom), Gain, Tight,
  Bass / Mid / Treble / Presence, Cabinet (DI, 4x12, 2x12, 1x12, bass 8x10), Volume, Clean blend. The drive runs at 4x
  the sample rate. The track records the plain guitar (DI): change the amp after recording (re-amping for free).
- **Tuner** (Ctrl+U, App bar Tuner, track menu Tuner...): a window that follows the highlighted track, needs the track
  armed, puts a Tuner effect first on the track. Needle, note, cents, A = 430-450 Hz, Mute while tuning.
- Drum kits added: Garage Punk, Metal Kit, Hardcore Kit (appended: songs name kits by place).
- Track presets: 808 slide (trap, mono with glide), Reese bass (DnB), Guitar clean / crunch / metal rhythm / lead,
  Bass guitar, Bass hardcore grit, Rock vocal, Scream vocal, Gang vocals, Drums punk / metal / hardcore, Drum mic
  kick / snare / toms / overheads / room. Chains: Guitar metal rhythm, punk crunch, clean & shimmer, lead, Bass DI + amp,
  Vocal scream, Drums room smash. Templates: Punk, Hardcore, Metal (guitars doubled hard left / right), Rock band,
  Live band tracking (track n records input n when inputs are one per channel), Trap (sliding 808), DnB (Reese).
- Step sequencer: rolls (Alt+click a step cycles 1, 2, 3, 4, 6, 8 hits; step menu > Roll; ticks show the hits);
  12 and 24 steps are labelled triplets.

## Phase 5 (Nov 2026)
- **Punch**: P toggles; Ctrl+Shift+I / Ctrl+Shift+O (transport Punch in / Punch out) set the loop region's start or end
  at the playhead and switch punch on. Records only inside the loop region.
- **Takes**: recording over a loop keeps every pass. T / Shift+T next / previous take of the selected clips, clip menu
  > Takes, "Take 2/4" in the clip header. Comp by splitting (Ctrl+E) and picking a take per part.
- **Setup > Recording**: inputs one per channel (a band) or stereo pairs; a level meter per input with advice (too
  quiet / good / loud / clipping); recording delay fix (ms earlier); punch; how takes work.
- **Setup > MIDI & Controllers**: controllers heard; MIDI clock out per output (Sync button / Ctrl+Shift+Y);
  links. MIDI learn: Ctrl+Shift+L, touch a control, move a knob (saved with the song). Pads and buttons can run
  actions (Play / stop, Record, Loop, Click, Undo, Redo, Punch, takes, Tuner).
- **Export**: Save as preset (yours, for every song; right-click to delete).
- **Master assistant**: one tip at a time from the main meter (clipping, chain on, glue, mono bass for the club,
  match the target, compare with the reference fairly), with an Apply button (one undo step).
- **Easy | Pro** (Ctrl+Shift+P, saved): Easy hides the mixer's inputs/outputs and inserts rows, punch and Sync.
- **Help** (?): Take the tour (also on first start), Keyboard shortcuts (F1, searchable), open the log folder.

## Audio clips and tempo (Nov 2026)
- Fades: drag the handles on the line under a clip's header; F gives short fades to the selected clips. The faded part
  is shaded. Clip menu > Audio: fade in / out, clip gain, Normalise (Ctrl+Shift+N), Reverse (Ctrl+Alt+R), Transpose
  (Alt+Up / Alt+Down), Follow the song tempo (Ctrl+J: a loop's own tempo from the file, its name, or whole bars).
  Tags in the clip's corner show gain, Rev, transpose and Tempo.
- Ducking (sidechain): track menu > Duck under (sidechain) > a track. A compressor marked as the duck, keyed by that track.
- Tempo: K taps it in; right-click the BPM display for double / half time. Z zooms to fit the song; Ctrl+L loops the
  selected clips.

## Design system (v1.1 visual refresh)

All values live in `Source/UI/Theme.h`; components never hard-code colours or sizes.

**Surfaces** (each step is one layer nearer the user; never pure black):
`statusBar #0F1115` → `background #12141A` → `bar #161920` → `panel #1A1D24` → `raised #22262F` → `hover #2A2F3A`.
Recessed wells (readouts, graphs, text fields): `display #0D0F13`.

**Lines:** `hairline #2A2E38` between regions, `border #343A46` around controls, `laneSeparator #0C0E12` between lanes.

**Text:** `text #ECEEF2`, `textDim #9BA3B1` (secondary, 6.6:1), `textFaint #6B7383` (units, hints; large or non-essential only).

**Accent:** electric blue `#4D9FFF`, only for what is on, selected, focused or the next action. State colours:
mute `#F5B544`, solo `#4D9FFF`, record `#E5484D`, play `#30A46C`, success `#3DD68C`, warning `#F2B84C`.
Meters: green, amber from -6 dB, red at 0 dB.

**Radii:** 4 (chips, clips, small toggles), 6 (buttons, fields, menus), 8 (cards, strips), 10 (popups, dialogs).

**Spacing:** a 4 px grid: 4 / 8 / 12 / 16 / 24.

**Type:** Segoe UI Variable Text (Windows 11) or Segoe UI. Captions 10 px semibold UPPERCASE with tracking;
small 11; body 12.5; titles 13.5 semibold; readouts 19-24 semibold. Weights: regular for values, medium for
controls, semibold for names and headers.

**Depth:** cards = `drawCard()` (gradient, hairline, 1 px top highlight; the parent draws `Theme::drawShadow`
because a component's own shadow is clipped). Wells = `drawWell()`. A soft shadow sits under the transport and
over the timeline when the bottom panel is open.

**Clips** wear their track's colour (`ClipView::colourFor`, also used by the piano roll). Audio: zero line and a
filled, shaded wave. MIDI: faint pitch rows and rounded note bars. A glyph before the name says which.
Selected = lit tint, inner glow and a 1.5 px selection edge.

**Checking the look:** `EzDAWsIt.exe --screenshots <folder>` renders every view of a demo song at 1280x760 and
900x500, plus a menu and a message box. Use it after any visual change.
