# EzDAWsIt release notes

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
