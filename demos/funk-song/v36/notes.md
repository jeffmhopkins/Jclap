# Funk song v36

v35 plus two fixes.

- **Fills:** JDrummer's fills start soft and crescendo, so the snare dropped out under beat 4 where the backbeat was. In the bar before each section change, the snare and toms from beat 3 on now have a rising floor, from velocity 72 up to about 100, with the beat-4 hit at least 100. Drum level on beat 4 of those bars: bar 12 went from -22.7 to -19.7 dB and bar 24 from -21.4 to -19.4 dB, in line with beats 1 and 3 (about -18.5).
- **Vocals:** the lead is up 2.5 dB, now 1.0x the drums' RMS over the sung bars (was 0.75x).

## Spectrum per section
```
intro     slope -1.14  2-8k -3.9 dB/oct  125:+18.3 1k:+0.0 2k:+1.8 4k:+3.5 8k:+1.5 16k:-5.3
verse 1   slope -1.17  2-8k -1.9 dB/oct  125:+6.5 1k:+0.0 2k:-5.4 4k:-3.6 8k:-5.9 16k:-13.7
chorus 1  slope -1.14  2-8k -2.1 dB/oct  125:-0.2 1k:+0.0 2k:-8.3 4k:-7.9 8k:-9.5 16k:-16.5
verse 2   slope -1.19  2-8k -2.1 dB/oct  125:+3.1 1k:+0.0 2k:-6.5 4k:-4.7 8k:-7.3 16k:-14.9
break     slope -1.09  2-8k -5.7 dB/oct  125:+0.0 1k:+0.0 2k:-4.6 4k:-2.7 8k:-8.0 16k:-14.0
chorus 2  slope -1.16  2-8k -2.2 dB/oct  125:+1.1 1k:+0.0 2k:-8.9 4k:-6.9 8k:-8.8 16k:-16.0
pre-tag   slope -1.41  2-8k -8.2 dB/oct  125:+2.0 1k:+0.0 2k:-5.5 4k:-7.2 8k:-14.9 16k:-21.8
tag       slope -1.42  2-8k -3.0 dB/oct  125:+0.9 1k:+0.0 2k:-4.2 4k:+0.3 8k:-5.4 16k:-19.0
```
Whole song: log-log slope -1.21, 8 kHz -8.5 dB under 1 kHz.
