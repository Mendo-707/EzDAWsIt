/*
    EzDAWsIt - Copyright (C) 2026 EzDAWsIt contributors. GNU GPL v3.
*/

#include "HelpGuide.h"
#include "CommandIDs.h"
#include "../Engine/DrumMachinePlugin.h"

namespace ez
{
    namespace HelpGuide
    {
        namespace
        {
            //==========================================================================
            // What the tutorials check in the song

            bool anyTrack (Session& s, std::function<bool (int)> test)
            {
                if (! s.hasSong())
                    return false;

                for (int t = 0; t < s.getNumAudioTracks(); ++t)
                    if (test (t))
                        return true;

                return false;
            }

            te::AudioTrack* trackAt (Session& s, int t)
            {
                return s.getEdit() != nullptr ? te::getAudioTracks (*s.getEdit())[t] : nullptr;
            }

            bool hasNotes (Session& s, int t)
            {
                if (auto* track = trackAt (s, t))
                    for (auto* clip : track->getClips())
                        if (auto* midi = dynamic_cast<te::MidiClip*> (clip); midi != nullptr && midi->getSequence().getNumNotes() > 0)
                            return true;

                return false;
            }

            bool hasAudioClip (Session& s, int t)
            {
                if (auto* track = trackAt (s, t))
                    for (auto* clip : track->getClips())
                        if (dynamic_cast<te::WaveAudioClip*> (clip) != nullptr)
                            return true;

                return false;
            }

            bool hasInstrument (Session& s, int t)
            {
                if (auto* track = trackAt (s, t))
                    for (auto* p : track->pluginList)
                        if (p->isSynth())
                            return true;

                return false;
            }

            bool drumsWithNotes (Session& s)     { return anyTrack (s, [&s] (int t) { return s.getDrumMachine (t) != nullptr && hasNotes (s, t); }); }
            bool melodicWithNotes (Session& s)   { return anyTrack (s, [&s] (int t) { return s.getDrumMachine (t) == nullptr && hasInstrument (s, t) && hasNotes (s, t); }); }
            bool anyArmed (Session& s)           { return anyTrack (s, [&s] (int t) { return s.isTrackArmed (t); }); }
            bool anyAudio (Session& s)           { return anyTrack (s, [&s] (int t) { return hasAudioClip (s, t); }); }
            bool anyEffects (Session& s)         { return anyTrack (s, [&s] (int t) { return ! s.getEffectInfo (t).isEmpty(); }); }
            bool anyLevelSet (Session& s)        { return anyTrack (s, [&s] (int t) { return std::abs (s.getTrackVolumeDb (t)) > 0.1f; }); }
            bool anyPanSet (Session& s)          { return anyTrack (s, [&s] (int t) { return std::abs (s.getTrackPan (t)) > 0.01f; }); }

            Step step (const char* text, std::function<bool (Session&)> done = nullptr, juce::CommandID command = 0, const char* label = "")
            {
                return { text, std::move (done), command, label };
            }

