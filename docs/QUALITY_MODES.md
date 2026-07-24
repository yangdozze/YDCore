# GLOBUS quality modes

The global **QUALITY** selector (GLOBAL page) routes the v1.2 processing paths.
It is a normal automatable parameter, saved with presets and projects. Old
states and the 51 original presets contain no quality parameter and therefore
resolve to **LEGACY** — their sound cannot change by upgrading GLOBUS.

| | LEGACY | ECO | HIGH | ULTRA |
|---|---|---|---|---|
| Intent | exact 1.1 sound | big projects / live | default for new sounds | rendering / strong CPUs |
| Oscillator engines | selectable | selectable | selectable | selectable |
| Wavetable mip read | nearest level | nearest level | 2-level crossfade | 2-level crossfade |
| Ladder/OTA filter cores | 1× | 1× | 2× oversampled | 2× oversampled |
| Distortion shaper | 1.1 math | 1.1 math | 2× oversampled + DC blocker | 4× oversampled + DC blocker |
| Chorus/Delay interpolation | linear (1.1) | Catmull-Rom | Catmull-Rom | Catmull-Rom |
| Reverb | 1.1 algorithm | 1.1 algorithm | 8-line FDN | 8-line FDN |
| EQ gain changes | instant (1.1) | smoothed ~40 ms | smoothed ~40 ms | smoothed ~40 ms |
| Reported latency | 0 | 0 | **2 samples** | **4 samples** |

Notes, honestly stated:

- The reported latency covers the oversampled master distortion stage
  (measured impulse delay of the polyphase halfband chains). The per-voice
  filter oversampling runs inside the voice and adds ~2 samples of group delay
  there (≈0.04 ms) which is not host-compensated.
- ULTRA raises the distortion to 4×; the per-voice nonlinear filters stay at 2×
  (per-voice 4× would multiply CPU for marginal benefit — documented trade).
- Everything is prepared in `prepareToPlay`: switching quality mid-playback
  swaps prepared paths at the block boundary, never allocates on the audio
  thread, and the reverb engine swap fades its wet level back in (~50 ms) so
  the switch cannot click.
- CPU guidance (typical 8-voice pad, one instance): ECO ≈ LEGACY; HIGH adds
  roughly 10–25 % on the wavetable/filter path and the FDN reverb; ULTRA adds
  the 4× distortion on top. Exact figures vary by machine and patch — the CPU
  meter in the top bar always shows the real cost.
