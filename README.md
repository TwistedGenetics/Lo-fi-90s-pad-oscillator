# Dusty Pads

A lo-fi 90s pad oscillator for the **Korg microKORG 2** and the **Korg NTS-1 digital kit mkII**, built with the logue SDK.

Dusty Pads makes the chord pads from early-90s jungle and hardcore: a string machine, Juno-ish analog pad, organ, choir or glass tone, played clean and then **sampled into an S950** and pitched round the keyboard. Every voice has two drifting, detuned copies for a wide ensemble, and the sampler section adds the **sample-rate and bit reduction**, the **"sampled at 45, played at 33" resample drop**, and a **key-tracked** grain where low notes go murky like a real sampler. A little **hiss** sits underneath, in the sample.

It's made for the microKORG 2's polyphony: hold chords with it on one layer and put [Rollers](https://github.com/TwistedGenetics/Rollers_Bass) on the other for a full jungle chord-and-bass patch. On the NTS-1 mkII it plays as a mono version, good for single-note atmospheres.

## Download & install

Go to [**Releases**](https://github.com/TwistedGenetics/Lo-fi-90s-pad-oscillator/releases/latest) and download the file for your synth.

### microKORG 2: `DustyPads.mk2unit`

Needs microKORG 2 firmware **2.0.0 or later** (the version that added logue SDK support).

- **KORG Sound Librarian** (Korg's official app for v2.0): connect the microKORG 2 by USB, add `DustyPads.mk2unit` as a user oscillator and send it to the synth.
- **Or USB mass storage:** hold the first option button while powering on, then copy the file into `Units/Oscs/` (into a `SlotN` folder) on the drive that appears.

Dusty Pads then appears at the end of the oscillator list as **DustyPad**. It's polyphonic: every voice gets its own random start phase, drift and PWM, so chords never sound phase-locked.

**`DPmeter.mk2unit`** (optional, also in each Release) is the same oscillator with its CPU load shown next to RESAMPLE, e.g. `OFF 37%` (peak over the last half second; 100 % = out of time, crackles). Load it alongside Dusty Pads if you hear crackles and want to know how close to the limit a patch is.

### NTS-1 mkII: `DustyPads.nts1mkiiunit`

1. Open **KORG KONTROL Editor** with the NTS-1 mkII connected (firmware 1.3 or later recommended).
2. Drag the file onto the **oscillator** list, then press **SEND**.
3. Select Dusty Pads with the OSC button.

## Controls

The parameters are the same on both synths, in the same order.

| microKORG 2 | NTS-1 mkII | Parameter | Range | What it does |
|---|---|---|---|---|
| OSC page 1 | Knob A | TIMBRE | 0–1023 | **Morphs through five pad sources**: STRINGS (0) → ANALOG (256) → ORGAN (512) → CHOIR (768) → GLASS (1023). In-between values crossfade the two neighbours |
| OSC page 1 | Knob B | GRIT | 0–1023 | **The sampler.** 0 = clean. Up from there the sample rate drops (48 kHz → 6 kHz) and the bit depth drops (16 → 8 bits) together, with a sampler-style output filter. Aliasing is deliberate |
| OSC page 1 | Edit 1 | ENSEMBLE | 0–100 % | Second copy of every voice, detuned up to 25 cents from the first. 0 = one copy (lighter on the CPU) |
| OSC page 2 | Edit 2 | DRIFT | 0–100 % | Slow random pitch wander, separate for each copy and each voice (up to ±10 cents). What makes held chords breathe |
| OSC page 2 | Edit 3 | TONE | −100…+100 | Tilt: negative = dark and dull (great with GRIT), positive = bright and airy |
| OSC page 2 | Edit 4 | RESAMPLE | OFF, −12, −24 | Emulates sampling the pad an octave or two **up** and playing it back down: the sampler's rate drops one or two more octaves on top of GRIT. Grainier, darker, more 94 |
| OSC page 2 | Edit 5 | KEY TRACK | OFF, ON | **ON:** the sampler's rate follows the note around middle C, like a variable-clock sampler. Low notes get murkier, high notes crisper. Works with any GRIT setting |
| OSC page 2 | Edit 6 | HISS | 0–100 % | Noise floor "in the sample" (it goes through the sampler with the pad) |

### The five timbres

- **STRINGS**: a saw per copy; with ENSEMBLE up, the string-machine sound.
- **ANALOG**: saw plus a slowly pulse-width-modulated square, Juno-pad style. Each voice has its own PWM speed.
- **ORGAN**: drawbar sines at 16', 8', 4' and 2⅔'. The rave organ.
- **CHOIR**: a narrow pulse through fixed "ah" formants (730 / 1090 / 2440 Hz). The formants don't move with the note, so it sounds like voices, not a filter sweep.
- **GLASS**: a 2:1 phase-modulated sine with a 4th-harmonic shimmer. Bell-ish, good for the high, sparse chords.

**microKORG 2:** the parameters are on OSC pages 1–2. In the **virtual patch**, Dusty Pads adds four modulation destinations: **Timbre**, **Grit**, **Tone** and **Ensemble**. Modulation is per voice. Try a slow LFO → Timbre for an evolving pad, velocity → Grit so harder notes sound more sampled, or an envelope → Tone for a pad that opens up.

**NTS-1 mkII:** hold **OSC** and turn the TYPE encoder to choose an Edit parameter, then use Knob B to set the value. The NTS-1's **shape LFO** modulates TIMBRE.

## Starter settings

| Name | Timbre | Grit | Ensemble | Drift | Tone | Resample | Key track | Hiss |
|---|---|---|---|---|---|---|---|---|
| Warehouse strings | 60 | 420 | 70 % | 40 % | −20 | OFF | OFF | 20 % |
| Juno pad | 256 | 200 | 60 % | 30 % | +10 | OFF | OFF | 10 % |
| Rave organ | 512 | 550 | 20 % | 15 % | +20 | −12 | OFF | 15 % |
| Ah choir | 768 | 380 | 80 % | 45 % | −10 | OFF | ON | 25 % |
| Glass bells | 1023 | 300 | 40 % | 20 % | +30 | OFF | OFF | 10 % |
| Pitched-down 94 | 140 | 650 | 70 % | 50 % | −40 | −24 | ON | 35 % |

On the microKORG 2, give the pads a slow-ish amp attack and a long release; the filter can stay fairly open, because TONE and GRIT do most of the darkening. The desktop tests render an 8-voice jungle chord loop (Fm9 – D♭maj7 – E♭sus – Cm7 at 170 BPM) with each of these to `test/out/*.wav`. They're also in each CI run's artifact, so you can hear them before loading anything.

## Test checklist (by ear on the hardware)

Start from the defaults (Timbre 180, Grit 300, Ensemble 60 %, Drift 35 %, Tone 0, Resample OFF, Key track OFF, Hiss 15 %).

- [ ] **Tuning**: TIMBRE 0, GRIT 0, ENSEMBLE 0, DRIFT 0: A4 reads 440 Hz on a tuner.
- [ ] **Timbre sweep**: TIMBRE 0 → 1023 goes strings → analog → organ → choir → glass with no level jumps or clicks.
- [ ] **Grit**: 0 sounds clean; turning up adds grain and aliasing; 1023 is very lo-fi.
- [ ] **Resample**: −12 and −24 each sound a step darker and grainier; switching while holding a chord doesn't click.
- [ ] **Key track**: ON, play the bottom and top of the keyboard: low notes are murkier than high ones.
- [ ] **Ensemble / drift**: ENSEMBLE 0 = one plain copy, up = wide chorus; DRIFT up = the chord wanders slightly.
- [ ] **Tone**: −100 dark, +100 bright, no big level change.
- [ ] **Hiss**: 0 silent between notes (apart from the synth's own noise), 100 % a clear noise bed under the pad.
- [ ] **Chords / full polyphony (microKORG 2)**: hold 8 notes with GRIT and ENSEMBLE up: no dropouts or crackles. With DPmeter loaded, note the % shown next to RESAMPLE.
- [ ] **Virtual patch (microKORG 2)**: route an LFO or envelope to Timbre / Grit / Tone / Ensemble and hear each move, per voice.
- [ ] **Layering (microKORG 2)**: Dusty Pads on one layer, Rollers on the other.
- [ ] **NTS-1 shape LFO**: route to shape → TIMBRE wobbles.
- [ ] **Everything at max**: loud and crunchy, but no dropouts, crackles, or the unit stopping.

## CPU

Dusty Pads uses no tables and allocates no memory. Everything slow (pitch, drift, detune, timbre weights, filter coefficients, sampler settings) is worked out once per buffer; the per-sample work is DPW saws, parabolic sines and a few one-pole filters. A timbre whose weight is zero isn't computed at all, the second ensemble copy is skipped while ENSEMBLE is 0, and the sampler section is skipped while it's fully bypassed (GRIT 0, RESAMPLE OFF, KEY TRACK OFF). On the microKORG 2 the four voices of each group run in the four NEON lanes, and idle groups are skipped.

- **Desktop** (test harness, 8 voices): about 140 ns per sample with the defaults, about 220 ns with ORGAN and everything up, and about 335 ns in the worst case (all five timbres running at once through per-voice modulation). That's roughly 1.1–2.5× the cost of Rollers' microKORG 2 renderer on the same machine.
- **microKORG 2** (Cortex-A7, up to 8 voices): Korg doesn't publish the per-oscillator budget, so check with DPmeter. If a big chord crackles, the cheapest fixes are ENSEMBLE 0 (halves the oscillator work), staying between two neighbouring timbres (the normal case) rather than modulating TIMBRE across the whole range, or fewer voices in the patch.
- **NTS-1 mkII** (Cortex-M7, one voice): an estimate, not a measurement; one voice is a small fraction of the CPU.

The NTS-1 unit is ~9 KB (limit 48 KB) and links no library code beyond the pitch table the firmware provides.

## Building

GitHub Actions builds both units with Korg's own toolchains and runs the DSP tests on every push. Each run's files are under the **Actions** tab.

Releases are automatic: when `.version` in `nts1mkii/header.c` and `microkorg2/header.c` changes (e.g. `0x00010001U` = 1.0.1) and is pushed to main, Actions tags it and publishes a new Release with both unit files. Pushes that keep the same version refresh that Release's files.

To build locally (no Docker needed):

```sh
./build.sh          # both units into dist/
./build.sh nts1     # NTS-1 mkII only
./build.sh mk2      # microKORG 2 only (Linux: Korg only publishes its toolchain for Linux)
```

This downloads the toolchains and the logue SDK into `.deps/` on first run.

Desktop DSP tests check NaN/clipping/DC across every timbre, grit, resample, key-track and tone setting over the whole note range, plus tuning, aliasing at GRIT 0, that GRIT / RESAMPLE / KEY TRACK each add crunch, level balance across timbres and grit, zipper noise on every control, and that the 4-voice (microKORG 2) and 1-voice (NTS-1) builds match. They also write the demo WAVs to `test/out/`:

```sh
./test/run.sh
```

## Layout

```
core/         dustypads.h: the whole DSP, one template for both synths
              (PadEngine<float> = NTS-1 mkII, PadEngine<v4f> = 4 voices in NEON lanes for the microKORG 2)
microkorg2/   microKORG 2 unit: header.c (parameters), unit.cc (voice groups, voice events, virtual patch, optional load meter)
nts1mkii/     NTS-1 mkII unit: header.c, unit.cc (SDK callbacks), Makefile, config.mk
tools/mk2-meter/  DPmeter: the microKORG 2 unit built with the CPU load meter
test/         render.cc, run.sh
build.sh
.github/workflows/build.yml   CI build + release
```

## Notes

- Developer ID `0x5477476E` ('TwGn'), unit ID `4` (VectorFM is 1, Formant Mutator is 2, Rollers is 3). If you register an ID with Korg (pull request to the logue SDK's `developer_ids.md`), set it in both `header.c` files.
- Built against logue SDK commit `e78b16c`.
- BSD 3-Clause licence (see `LICENSE`).
