# NullSpine

NullSpine is a YUP-based audio plugin and standalone synth that strikes a short MIDI-triggered exciter into a tuned damped resonator spine. Each note seeds deterministic texture, retunes the modal bank, folds the result through a hard-bounded stage, then applies tone and output gain.

Version: `0.1.0`

## Product identity

- Standalone app ID: `audio.2bit.nullspine`
- Plugin ID: `audio.2bit.NullSpine`
- AU subtype: `NlSp`
- State magic/version: `NSP1` / `1`

## Controls

Host-automatable parameters are intentionally stable and distinct:

- `pitch_offset` / Pitch offset: semitone offset applied to MIDI note tuning.
- `spine` / Spine: modal inharmonicity, inter-mode feed, and stereo spread.
- `decay` / Decay: resonator tail persistence.
- `fold` / Fold: bounded wavefold drive.
- `tone` / Tone: dark-to-bright output contour.
- `output` / Output: final gain before the output ceiling.

The standalone `Trigger` button and Space key are runtime gates only. They are not saved or exposed as automatable parameters. External MIDI has priority; when MIDI releases, a held standalone gate resumes.

## DSP contracts

- Silent before trigger at or below `1e-7`.
- Default triggered RMS at or above `1e-4`.
- Every rendered sample is finite.
- Final peak is clamped to `0.98`.
- Same seed, note, velocity, parameters, and sample rate render deterministically within `1e-6`.
- Release tails fall below `1e-5` after the physical tail horizon `max(4.0 seconds, Decay * 14.0 seconds)`, measured after gate release. Tests cover short decay, the default `1.2s` decay, and the maximum `6.0s` decay without redefining the contract to a short-only case.
- Denormal-sized modal and tone states are flushed.
- The audio callback performs no allocation, locks, I/O, logging, or UI calls. `engine-debug` includes a static callback-source scan for these forbidden operations.

## Build and test

Use the adjacent YUP checkout by default:

```sh
cmake --preset engine-debug
cmake --build --preset engine-debug --parallel
ctest --preset engine-debug --output-on-failure
```

`engine-debug` builds and runs the engine tests plus the plugin bridge tests without building release bundles.

The full bundle preset exists for local or CI release validation:

```sh
cmake --preset plugin-release
cmake --build --preset plugin-release --parallel
ctest --preset plugin-release --output-on-failure
```

Do not use `plugin-release` for lightweight iteration.

## CI and release

`.github/workflows/ci.yml` runs path classification, Debug tests, release bundle builds, tests, package verification, and artifact upload. Artifacts are named exactly:

- `NullSpine-latest-macos-arm64`
- `NullSpine-latest-windows-x64`

Each uploaded artifact includes a platform ZIP and `SHA256SUMS.txt` with 14-day retention.

`.github/workflows/release.yml` promotes only exact-SHA artifacts from a successful `CI` push run on `main`. It resolves lightweight or annotated `v*` tags to a commit, requires the tag version to match `project(NullSpine VERSION ...)`, verifies artifact IDs and SHA-256 manifests, checks ZIP integrity, then publishes:

- `NullSpine-<version>-macos-arm64.zip`
- `NullSpine-<version>-windows-x64.zip`

The release job performs no compilation and fails closed on missing, expired, ambiguous, or mismatched provenance.