            std::vector<Topic> build()
            {
                std::vector<Topic> t;

                //======================================================================
                // Tutorials
                t.push_back ({ "first-beat", "Tutorials", "Make your first beat",
                    "Drums, a bassline and a loop, in about five minutes",
                    { "Each step ticks itself when you've done it. \"Show me\" does the first part for you.",
                      "# Tips",
                      "- In the drum editor, click a step to add a hit and click it again to remove it. Right-click a step for its velocity.",
                      "- Patterns from the browser land in your song's key. Set the key with the Key box in the transport." },
                    { step ("Have a song open: start a new one (Ctrl+N), or double-click the Hip hop beat template in the browser",
                            [] (Session& s) { return s.hasSong(); }, CommandIDs::newSong, "New song"),
                      step ("Add drums: in the browser, open Drum kits and double-click a kit (or MIDI patterns > Drum beats)",
                            [] (Session& s) { return anyTrack (s, [&s] (int i) { return s.getDrumMachine (i) != nullptr; }); },
                            CommandIDs::toggleBrowser, "Show the browser"),
                      step ("Give the drums a pattern: double-click a drum beat in MIDI patterns, or open the drum editor (E) and click steps",
                            drumsWithNotes),
                      step ("Loop it: press L (or drag in the lower half of the ruler)", [] (Session& s) { return s.isLooping(); },
                            CommandIDs::toggleLoop, "Loop on"),
                      step ("Press Space to play", [] (Session& s) { return s.isPlaying(); }, CommandIDs::togglePlay, "Play"),
                      step ("Add a bassline: MIDI patterns > Basslines, double-click one (it makes its own synth track)", melodicWithNotes) },
                    { "drums", "browser", "piano-roll" } });

                t.push_back ({ "record-voice", "Tutorials", "Record your voice",
                    "From a microphone to a polished vocal",
                    { "Wear headphones while you record, so the music doesn't leak into the microphone.",
                      "# If nothing comes in",
                      "- Check the input in Setup (Ctrl+,): the meter next to your microphone's input should move when you speak.",
                      "- A condenser microphone needs phantom power (48V) switched on at your interface." },
                    { step ("Have a song open", [] (Session& s) { return s.hasSong(); }, CommandIDs::newSong, "New song"),
                      step ("Check your microphone in Setup: speak and watch its meter", nullptr, CommandIDs::audioSetup, "Open Setup"),
                      step ("Add a vocal track: Track presets > Pop vocal (or Rap vocal) in the browser", anyEffects, CommandIDs::toggleBrowser, "Show the browser"),
                      step ("Arm the track: click its R button (or Shift+R)", anyArmed, CommandIDs::armTrack, "Arm it"),
                      step ("Press R to record, sing, then Space to stop", anyAudio, CommandIDs::toggleRecord, "Record"),
                      step ("Listen back: Home, then Space. Record over a loop to keep several takes (T switches between them)",
                            nullptr, CommandIDs::returnToStart, "Back to the start") },
                    { "recording", "latency-lag", "no-input" } });

                t.push_back ({ "record-guitar", "Tutorials", "Record a guitar with no lag",
                    "Amp sound on a DI guitar, with what you play coming straight back",
                    { "Plug the guitar into your interface's instrument (Hi-Z) input. The amp is in the computer.",
                      "With low-latency monitoring on, effects that add delay are switched off on every track you aren't recording on. "
                      "The status bar shows the delay you'll hear: under 10 ms feels instant." },
                    { step ("Have a song open", [] (Session& s) { return s.hasSong(); }, CommandIDs::newSong, "New song"),
                      step ("Add a guitar track: Track presets > Guitar: crunch (or any Guitar preset)", anyEffects, CommandIDs::toggleBrowser, "Show the browser"),
                      step ("Turn on low-latency monitoring (the bolt in the transport, Ctrl+Alt+L)",
                            [] (Session& s) { return s.isLowLatencyMonitoring(); }, CommandIDs::toggleLowLatency, "Turn it on"),
                      step ("Tune up: Ctrl+U opens the tuner on the highlighted track", nullptr, CommandIDs::showTuner, "Open the tuner"),
                      step ("Arm the track (Shift+R) and play: you hear the amp", anyArmed, CommandIDs::armTrack, "Arm it"),
                      step ("Record (R), stop (Space)", anyAudio, CommandIDs::toggleRecord, "Record") },
                    { "low-latency", "latency-lag", "crackles" } });

                t.push_back ({ "chords-bass", "Tutorials", "Add chords and a bassline",
                    "Ready-made progressions in your key, on built-in sounds",
                    { "Every chord progression and bassline in MIDI patterns is written from the key's root, and lands in the key set "
                      "at that point of the song. Major ones say \"(major)\", minor ones \"(minor)\": pick the one that matches your key.",
                      "Once a pattern is on a track, double-click its clip to change any note in the piano roll." },
                    { step ("Set the song's key: click the Key box in the transport", [] (Session& s) { return s.hasSong() && s.getKeyAt (te::TimePosition()).root >= 0; }),
                      step ("Add chords: MIDI patterns > Chord progressions, double-click one", melodicWithNotes, CommandIDs::toggleBrowser, "Show the browser"),
                      step ("Try another sound on them: Sounds in the browser, right-click a sound > Load on the synth", nullptr),
                      step ("Add a bassline: MIDI patterns > Basslines", [] (Session& s)
                            {
                                int withNotes = 0;
                                anyTrack (s, [&] (int i) { if (s.getDrumMachine (i) == nullptr && hasNotes (s, i)) ++withNotes; return false; });
                                return withNotes >= 2;
                            }),
                      step ("Play it with the loop on (L, then Space)", [] (Session& s) { return s.isPlaying() && s.isLooping(); }, CommandIDs::togglePlay, "Play") },
                    { "piano-roll", "browser", "presets" } });

                t.push_back ({ "mix-song", "Tutorials", "Mix your song",
                    "Levels, pan, a little EQ and compression, and a shared reverb",
                    { "Mix at a medium-low volume, and check now and then on headphones and a phone speaker.",
                      "# The order that works",
                      "- Balance first: get the faders right before any effect.",
                      "- Pan to make room: drums and bass in the middle, guitars and keys out to the sides.",
                      "- EQ to cut what's in the way (mud around 300 Hz), compression to even out, a shared reverb for one space." },
                    { step ("Open the mixer: Mix (Ctrl+2, or Tab)", nullptr, CommandIDs::showMixView, "Show the mixer"),
                      step ("Balance the faders: drag one, or double-click it for 0 dB", anyLevelSet),
                      step ("Pan something off-centre: drag a pan knob", anyPanSet),
                      step ("Add an EQ or compressor to a track: Effects in the browser (the arrow shows presets)", anyEffects),
                      step ("Add a shared reverb: + Reverb return (Ctrl+Shift+R), then turn up the sends",
                            [] (Session& s) { return ! s.getReturns().empty(); }, CommandIDs::addFxReturn, "Add a reverb return") },
                    { "mixing", "effects-guide", "automation" } });

                t.push_back ({ "master-export", "Tutorials", "Master and export",
                    "Loud, clean and ready for streaming",
                    { "Mastering is the last polish: a little EQ, gentle compression and a limiter so the song is as loud as other songs "
                      "without distorting. Play the whole song once so the loudness reading is right.",
                      "Streaming services turn songs down to about -14 LUFS, so louder isn't better past that." },
                    { step ("Open the Master view (Ctrl+3)", nullptr, CommandIDs::showMasterView, "Show the master"),
                      step ("Turn the mastering chain on (Ctrl+Shift+A)", [] (Session& s) { return s.isMasterChainOn(); }, CommandIDs::toggleMasterChain, "Turn it on"),
                      step ("Pick a preset: \"Gentle (start here)\" or \"Streaming, loud & clean\"", nullptr),
                      step ("Play the whole song and watch the loudness reading", [] (Session& s) { return s.isPlaying(); }, CommandIDs::togglePlay, "Play"),
                      step ("Export (Ctrl+R): a WAV for streaming, an MP3 to share", nullptr, CommandIDs::exportSong, "Export") },
                    { "mastering", "exporting", "too-quiet-loud" } });

                //======================================================================
                // Guides
                t.push_back ({ "screen", "Guides", "The screen at a glance", "What each part of the window is for",
                    { "# Along the top",
                      "- The app bar: New, Open and Save, the song's name (click it for recent songs), Arrange / Mix / Master (Tab), Export, "
                      "Setup, Easy or Pro, and Help.",
                      "- The transport: back to the start (Home), play (Space), record (R), loop (L), the bar and time, the tempo, the time "
                      "signature, the key, the click (C), count-in (Shift+C), low-latency monitoring, snap (G), undo and redo, and the meter.",
                      "# In the middle",
                      "- The browser on the left (B): everything you can add to the song.",
                      "- The main view: Arrange (tracks and clips), Mix (the mixer) or Master (the mastering chain).",
                      "- The bottom panel: the highlighted track's devices (D) or the clip you're editing (E).",
                      "# Along the bottom",
                      "- The status bar: what just happened, the delay you'll hear when you play live, the audio device and the CPU." },
                    {}, { "browser", "tracks" } });

                t.push_back ({ "browser", "Guides", "Using the browser", "Find, hear and add sounds, instruments, effects and patterns",
                    { "The categories sit in two columns at the top. The chevron by the title folds them into one row, so the list gets more room. "
                      "Ctrl+F searches everything at once, the help included.",
                      "# Adding things",
                      "- Double-click to use something: an instrument or sound gets a new track; an effect, chain or pattern goes on the "
                      "highlighted track; a sample goes on the highlighted track at the playhead.",
                      "- Drag samples, patterns, instruments and effects onto the timeline (on a track, or below the last one for a new track).",
                      "- Right-click anything for every option, like putting a sample on a drum pad or loading a sound onto a synth you already have.",
                      "# Finding things again",
                      "- The star at the right of a row (or Ctrl+K) keeps it in Favourites.",
                      "- Recent lists what you used last, newest first.",
                      "- Click a section title to fold it. In Effects, the arrow by an effect shows its presets.",
                      "# Hearing sounds",
                      "- Click a sample to hear it. The strip under the list shows its waveform and where it's playing.",
                      "- Underneath: preview on or off, loop the preview, and the preview volume.",
                      "- In Samples & loops, the chips filter loops, one-shots, drums, bass and more (from the file and folder names).",
                      "# Search keys",
                      "- Up and Down choose, Enter uses it, Shift+Enter puts it on a new track, Ctrl+Enter plays a sound, Esc clears." },
                    {}, { "presets", "drums" } });

                t.push_back ({ "tracks", "Guides", "Tracks and clips", "Adding, moving, cutting and arranging",
                    { "- Ctrl+T adds an audio track, Ctrl+Shift+T an instrument track. Up and Down change the highlighted track.",
                      "- Each track has M (mute), S (solo), R (arm for recording), FX (its effects) and A (automation).",
                      "- Drag a clip to move it. Drag its edges to trim it. Ctrl-drag copies it. Ctrl+D repeats it straight after itself.",
                      "- Ctrl+E splits at the playhead. F adds short fades so audio clips don't click.",
                      "- The tools in the toolbar (select, split, erase) change what a click does.",
                      "- Sections (Intro, Verse...) name parts of the song. Right-click the Sections row to add one.",
                      "- Freeze (Ctrl+Alt+F) renders a heavy track to audio so it stops using the computer. Bounce in place (Ctrl+Alt+B) "
                      "makes an audio copy you can edit." },
                    {}, { "screen", "arranging" } });

                t.push_back ({ "recording", "Guides", "Recording", "Inputs, arming, count-in, takes and punch-in",
                    { "- Arm the tracks you want to record on (R on the track, or Shift+R). Each records from its input: set it on the track "
                      "or in Routing (Ctrl+Alt+,).",
                      "- R starts recording, Space stops. Count-in (Shift+C) gives you a bar of click first.",
                      "- Record over a loop to get takes: each time round is a new take. T plays the next take, Shift+T the one before.",
                      "- Punch (P) records only inside the loop region, so you can fix one line without touching the rest.",
                      "- EzDAWsIt never saves in the middle of a recording. It saves straight after you stop." },
                    {}, { "low-latency", "no-input" } });

                t.push_back ({ "low-latency", "Guides", "Lag-free monitoring", "Hearing yourself without a delay",
                    { "When you play or sing through the computer you hear yourself a little late. Three things decide how late: the "
                      "audio driver, the buffer size and the effects on the way.",
                      "# What EzDAWsIt does",
                      "- On Windows it picks a low-latency driver the first time (your interface's ASIO driver if there is one) and a "
                      "small buffer.",
                      "- Low-latency monitoring (the bolt in the transport, Ctrl+Alt+L) switches off effects that add delay on every "
                      "track you aren't recording on. They come back when you switch it off.",
                      "- The status bar shows the delay you'll hear. Under 10 ms feels instant, and 10-20 ms is fine for most playing.",
                      "# If it still feels late",
                      "- In Setup (Ctrl+,), pick your interface's ASIO driver and a buffer of 64 or 128 samples.",
                      "- Use your interface's own direct monitoring, if it has it, while you record vocals." },
                    {}, { "latency-lag", "crackles" } });

                t.push_back ({ "midi", "Guides", "MIDI keyboards and pads", "Playing instruments and linking controls",
                    { "- Plug the keyboard in before starting EzDAWsIt, or switch it on in Setup > MIDI.",
                      "- A new instrument track plays from every MIDI input straight away.",
                      "- MIDI learn (Ctrl+Shift+L): click a control in EzDAWsIt, then move a knob or fader on your controller.",
                      "- Send MIDI clock (Ctrl+Shift+Y) makes a drum machine or groovebox follow the tempo, play and stop." },
                    {}, { "midi-not-working", "piano-roll" } });

                t.push_back ({ "piano-roll", "Guides", "Editing notes (piano roll)", "Drawing, moving and fixing MIDI notes",
                    { "Double-click a MIDI clip (or select it and press E) to open it in the bottom panel.",
                      "- Draw notes with the pencil, or double-click in the grid. Drag a note to move it, or its end to change its length.",
                      "- Delete removes the selected notes. Ctrl+A selects all of them. Ctrl+C, X and V copy, cut and paste.",
                      "- Q quantises the selection to the grid. Arrows nudge it (Shift for bigger steps).",
                      "- Ctrl + mouse wheel zooms across, Alt + mouse wheel zooms up and down. Scale highlighting shows your key." },
                    {}, { "chords-bass", "drums" } });

                t.push_back ({ "drums", "Guides", "The Drum Machine", "16 pads, kits and the step sequencer",
                    { "- Drum kits in the browser load a whole kit. Drag any sample onto a pad to replace that sound.",
                      "- Open a drum clip (E) to see the steps. Click to add a hit, click again to remove it.",
                      "- Each pad has its own level, pitch, decay and choke group (an open hat cuts off when the closed hat plays).",
                      "- MIDI patterns > Drum beats has ready-made beats from boom bap to blast beats." },
                    {}, { "first-beat", "browser" } });

                t.push_back ({ "arranging", "Guides", "Arranging a song", "From a loop to a whole song",
                    { "- Start with a loop of 4 or 8 bars that you like.",
                      "- Copy it across (Ctrl+D) to make the length of the song, then take parts out: the intro without drums, the "
                      "verse without the lead, everything in the chorus.",
                      "- Name sections (Intro, Verse, Chorus) so you can see the shape.",
                      "- Zoom to fit (Z) shows the whole song. Follow (Shift+F) pages along as it plays." },
                    {}, { "tracks" } });

                t.push_back ({ "mixing", "Guides", "Mixing basics", "Balance, pan, EQ, compression, space",
                    { "# Balance",
                      "Start with every fader down a little, the most important part (usually the vocal or kick) at about -6 dB, and "
                      "bring the rest up around it.",
                      "# Pan",
                      "Keep kick, snare, bass and lead vocal in the middle. Spread guitars, keys, backing vocals and percussion.",
                      "# EQ",
                      "Cut before you boost. Low/high cut takes rumble out of everything but the bass and kick. A small cut around 300 Hz "
                      "clears mud, and a lift around 3-5 kHz brings a vocal forward.",
                      "# Compression",
                      "Evens out loud and quiet parts. Start with a preset (Vocal, Punchy drums) and turn the threshold until it just works.",
                      "# Space",
                      "Use a reverb return (Ctrl+Shift+R) and send tracks to it, so everything shares one room.",
                      "# Buses",
                      "Put the drums (or backing vocals) in a bus (Ctrl+Shift+G) to turn the whole group with one fader." },
                    {}, { "mix-song", "effects-guide" } });

                t.push_back ({ "effects-guide", "Guides", "What each effect does", "A one-line guide to every built-in effect",
                    { "- EQ: shapes the tone. Cut mud, add air.",
                      "- Compressor: evens out loud and quiet. Limiter: nothing gets past the ceiling.",
                      "- Gate: silence between notes, against hiss and spill. De-esser: tames harsh \"s\" sounds.",
                      "- Low/high cut: removes rumble below and hiss above. Filter: a sweepable low- or high-pass.",
                      "- Reverb: a room. Delay: echoes in time. Chorus and Phaser: movement and width.",
                      "- Saturator: warmth and grit. Amp: a guitar or bass amp with a cabinet.",
                      "- Tune: pulls singing to the key, from natural to robotic. Vocal Chain: a whole vocal sound on six macros.",
                      "- Tremolo: a pulsing level or an auto-pan. Utility: gain, width, mono and phase.",
                      "- Arpeggiator: plays held chords as patterns (it goes before an instrument).",
                      "Every effect has presets: the arrow in the browser, or the preset menu at the top of its window." },
                    {}, { "mixing", "presets" } });

                t.push_back ({ "automation", "Guides", "Automation", "Controls that move by themselves",
                    { "- A shows a track's automation lanes. Draw points with the mouse, or drag a line.",
                      "- Write (W): move a control while the song plays and the movement is recorded.",
                      "- Read (Shift+W): the lanes move their controls as the song plays." },
                    {}, { "mixing" } });

                t.push_back ({ "mastering", "Guides", "Mastering", "The last polish on the whole song",
                    { "The Master view (Ctrl+3) has a four-part chain: EQ, a three-band compressor that glues the mix, stereo width, and "
                      "a limiter. Its presets are good starting points.",
                      "- Loudness: play the whole song so the integrated reading is right, then set the limiter to reach the target.",
                      "- Reference: load a song you like and switch between it and yours (Ctrl+Shift+F) at the same loudness.",
                      "- Ctrl+Shift+A switches the chain off and on, so you can hear what it's doing." },
                    {}, { "master-export", "exporting" } });

                t.push_back ({ "exporting", "Guides", "Exporting", "WAV, MP3, stems and MIDI",
                    { "- Export (Ctrl+R): the full song, the loop region, or each track as its own file (stems).",
                      "- WAV at 24-bit is best for uploading. MP3 is for sharing quickly.",
                      "- Match loudness sets the gain so the file lands on your target without going over the ceiling.",
                      "- Exports go to Documents\\EzDAWsIt\\Exports unless you choose another folder." },
                    {}, { "mastering", "too-quiet-loud" } });

                t.push_back ({ "plugins", "Guides", "Plug-ins", "The EzDAW plug-ins, and your own VST3s",
                    { "EzDAWsIt comes with eight EzDAW plug-ins, listed with the built-ins: AETHER X1 Heritage (guitar amp, pedals and "
                      "cab, under Guitar & bass), StudioVoice Pro (vocal channel strip, under Chains), and the instruments Aurelia 290, "
                      "HexaFold, Voxalis, RIOT PRESS MK4 (bass), BASSED GOD (808 basslines) and HOT LAVA 90 (leads and melodies).",
                      "BASSED GOD and HOT LAVA 90 play their own pattern while the song plays. Notes you play on their track move the "
                      "pattern to that key (Smart Root); switch Smart Root off to play them like an ordinary synth.",
                      "- Your own VST3 plug-ins: Setup > Plugins (Ctrl+Alt+P) scans them. They appear under VST3 in Instruments and Effects.",
                      "- Each plug-in opens in its own window. Its settings are saved with the song.",
                      "- If a plug-in adds delay, low-latency monitoring switches it off while you record on another track." },
                    {}, { "plugin-fails" } });

                t.push_back ({ "presets", "Guides", "Presets, chains and templates", "Saving and reusing your sounds",
                    { "- An effect or synth preset: the preset menu at the top of its window. Built-in ones are grouped (Bass, Keys, Pad...), "
                      "and yours come after them.",
                      "- An effect chain: every effect on a track, in order. Save one from the browser or by right-clicking a track.",
                      "- A track preset: the instrument, effects, colour, level and pan.",
                      "- A song template: every track, send, the tempo and the master, without recordings.",
                      "Yours are saved in Documents\\EzDAWsIt\\Presets." },
                    {}, { "browser" } });

                t.push_back ({ "shortcuts-guide", "Guides", "The shortcuts you'll use most", "The ten keys worth learning first",
                    { "- Space: play and stop. R: record. Home: back to the start. L: loop.",
                      "- Ctrl+Z and Ctrl+Y: undo and redo. Ctrl+S: save.",
                      "- B: the browser. D: the devices panel. E: the clip editor. Tab: Arrange, Mix, Master.",
                      "- Ctrl+F: search. Ctrl+K: star the selected browser item.",
                      "- Ctrl+/ lists every shortcut. F1 opens this guide." },
                    {}, {} });

                t.push_back ({ "saving", "Guides", "Saving and backups", "Where your songs live and how they're kept safe",
                    { "- Songs are saved in Documents\\EzDAWsIt\\Projects, one folder per song, with their recordings next to them.",
                      "- EzDAWsIt autosaves every minute (never while recording). If the app closes unexpectedly, it offers the autosave "
                      "the next time you start it.",
                      "- Save as new song (Ctrl+Shift+S) copies the song and its recordings under a new name." },
                    {}, { "recovered-song" } });

                //======================================================================
                // Troubleshooting
                t.push_back ({ "no-sound", "Troubleshooting", "I can't hear anything", "No sound when you press play",
                    { "- Look at the output box at the top. \"No audio output\" means pick one: click it, or open Setup (Ctrl+,).",
                      "- Check that the tracks aren't muted (M) and that nothing is soloed (S) by mistake.",
                      "- Watch the meter on the right of the transport. If it moves but you hear nothing, the sound is going to another "
                      "output: Routing (Ctrl+Alt+,) plays a test beep on each one.",
                      "- On Windows, another program can hold the device. Close other audio apps, or pick Windows Audio in Setup." },
                    {}, { "no-input" } });

                t.push_back ({ "latency-lag", "Troubleshooting", "There's a delay when I play or sing", "Hearing yourself late",
                    { "- Turn on low-latency monitoring (Ctrl+Alt+L).",
                      "- Look at the delay in the status bar and click it for advice.",
                      "- In Setup (Ctrl+,), choose your interface's ASIO driver and a smaller buffer (64-128 samples).",
                      "- Effects that look ahead (Tune, some plug-ins) add delay. Low-latency monitoring switches them off on other tracks.",
                      "- Use your interface's direct monitoring if it has it." },
                    {}, { "low-latency", "crackles" } });

                t.push_back ({ "crackles", "Troubleshooting", "Crackles, pops or dropouts", "The computer can't keep up",
                    { "- Raise the buffer size in Setup (256 or 512) while mixing. Lower it again for recording.",
                      "- Freeze heavy tracks (Ctrl+Alt+F): they play as audio and stop using the computer.",
                      "- Watch CPU in the status bar. Close other programs, and plug a laptop into power.",
                      "- Use the same sample rate as your interface (usually 44.1 or 48 kHz)." },
                    {}, { "latency-lag" } });

                t.push_back ({ "no-input", "Troubleshooting", "My microphone or guitar isn't picked up", "Nothing comes in when you record",
                    { "- Is the track armed (R)? Is its input the right one? Click the input on the track, or open Routing (Ctrl+Alt+,).",
                      "- In Setup (Ctrl+,), the input should be switched on and its meter should move.",
                      "- Windows privacy settings can block microphones: Settings > Privacy > Microphone > let desktop apps use it.",
                      "- A condenser microphone needs 48V phantom power. A guitar needs the Hi-Z / instrument input." },
                    {}, { "record-voice", "recording" } });

                t.push_back ({ "plugin-fails", "Troubleshooting", "A plug-in won't load or crashes", "VST3 problems",
                    { "- Rescan in Setup > Plugins (Ctrl+Alt+P). EzDAWsIt scans plug-ins in a separate process, so a broken one can't take "
                      "the app down.",
                      "- A song with a missing plug-in still opens, and keeps the plug-in's settings until it's installed again.",
                      "- Only 64-bit VST3 plug-ins work.",
                      "- Help > Open the log folder shows what went wrong: send the newest log with a bug report." },
                    {}, { "plugins" } });

                t.push_back ({ "midi-not-working", "Troubleshooting", "My MIDI keyboard doesn't play", "No notes from the keyboard",
                    { "- Plug the keyboard in, then check Setup > MIDI: its input should be on.",
                      "- Highlight (and arm) an instrument track. Audio tracks don't play MIDI.",
                      "- If another app has the keyboard open, close it: Windows lets only one app use some MIDI devices." },
                    {}, { "midi" } });

                t.push_back ({ "recovered-song", "Troubleshooting", "The app closed: getting my song back", "Autosaves and backups",
                    { "- Start EzDAWsIt again. If there's a newer autosave than the saved song, it asks whether to open it.",
                      "- Autosaves are kept beside the song as .tmp_<Song>. EzDAWsIt never deletes one: old ones are renamed aside with a date.",
                      "- Recordings are written straight to the song folder, so they're there even if the song wasn't saved." },
                    {}, { "saving" } });

                t.push_back ({ "too-quiet-loud", "Troubleshooting", "My export is too quiet or too loud", "Loudness",
                    { "- Turn on the mastering chain and play the whole song once so the loudness reading is complete.",
                      "- In Export, Match loudness sets the gain to your target (-14 LUFS suits streaming).",
                      "- If it distorts, the ceiling is too high or the limiter is pushed too hard: use a ceiling of -1 dB and less gain." },
                    {}, { "mastering", "exporting" } });

                return t;
            }
        }

