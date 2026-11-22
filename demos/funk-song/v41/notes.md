# Funk song v41

v40 with the e-piano tightened.

- **Voicing:** drop 2 instead of drop 2 and 4, so the shapes span about an octave and a quarter (15 semitones) instead of about an octave and a half or more. A doubled note now goes in the octave nearest the middle of the chord instead of on top. That was causing an octave jump on the A/E shapes. The biggest top-note move between chords is now 3 semitones (was 6). The range is G#2-F#4, with a median of G3.
- **Lengths:** every e-piano note before the tag is let go 25 percent sooner, never under 50 ms. The median note goes from 0.12 s to 0.09 s, and the 90th percentile from 0.37 s to 0.30 s. Down in this register the tines ring longer, which is why it felt longer.
- **Reverb:** the keys' reverb wet goes from 0.35 to 0.22.
- The passing runs still land on the top note.

## Spectrum per section
```
intro     slope -1.14  2-8k -3.9 dB/oct  125:+18.3 1k:+0.0 2k:+1.9 4k:+3.5 8k:+1.5 16k:-5.3
verse 1   slope -1.13  2-8k -1.9 dB/oct  125:+13.4 1k:+0.0 2k:-1.9 4k:+3.1 8k:+0.7 16k:-7.0
chorus 1  slope -1.13  2-8k -2.1 dB/oct  125:+0.9 1k:+0.0 2k:-10.6 4k:-7.0 8k:-8.7 16k:-15.6
verse 2   slope -1.17  2-8k -2.3 dB/oct  125:+5.8 1k:+0.0 2k:-4.8 4k:-2.3 8k:-4.8 16k:-12.5
break     slope -1.08  2-8k -5.8 dB/oct  125:+0.3 1k:+0.0 2k:-4.2 4k:-2.3 8k:-7.7 16k:-13.7
chorus 2  slope -1.16  2-8k -2.3 dB/oct  125:+1.9 1k:+0.0 2k:-8.4 4k:-6.4 8k:-8.2 16k:-15.4
pre-tag   slope -1.39  2-8k -8.5 dB/oct  125:+3.1 1k:+0.0 2k:-5.1 4k:-6.2 8k:-14.1 16k:-20.8
tag       slope -1.38  2-8k -3.0 dB/oct  125:+2.6 1k:+0.0 2k:-3.6 4k:+1.8 8k:-3.9 16k:-17.6
```
