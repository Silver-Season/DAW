# Cyber bass demo

[Listen or download the WAV](cyberbass-demo.wav?raw=true).

Four factory programs from the existing Prisma synth, in this order:

1. Neon Grid Bass
2. Razor Bass
3. Chrome Wobble Bass
4. Overload Bass

Each program plays the same 16-beat phrase at 140 BPM, followed by 0.8 seconds for release. The phrase uses MIDI notes 43, 48, 51, 55, 58 and 60. The final section overlaps notes to expose mono/legato and glide behavior. Velocity alternates between 115 on accents and 100 otherwise.

Rendered offline by `daw::synth::PrismaSynth` at 44.1 kHz into a stereo 16-bit PCM WAV, using only the stored factory parameters. No external effects, normalization or mastering. Peaks: Neon Grid −12.57 dBFS, Razor −13.09 dBFS, Chrome Wobble −13.81 dBFS, Overload −12.85 dBFS. Levels describe the rendered left channel.

The full Grieg arrangement made with the app is in `media/demo-mountain-king.mp3`.
