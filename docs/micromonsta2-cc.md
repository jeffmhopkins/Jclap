# MicroMonsta 2 MIDI CC map

Source: *MicroMonsta 2 User manual v2.8* (Audiothingies, July 2026), section 7.1 "Control Change support".

`micromonsta2-cc.csv` lists every CC the manual names. `manual_name` is the label exactly as printed;
`parameter` and `section` are our grouping (the manual has none). The manual gives no value ranges, so
every CC is a plain 0-127 value; the synth maps it onto its own range (ms, Hz, %, shapes).

- 61 parameters are marked `in_plugin=yes`. Bank select, sustain and the channel-mode messages are left
  out of the controller.
- In bi-timbral mode Program A listens on the main MIDI channel and Program B on main channel + 1.
- NRPN: "Full NRPN control is available, documentation will be released separately" (not yet published).
- The manual does not say the synth sends CCs when its knobs move, so two-way sync is not assumed.
