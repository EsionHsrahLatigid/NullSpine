# Design

## Source of truth
- Status: Active
- Last refreshed: 2026-08-08
- Primary product surfaces: YUP standalone/plugin parameter-grid editor, host parameter list, README, CI/release artifacts.
- Evidence reviewed: `CMakeLists.txt`, `CMakePresets.json`, `source/ParameterGridEditor.*`, `source/NullSpinePlugin.*`, `source/NullSpineEngine.cpp`, `tests/NullSpineEngineTests.cpp`, `tests/NullSpinePluginBridgeTests.cpp`, `.github/workflows/ci.yml`, `.github/workflows/release.yml`, read-only local synth scaffold patterns.

## Brand
- Personality: Skeletal, pressure-fractured, direct, high contrast, technical.
- Trust signals: Stable parameter IDs, bounded DSP, deterministic tests, exact plugin IDs, exact-SHA release promotion, SHA-256 manifests.
- Avoid: Decorative assets, soft gradients, marketing language, low-contrast controls, ambiguous parameter names, animated visual noise.

## Product goals
- Goals: Provide a MIDI-triggered impulse-to-resonator spine synth with note-seeded texture, short excitation, tuned damped modal tails, bounded fold, tone, and output control.
- Non-goals: Sample playback, polyphonic voice allocation, visual asset packs, new UI dependencies, generic subtractive synth controls.
- Success signals: Triggered output is audible by default, silent before trigger, finite and bounded under extremes, reproducible for the same seed, release tails fall below `1e-5` after the documented horizon, and output is visibly metered in standalone use.

## Personas and jobs
- Primary personas: Sound designers, electronic musicians, plugin testers, maintainers validating small YUP synth projects.
- User jobs: Strike resonant percussive tones from MIDI, shape modal fracture and decay quickly, verify output level, test standalone triggering without a controller.
- Key contexts of use: DAW instrument track, local standalone smoke testing, automated Debug tests, release packaging.

## Information architecture
- Primary navigation: Single editor surface; no navigation hierarchy.
- Core routes/screens: Header, runtime Trigger and output meter row, six-parameter grid.
- Content hierarchy: Product name first, trigger/meter second, host parameters third.

## Design principles
- Principle 1: Runtime controls stay separate from host automation; Trigger is a performance gate, not saved state.
- Principle 2: The UI should read like an instrument panel, not a landing page.
- Tradeoffs: Keep the native YUP rotary grid for consistency, even though a custom visualizer could express the brand more literally.

## Visual language
- Color: Near-black base, chalk-bone accent, muted steel meter troughs, restrained warning text. High contrast takes priority over atmosphere.
- Typography: Native YUP label rendering; concise labels that fit compact controls.
- Spacing/layout rhythm: Fixed top control band with evenly spaced parameter cells; stable dimensions to avoid layout shift.
- Shape/radius/elevation: Flat, squared, minimal elevation; no nested cards or decorative panels.
- Motion: Meter decay only; no decorative animation.
- Imagery/iconography: No new assets. The instrument identity is expressed through color, naming, and tight grid composition.

## Components
- Existing components to reuse: `ParameterGridEditor`, `yup::Label`, `yup::Slider`, `yup::TextButton`, custom `OutputMeter`.
- New/changed components: NullSpine-specific Trigger copy, accent color, parameter names, output meter color.
- Variants and states: Trigger pressed/released, Space held/released, MIDI priority active, meter active/decaying, parameters idle/dragged.
- Token/component ownership: Product accent is passed from `NullSpinePlugin::createEditor`; reusable editor owns layout and meter drawing.

## Accessibility
- Target standard: Keyboard-operable standalone controls with readable contrast.
- Keyboard/focus behavior: Space acts as a momentary gate while focused; focus loss clears mouse and Space gates.
- Contrast/readability: Bone-on-black and muted steel surfaces maintain high contrast; no text over imagery.
- Screen-reader semantics: Native YUP component semantics only; no custom accessibility layer in this version.
- Reduced motion and sensory considerations: Meter smoothing is functional and low-motion; no flashing or decorative movement.

## Responsive behavior
- Supported breakpoints/devices: Resizable desktop plugin/standalone window using the preferred 940x520 aspect.
- Layout adaptations: Parameter cells recalculate from available bounds; controls keep fixed relative ordering.
- Touch/hover differences: Mouse/touch press on Trigger gates while held; hover is nonessential.

## Interaction states
- Loading: Not applicable for the static plugin editor.
- Empty: Pre-trigger audio is silent; meter rests at zero.
- Error: Invalid state loads fail through `yup::Result` without mutating parameters.
- Success: Trigger or MIDI note produces bounded output and meter movement.
- Disabled: No disabled controls in the current surface.
- Offline/slow network, if applicable: Not applicable at runtime; CI dependency fetch failures are build-time concerns.

## Content voice
- Tone: Sparse, technical, instrument-focused.
- Terminology: Use `spine`, `exciter`, `resonator`, `fold`, `tone`, `output`, `Trigger`, and `MIDI`.
- Microcopy rules: Do not explain basic interaction in long prose; use one compact status line when necessary.

## Implementation constraints
- Framework/styling system: Repo-native CMake and YUP components only.
- Design-token constraints: No new assets, no new dependencies, product accent supplied as a constant.
- Performance constraints: No allocation, locks, I/O, logging, or UI calls in the audio callback; every sample finite and peak-bounded. Release-tail silence is measured after `max(4.0 seconds, Decay * 14.0 seconds)` from gate release, including the default `1.2s` decay and maximum `6.0s` decay.
- Compatibility constraints: Standalone app ID `audio.2bit.nullspine`, plugin ID `audio.2bit.NullSpine`, AU subtype `NlSp`, state magic/version `NSP1` / `1`.
- Test/screenshot expectations: Unit tests cover stereo DSP identity, deterministic behavior, bounds, parameterized release-tail horizon, state round-trip, trigger/MIDI ownership, meter publication, and static `processBlock` forbidden-operation scanning. No screenshot baseline is required for this native plugin editor.

## Open questions
- [ ] Decide whether pitch offset should become stepped when YUP exposes a local interval API / maintainer / affects host automation feel.
- [ ] Decide whether future versions need an explicit visual modal spine meter / product owner / affects UI scope and test strategy.
