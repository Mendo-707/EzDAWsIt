# EzDAWsIt release notes

## 1.1.0 (beta) - November 2026

A much bigger library, the EzDAW plug-ins built in, lag-free live playing, and a help guide that teaches as you go.

**A fuller browser**
- 14 categories in two columns: Favourites, Recent, Templates, Track presets, Effect chains, Sounds, Instruments,
  Effects, MIDI patterns, Drum kits, Samples, This song, Files and Learn. They fold to one row on small screens
- Star anything (the star on a row, or Ctrl+K); Recent keeps the last 30 things you used
- Effects show their presets under an arrow; search finds presets, patterns and help topics too
- Samples filter by loops, one-shots, drums, bass, keys, vocals, guitar and FX
- The selected sound's waveform with a moving playhead, loop preview and a preview volume
- Patterns, instruments and effects drag onto the timeline; Up/Down/Enter work from the search box

**More to start from**
- 57 MIDI patterns: chord progressions, basslines, melodies and arps, and drum beats from boom bap to blast beats.
  Melodic ones land in your song's key
- 34 synth sounds (bass, keys, pads, leads, plucks, brass and FX) and 112 effect presets, grouped in the preset menu
- 6 new drum kits (Techno, Classic 808, Afro, Pop, Soft Jazz, UK Garage), 17 new effect chains, 26 new track presets and
  10 new song templates (House, Techno, Pop, R&B, Afrobeats, UK garage, Synthwave, Ambient, Jazz trio, Voice-over)
- Load a sound onto a synth or an effect you already have (right-click it in the browser)

**The EzDAW plug-ins, built in**
- AETHER X1 Heritage (guitar amp, pedals and cab), StudioVoice Pro (vocal strip), and the instruments Aurelia 290,
  HexaFold, Voxalis, RIOT PRESS MK4, BASSED GOD and HOT LAVA 90 come with EzDAWsIt and sit with the built-in devices

**Lag-free live playing**
- Low-latency monitoring (Ctrl+Alt+L) switches off effects that add delay on tracks you aren't recording on
- The ASIO driver and a small buffer are picked on the first run; the status bar shows the delay you'll hear

**Help guide (F1)**
- 6 tutorials whose steps tick themselves as you do them, with "Show me" buttons: first beat, recording a voice,
  recording a guitar with no lag, chords and bass, mixing, mastering and export
- 17 guides and 9 troubleshooting pages, searchable. Keyboard shortcuts moved to Ctrl+/

**Also new**
- Freeze and bounce in place; an Arpeggiator; the master view fits small windows

**Fixed**
- The Compressor's built-in presets barely compressed (its ratio runs the other way): they now squeeze as named
- Undoing a preset now also puts its name back

**Known issues**
- The installer isn't code-signed, so Windows shows a SmartScreen warning the first time
- A plugin that crashes while *playing* (not scanning) still closes EzDAWsIt; autosave keeps your work
- Tested on one machine so far: please report problems with your audio interface or plugins

## 1.0.0 (beta) - November 2026

The first public release: a powerful but easy DAW for Windows, from the first take to the finished master.

**Record**
- Audio and MIDI recording with count-in, punch in/out, loop takes and a take picker
- A tuner, guitar and bass amps (clean to metal), and recording-delay correction
- Song templates for rock, punk, hardcore, metal, hip hop, trap, DnB and more

**Arrange and edit**
- Timeline with sections, tempo, time-signature and key changes, and snap
- Piano roll, Drum Machine with a step sequencer and rolls, a 4-oscillator synth and a sampler
- Clip fades, gain, reverse, normalise, transpose and follow-tempo; tap tempo

**Mix**
- Mixer with inserts, sends, FX returns, buses, sidechain ducking and peak/RMS meters
- Built-in effects (EQ, compressor, gate, de-esser, reverb, delay, saturation, pitch correction and more)
- VST3 plugin hosting, scanned in a separate process so a broken plugin can't crash the app
- Automation lanes (read/write) and MIDI learn for controllers

**Master and export**
- Mastering chain (EQ, multiband, stereo, limiter) with a master assistant
- Loudness metering for Spotify, Apple Music, YouTube, club and CD, and a reference track A/B
- Export to WAV, FLAC, OGG and MP3 with your own export presets

**Safety**
- Autosave and crash recovery; backups are never deleted
- After a run that didn't close properly, EzDAWsIt offers to show its log for a bug report
- Songs are stamped with their format: a song from a newer EzDAWsIt is refused, not damaged
- Help > Check for updates (asks GitHub only when you click it)

**Known issues**
- The installer isn't code-signed, so Windows shows a SmartScreen warning the first time
- A plugin that crashes while *playing* (not scanning) still closes EzDAWsIt; autosave keeps your work
- No track freeze yet: very heavy plugin sessions may need a larger audio buffer
- Tested on one machine so far: please report problems with your audio interface or plugins