        const std::vector<Topic>& topics()
        {
            static const std::vector<Topic> all = build();
            return all;
        }

        const Topic* find (const juce::String& id)
        {
            for (const auto& t : topics())
                if (t.id == id)
                    return &t;

            return nullptr;
        }

        juce::StringArray sections()
        {
            return { "Tutorials", "Guides", "Troubleshooting" };
        }

        bool matches (const Topic& t, const juce::String& query)
        {
            juce::StringArray words;
            words.addTokens (query, " ", "\"");
            words.removeEmptyStrings();

            const auto text = t.title + " " + t.summary + " " + t.body.joinIntoString (" ");

            for (const auto& w : words)
                if (! text.containsIgnoreCase (w))
                    return false;

            return true;
        }
    }

    //==============================================================================
    /** A topic: title, summary, the steps (tutorials), then the text and links to related topics. */
    class HelpGuideView::Article : public juce::Component
    {
    public:
        Article (HelpGuideView& o) : owner (o) {}

        void setTopic (const HelpGuide::Topic* t)
        {
            topic = t;
            done.assign (t != nullptr ? t->steps.size() : 0, false);
            buttons.clear();
            links.clear();

            if (topic != nullptr)
            {
                for (size_t i = 0; i < topic->steps.size(); ++i)
                {
                    const auto& s = topic->steps[i];

                    if (s.command == 0)
                    {
                        buttons.add (nullptr);
                        continue;
                    }

                    auto* b = buttons.add (new juce::TextButton (s.commandLabel.isNotEmpty() ? s.commandLabel : juce::String ("Show me")));
                    b->setTooltip ("Does it for you: " + s.commandLabel);
                    b->setWantsKeyboardFocus (false);
                    b->onClick = [this, command = s.command] { owner.commands.invokeDirectly (command, true); };
                    addAndMakeVisible (b);
                }

                for (const auto& id : topic->seeAlso)
                    if (const auto* other = HelpGuide::find (id))
                    {
                        auto* b = links.add (new juce::TextButton (other->title));
                        b->setTooltip (other->summary);
                        b->setWantsKeyboardFocus (false);
                        b->onClick = [this, id] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<HelpGuideView> (&owner), id]
                                                                                  { if (safe != nullptr) safe->showTopic (id); }); };
                        addAndMakeVisible (b);
                    }
            }

            check();
            layout (getWidth());
        }

        /** Ticks what's been done (and stays ticked). True if anything changed. */
        bool check()
        {
            if (topic == nullptr)
                return false;

            bool changed = false;

            for (size_t i = 0; i < topic->steps.size(); ++i)
                if (! done[i] && topic->steps[i].isDone != nullptr && topic->steps[i].isDone (owner.session))
                {
                    done[i] = true;
                    changed = true;
                }

            if (changed)
                repaint();

            return changed;
        }

        int layout (int width)
        {
            stepAreas.clear();
            paragraphs.clear();
            int y = 34;   // room for the section above the title
            const auto textWidth = juce::jmax (200, width - 48);

            if (topic == nullptr)
            {
                setSize (width, 100);
                return 100;
            }

            titleArea = { 24, y, textWidth, 30 };
            y += 30;
            summaryArea = { 24, y, textWidth, 22 };
            y += 34;

            for (size_t i = 0; i < topic->steps.size(); ++i)
            {
                const auto buttonWidth = buttons[(int) i] != nullptr ? 150 : 0;
                const auto text = makeText (juce::String ((int) i + 1) + ".  " + topic->steps[i].text, false, false, stepTextWidth (textWidth, buttonWidth));
                const auto h = juce::jmax (34, juce::roundToInt (text.getHeight()) + 14);
                stepAreas.push_back ({ 24, y, textWidth, h });

                if (auto* b = buttons[(int) i])
                    b->setBounds (24 + textWidth - buttonWidth + 2, y + (h - 26) / 2, buttonWidth - 10, 26);   // inside the step's card, not on its edge

                y += h + 6;
            }

            if (! topic->steps.empty())
                y += 14;

            for (const auto& p : topic->body)
            {
                const bool heading = p.startsWith ("# ");
                const bool bullet = p.startsWith ("- ");
                const auto indent = bullet ? 16 : 0;
                const auto text = makeText (heading || bullet ? p.substring (2) : p, heading, false, (float) (textWidth - indent));
                const auto h = juce::roundToInt (text.getHeight()) + (heading ? 12 : 6);
                y += heading ? 8 : 0;
                paragraphs.push_back ({ text, { 24 + indent, y, textWidth - indent, h }, bullet });
                y += h + 4;
            }

            if (! links.isEmpty())
            {
                y += 10;
                seeAlsoY = y;
                y += 24;
                int x = 24;

                for (auto* b : links)
                {
                    const auto w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (Theme::font (Theme::baseFontSize), b->getButtonText())) + 28;

                    if (x > 24 && x + w > 24 + textWidth)
                    {
                        x = 24;
                        y += 32;
                    }

                    b->setBounds (x, y, w, 26);
                    x += w + 8;
                }

                y += 40;
            }

            setSize (width, y + 20);
            return y + 20;
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Theme::background);

            if (topic == nullptr)
                return;

            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::smallFontSize, Theme::Weight::semibold));
            g.drawText (topic->section.toUpperCase(), titleArea.translated (0, -16).withHeight (14), juce::Justification::centredLeft, false);

            g.setColour (Theme::text);
            g.setFont (Theme::font (Theme::baseFontSize + 7.0f, Theme::Weight::semibold));
            g.drawText (topic->title, titleArea, juce::Justification::centredLeft, true);
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (Theme::baseFontSize));
            g.drawText (topic->summary, summaryArea, juce::Justification::centredLeft, true);

            int allDone = 0;

            for (size_t i = 0; i < stepAreas.size(); ++i)
            {
                const auto area = stepAreas[i];
                g.setColour (done[i] ? Theme::accent.withAlpha (0.12f) : Theme::panel);
                g.fillRoundedRectangle (area.toFloat(), (float) Theme::cornerRadius);

                auto box = juce::Rectangle<float> ((float) area.getX() + 10.0f, (float) area.getCentreY() - 9.0f, 18.0f, 18.0f);
                g.setColour (done[i] ? Theme::accent : Theme::laneSeparator.brighter (0.3f));

                if (done[i])
                {
                    g.fillEllipse (box);
                    juce::Path tick;
                    tick.startNewSubPath (box.getX() + 4.5f, box.getCentreY());
                    tick.lineTo (box.getX() + 8.0f, box.getBottom() - 5.0f);
                    tick.lineTo (box.getRight() - 4.0f, box.getY() + 5.0f);
                    g.setColour (Theme::background);
                    g.strokePath (tick, juce::PathStrokeType (2.0f));
                    ++allDone;
                }
                else
                {
                    g.drawEllipse (box, 1.5f);
                }

                const auto buttonWidth = buttons[(int) i] != nullptr ? 150 : 0;
                const auto text = makeText (juce::String ((int) i + 1) + ".  " + topic->steps[i].text, false, done[i],
                                            stepTextWidth (area.getWidth(), buttonWidth));
                text.draw (g, area.withTrimmedLeft (38).withTrimmedRight (buttonWidth + 8).reduced (0, 7).toFloat());
            }

            if (! stepAreas.empty() && allDone == (int) stepAreas.size())
            {
                g.setColour (Theme::accent);
                g.setFont (Theme::font (Theme::baseFontSize, Theme::Weight::semibold));
                g.drawText ("Done! Every step is ticked.", juce::Rectangle<int> (24, stepAreas.back().getBottom() + 4, getWidth() - 48, 18),
                            juce::Justification::centredLeft, false);
            }

            for (const auto& p : paragraphs)
            {
                if (p.bullet)
                {
                    g.setColour (Theme::accent);
                    g.fillEllipse ((float) p.area.getX() - 11.0f, (float) p.area.getY() + 8.0f, 5.0f, 5.0f);
                }

                p.text.draw (g, p.area.toFloat());
            }

            if (! links.isEmpty())
            {
                g.setColour (Theme::textDim);
                g.setFont (Theme::font (Theme::smallFontSize, Theme::Weight::semibold));
                g.drawText ("SEE ALSO", 24, seeAlsoY, 200, 18, juce::Justification::centredLeft, false);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            // A step with nothing to check ticks by hand
            if (topic == nullptr)
                return;

            for (size_t i = 0; i < stepAreas.size(); ++i)
                if (stepAreas[i].contains (e.getPosition()) && e.x < stepAreas[i].getX() + 40)
                {
                    done[i] = ! done[i];
                    repaint();
                }
        }

        const HelpGuide::Topic* topic = nullptr;
        std::vector<bool> done;

    private:
        struct Paragraph { juce::TextLayout text; juce::Rectangle<int> area; bool bullet; };

        HelpGuideView& owner;
        juce::OwnedArray<juce::TextButton> buttons, links;
        std::vector<juce::Rectangle<int>> stepAreas;
        std::vector<Paragraph> paragraphs;
        juce::Rectangle<int> titleArea, summaryArea;
        int seeAlsoY = 0;

        static juce::AttributedString attributed (const juce::String& s, bool heading, bool dim)
        {
            juce::AttributedString a;
            a.setText (s);
            a.setFont (heading ? Theme::font (Theme::baseFontSize + 2.0f, Theme::Weight::semibold) : Theme::font (Theme::baseFontSize));
            a.setColour (dim ? Theme::textDim : Theme::text);
            a.setWordWrap (juce::AttributedString::byWord);
            a.setLineSpacing (3.0f);
            return a;
        }

        static float stepTextWidth (int areaWidth, int buttonWidth)   { return (float) juce::jmax (80, areaWidth - 38 - buttonWidth - 8); }

        static juce::TextLayout makeText (const juce::String& s, bool heading, bool dim, float width)
        {
            juce::TextLayout layoutText;
            layoutText.createLayout (attributed (s, heading, dim), juce::jmax (80.0f, width));
            return layoutText;
        }
    };

    //==============================================================================
    HelpGuideView::HelpGuideView (Session& s, juce::ApplicationCommandManager& c)
        : session (s), commands (c)
    {
        search.setTextToShowWhenEmpty ("Search the help", Theme::textDim);
        search.setJustification (juce::Justification::centredLeft);
        search.setIndents (10, 0);
        search.setTooltip ("Finds every topic with all these words (Esc clears)");
        search.onTextChange = [this] { refilter(); };
        search.onEscapeKey = [this] { search.clear(); refilter(); };
        addAndMakeVisible (search);

        list.setModel (this);
        list.setRowHeight (30);
        list.setOutlineThickness (0);
        list.setColour (juce::ListBox::backgroundColourId, Theme::panel);
        addAndMakeVisible (list);

        article = std::make_unique<Article> (*this);
        articleView.setViewedComponent (article.get(), false);
        articleView.setScrollBarsShown (true, false);
        addAndMakeVisible (articleView);

        setSize (preferredWidth, preferredHeight);
        refilter();
        showTopic (HelpGuide::topics().front().id);
        startTimerHz (4);
    }

    HelpGuideView::~HelpGuideView()
    {
        list.setModel (nullptr);
    }

    void HelpGuideView::refilter()
    {
        rows.clear();
        const auto query = search.getText().trim();

        for (const auto& section : HelpGuide::sections())
        {
            bool heading = false;

            for (const auto& t : HelpGuide::topics())
                if (t.section == section && (query.isEmpty() || HelpGuide::matches (t, query)))
                {
                    if (! std::exchange (heading, true))
                        rows.push_back ({ true, section, {} });

                    rows.push_back ({ false, t.title, t.id });
                }
        }

        if (rows.empty())
            rows.push_back ({ true, "Nothing matches", {} });

        list.updateContent();
        list.repaint();
    }

    void HelpGuideView::showTopic (const juce::String& id)
    {
        const auto* topic = HelpGuide::find (id);

        if (topic == nullptr)
            return;

        shown = id;
        article->setSize (articleView.getMaximumVisibleWidth() > 0 ? articleView.getMaximumVisibleWidth() : preferredWidth - 260, article->getHeight());
        article->setTopic (topic);
        articleView.setViewPosition (0, 0);
        list.repaint();
    }

    void HelpGuideView::timerCallback()
    {
        article->check();
    }

    void HelpGuideView::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()))
            return;

        const auto& r = rows[(size_t) row];
        auto area = juce::Rectangle<int> (width, height).reduced (6, 1);

        if (r.isHeading)
        {
            drawCaption (g, r.text, area.withTrimmedLeft (4).withTrimmedBottom (3), juce::Justification::bottomLeft);
            return;
        }

        if (r.id == shown)
        {
            g.setColour (Theme::selectedRow);
            g.fillRoundedRectangle (area.toFloat(), (float) Theme::cornerRadius);
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (juce::Rectangle<float> ((float) area.getX() + 2.0f, (float) area.getCentreY() - 6.0f, 3.0f, 12.0f), 1.5f);
        }

        const auto* topic = HelpGuide::find (r.id);
        const auto icon = topic != nullptr && ! topic->steps.empty() ? Icon::wand : topic != nullptr && topic->section == "Troubleshooting" ? Icon::help : Icon::book;
        Icons::draw (g, icon, area.removeFromLeft (30).toFloat().withSizeKeepingCentre (13.0f, 13.0f), r.id == shown ? Theme::accent : Theme::textDim, 1.6f);
        g.setColour (Theme::text);
        g.setFont (Theme::font (Theme::baseFontSize - 0.5f, r.id == shown ? Theme::Weight::semibold : Theme::Weight::regular));
        g.drawText (r.text, area, juce::Justification::centredLeft, true);
    }

    void HelpGuideView::listBoxItemClicked (int row, const juce::MouseEvent&)
    {
        if (juce::isPositiveAndBelow (row, (int) rows.size()) && ! rows[(size_t) row].isHeading)
            showTopic (rows[(size_t) row].id);
    }

    void HelpGuideView::paint (juce::Graphics& g)
    {
        g.fillAll (Theme::background);
        g.setColour (Theme::panel);
        g.fillRect (getLocalBounds().removeFromLeft (list.getRight()));
        g.setColour (Theme::laneSeparator);
        g.drawVerticalLine (list.getRight(), 0.0f, (float) getHeight());
    }

    void HelpGuideView::resized()
    {
        auto r = getLocalBounds();
        auto left = r.removeFromLeft (juce::jlimit (200, 280, getWidth() / 3));
        search.setBounds (left.removeFromTop (44).reduced (10, 9));
        list.setBounds (left);
        articleView.setBounds (r.withTrimmedLeft (1));

        article->layout (juce::jmax (300, articleView.getMaximumVisibleWidth()));   // the ticks stay
    }

    //==============================================================================
    namespace
    {
        class HelpWindow : public juce::DocumentWindow
        {
        public:
            HelpWindow (HelpGuideView* content, std::function<void()> closed)
                : juce::DocumentWindow ("Help guide & tutorials", Theme::panel, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
                  onClosed (std::move (closed))
            {
                setUsingNativeTitleBar (true);
                setContentOwned (content, true);
                setResizable (true, false);
                setResizeLimits (640, 420, 2000, 1600);
                centreWithSize (getWidth(), getHeight());
            }

            void closeButtonPressed() override
            {
                if (onClosed != nullptr)
                    juce::MessageManager::callAsync ([cb = onClosed] { cb(); });   // the owner deletes us, after this returns
            }

        private:
            std::function<void()> onClosed;
        };
    }

    std::unique_ptr<juce::DocumentWindow> createHelpGuideWindow (Session& session, juce::ApplicationCommandManager& commands,
                                                                 const juce::String& topicId, std::function<void()> onClose)
    {
        auto* view = new HelpGuideView (session, commands);

        if (topicId.isNotEmpty())
            view->showTopic (topicId);

        return std::make_unique<HelpWindow> (view, std::move (onClose));
    }
}
