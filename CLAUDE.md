# Working on JClap

## Keep each plugin's docs up to date

Any change to a plugin's behaviour, controls, sound, presets or panel ships with its docs updated in
the same pull request. The docs are part of the change, not a follow-up.

- **README.** Every plugin has `plugins/<name>/README.md`. Update it whenever the plugin changes in
  a way a user could notice: new or renamed controls, changed ranges or defaults, new presets, new
  features, changed behaviour, the version. If the change alters the panel, re-render the README's
  pictures too (`plugins/<name>/docs/readme/make.sh`, where the plugin has one) and update
  `docs/panel.png`.
- **Manual.** A plugin with a manual has `plugins/<name>/docs/manual/` (source `manual.html`, built
  by `build.sh` into `plugins/<name>/docs/<Name>-User-Manual.pdf`). Today that is JSynth and JVerb;
  JBreath and Polar XY are getting one. When such a plugin changes, update `manual.html` (and its
  `img/` crops if the panel changed), then run `build.sh` and commit the rebuilt PDF with it. Never
  commit a `manual.html` change without the rebuilt PDF.
- **Top-level README.** If a plugin gains or loses a manual, or its one-line description changes,
  update its row in the root `README.md`.

A change that truly doesn't touch anything a reader sees (a refactor, a build fix, a bug fix that
only makes the plugin do what the docs already say) can skip the doc update. Say so in the PR
description with a line like:

    Docs unchanged: jverb - refactor only

The `plugin docs` CI check (`.github/workflows/plugin-docs.yml`) enforces this: it fails a PR that
changes `plugins/<name>/src/` or `m4l/` without touching that plugin's README (and its manual, if it
has one) unless the PR description waives it as above, and it fails a `manual.html` change that
didn't rebuild the PDF. Don't use the waiver to dodge real doc work.
