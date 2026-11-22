# Funk song v35

The spectrum is now checked section by section as well as across the whole song.

- **v34:** with the -3.5 dB master shelf, the whole song measured fine. But the choruses (0:29 and 1:17) and verse 1 were hi-hat heavy on their own: 8 kHz sat only about 5 dB under 1 kHz in chorus 2 and verse 1.
- **v35:** the hi-hat and shaker levels are set per section. Intro 0.5, verse 1 0.62, chorus 1 0.75, verse 2 0.75, break 0.8, chorus 2 0.72, the bars before the tag 0.5, and the tag 0.85, each with its in-between sixteenths lower. Open hats now follow the same levels. The master shelf is eased from -3.5 to -1.5 dB, so the whole mix stays in line (log-log slope -1.18, 8 kHz -7 dB under 1 kHz).

## v34 per section
```
intro     slope -1.01  2-8k -2.6 dB/oct  125:+19.3 1k:+0.0 2k:+2.0 4k:+4.2 8k:+3.9 16k:-1.8
verse 1   slope -1.11  2-8k -1.2 dB/oct  125:+6.3 1k:+0.0 2k:-5.3 4k:-3.4 8k:-4.8 16k:-12.4
chorus 1  slope -1.01  2-8k -0.8 dB/oct  125:+1.0 1k:+0.0 2k:-6.9 4k:-5.8 8k:-5.9 16k:-12.1
verse 2   slope -1.26  2-8k -2.3 dB/oct  125:+3.9 1k:+0.0 2k:-6.9 4k:-4.9 8k:-7.9 16k:-16.4
break     slope -1.15  2-8k -6.0 dB/oct  125:+2.1 1k:+0.0 2k:-4.8 4k:-3.1 8k:-8.6 16k:-14.8
chorus 2  slope -0.99  2-8k -0.4 dB/oct  125:+2.4 1k:+0.0 2k:-7.9 4k:-4.3 8k:-4.2 16k:-10.3
pre-tag   slope -1.47  2-8k -8.5 dB/oct  125:+3.3 1k:+0.0 2k:-5.8 4k:-8.3 8k:-15.9 16k:-23.1
tag       slope -1.50  2-8k -3.9 dB/oct  125:+1.9 1k:+0.0 2k:-4.1 4k:-0.1 8k:-6.5 16k:-20.4
```

## v35 per section
```
intro     slope -1.13  2-8k -3.7 dB/oct  125:+19.1 1k:+0.0 2k:+2.1 4k:+3.9 8k:+2.1 16k:-4.6
verse 1   slope -1.16  2-8k -1.8 dB/oct  125:+6.6 1k:+0.0 2k:-5.4 4k:-3.6 8k:-5.9 16k:-13.7
chorus 1  slope -1.11  2-8k -1.8 dB/oct  125:+1.4 1k:+0.0 2k:-7.0 4k:-6.5 8k:-7.9 16k:-14.9
verse 2   slope -1.15  2-8k -1.6 dB/oct  125:+4.1 1k:+0.0 2k:-6.7 4k:-3.9 8k:-6.3 16k:-13.9
break     slope -1.05  2-8k -4.6 dB/oct  125:+2.0 1k:+0.0 2k:-4.6 4k:-2.1 8k:-6.0 16k:-12.0
chorus 2  slope -1.14  2-8k -1.8 dB/oct  125:+2.9 1k:+0.0 2k:-8.3 4k:-5.3 8k:-7.0 16k:-14.1
pre-tag   slope -1.36  2-8k -7.1 dB/oct  125:+3.5 1k:+0.0 2k:-5.7 4k:-7.5 8k:-13.4 16k:-20.3
tag       slope -1.39  2-8k -2.7 dB/oct  125:+2.2 1k:+0.0 2k:-3.8 4k:+1.5 8k:-4.1 16k:-17.6
```

slope = log-log spectral slope (soul and R&B about -1.03, Pestana et al. 2013). The bands are relative to 1 kHz. The intro reads bright and bass-heavy because only drums and bass play there.
