/ KSynth Patch: Organ Drawbar approximation
/ Harmonics: 1, 2, 3, 4, 6, 8, 10, 12, 16
/ Corresponding to drawbars 16', 8', 5 1/3', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'

/ Setup wavetable size
N: 4096
P: ~ N

/ Amplitude vector (16 harmonics)
/  1   2   3   4   5   6   7   8   9   10  11  12  13  14  15  16
A: 1.0 0.8 0.6 0.5 0.0 0.4 0.0 0.3 0.0 0.2 0.0 0.1 0.0 0.0 0.0 0.1

/ Generate weighted additive synthesis (P $ A) and normalize (w)
W: w (P $ A)