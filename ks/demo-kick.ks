/ KSynth Patch: Synthesized 808-style Kick
/ Uses frequency sweep (chirp) and amplitude decay

/ 1. Setup Time (0.5 seconds at 44100Hz)
N: 22050
I: ! N             / Index vector 0..22049

/ 2. Normalized Ramp 0..1 for Envelopes
R: I % N

/ 3. Pitch Envelope (Frequency)
/ Sweep from 150Hz down to ~50Hz
/ 'x' verb is reserved, so use 'e' (exp) with scaling
E: e (R * -40.0)
F: 50 + (100 * E)

/ 4. Phase Accumulation (Integrate Frequency)
S: F * (6.28318 % 44100)
P: +\ S

/ 5. Oscillator & Amplitude Envelope
A: e (R * -10.0)  / Decay faster than pitch
O: (s P) * A

/ 6. Output (Normalize & Distort)
W: w (d O)