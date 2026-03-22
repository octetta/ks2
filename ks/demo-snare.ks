/ snare: tonal body + noise wires
/ based on the kick pattern in readme.md

N: 11025              / 250ms at 44100Hz
T: !N
C: p2%p0              / 2pi / sample_rate

/ 1. Tonal "Body"
/ Pitch sweep: starts at ~330Hz, drops quickly to 180Hz
F: 180 + 150*e(T*(0-20%N))
P: +\(N#(F*C))
/ Amplitude envelope: fast exponential decay
A: e(T*(0-12%N))
S: A * s P

/ 2. Noise "Wires"
R: r T
/ Highpass filter: subtract a lowpass (~700Hz) from the signal
L: 0.1 f R
H: R - L
/ Noise envelope: fast decay (snappy)
E: e(T*(0-25%N))
O: H * E

/ 3. Mix
/ Snare wires are usually prominent
W: w (S*0.6) + O
