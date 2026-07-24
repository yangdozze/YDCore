#!/usr/bin/env python3
"""GLOBUS factory preset generator (Ninth Parallel Audio).

Writes Presets/*.json plus Presets/factory_bundle.json. Each preset stores only
the parameters that differ from the plugin's built-in defaults (the loader
resets to defaults first).

Parameter IDs and choice indices MUST match Source/Parameters.cpp.
"""
import json, os, re, sys

AUTHOR = "Ninth Parallel Audio"

OUT = os.path.join(os.path.dirname(__file__), "..", "Presets")

# choice index helpers (mirror Parameters.cpp)
SINE, TRI, SAW, SQUARE, PULSE, SUPERSAW, NOISEW = range(7)
LP12, LP24, HP12, HP24, BP12, NOTCH, LADDER24, OTA24, SEM12, BP24 = range(10)   # v1.2 appended models
L_SINE, L_TRI, L_SAW, L_SQR, L_SH = range(5)
POLY, MONO, LEGATO = range(3)
ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM = range(4)
# lfo quick destinations
LD_OFF, LD_PITCH, LD_O1PITCH, LD_O2PITCH, LD_CUT, LD_AMP, LD_PAN, LD_PW, LD_FX = range(9)
# mod env destinations
MD_OFF, MD_PITCH, MD_O1PITCH, MD_O2PITCH, MD_O1PW, MD_O2PW, MD_CUT, MD_L1RATE, MD_L2RATE, MD_NOISE = range(10)
# matrix sources
MS_OFF, MS_VEL, MS_WHEEL, MS_AT, MS_KEY, MS_AENV, MS_FENV, MS_MENV, MS_LFO1, MS_LFO2, MS_RND, MS_PB = range(12)
# matrix destinations (v1.2 appended entries after Stereo Width)
MDST_OFF, MDST_O1P, MDST_O2P, MDST_ALLP, MDST_O1F, MDST_O2F, MDST_O1L, MDST_O2L, MDST_O1PAN, MDST_O2PAN, \
    MDST_O1PW, MDST_O2PW, MDST_CUT, MDST_RES, MDST_AMP, MDST_L1R, MDST_L2R, MDST_FX, MDST_WIDTH, \
    MDST_O1WT, MDST_O2WT, MDST_O1WARP, MDST_O2WARP, MDST_O1DET, MDST_O2DET, MDST_O1SPR, MDST_O2SPR, \
    MDST_DRIVE = range(28)
# v1.2 engines / warp / quality
E_LEGACY, E_HQ, E_WT = range(3)
W_OFF, W_BENDP, W_BENDM, W_SYNC, W_ASYM, W_MIRROR = range(6)
Q_LEGACY, Q_ECO, Q_HIGH, Q_ULTRA = range(4)
LD_WTPOS = 9   # appended lfo quick destination "WT Pos 1+2"
# arp divisions: 1/4,1/4T,1/8,1/8T,1/16,1/16T,1/32
A4, A4T, A8, A8T, A16, A16T, A32 = range(7)
# lfo divisions: 1/1,1/2,1/2T,1/4,1/4T,1/8,1/8T,1/16,1/16T,1/32
LV1, LV2, LV2T, LV4, LV4T, LV8, LV8T, LV16, LV16T, LV32 = range(10)
# delay divisions: 1/2,1/4D,1/4,1/4T,1/8D,1/8,1/8T,1/16
D2, D4D, D4, D4T, D8D, D8, D8T, D16 = range(8)

def env(pfx, a, d, s, r):
    return {f"{pfx}Attack": a, f"{pfx}Decay": d, f"{pfx}Sustain": s, f"{pfx}Release": r}

def osc(n, wave=None, on=None, oct=None, semi=None, fine=None, level=None, pan=None,
        pw=None, uni=None, det=None, spread=None, drift=None, phase=None, randphase=None,
        engine=None, wtpos=None, warpmode=None, warpamt=None):
    o, out = f"osc{n}", {}
    for k, v in [("Wave", wave), ("On", on), ("Oct", oct), ("Semi", semi), ("Fine", fine),
                 ("Level", level), ("Pan", pan), ("PW", pw), ("UniCount", uni), ("UniDetune", det),
                 ("UniSpread", spread), ("Drift", drift), ("Phase", phase), ("RandPhase", randphase),
                 ("Engine", engine), ("WtPos", wtpos), ("WarpMode", warpmode), ("WarpAmt", warpamt)]:
        if v is not None:
            out[o + k] = v
    return out

def curves(pfx, a=None, d=None, r=None):
    out = {}
    if a is not None: out[f"{pfx}AttackCurve"] = a
    if d is not None: out[f"{pfx}DecayCurve"] = d
    if r is not None: out[f"{pfx}ReleaseCurve"] = r
    return out

W = {}  # name -> {"osc1": bank, "osc2": bank} — v1.2 wavetable references

def lfo(n, wave=None, rate=None, sync=None, div=None, fade=None, dest=None, amount=None,
        retrig=None, bipolar=None, phase=None):
    o, out = f"lfo{n}", {}
    for k, v in [("Wave", wave), ("Rate", rate), ("Sync", sync), ("Div", div), ("Fade", fade),
                 ("Dest", dest), ("Amount", amount), ("Retrig", retrig), ("Bipolar", bipolar), ("Phase", phase)]:
        if v is not None:
            out[o + k] = v
    return out

def mat(slot, src, dst, amt, bipolar=1):
    return {f"mat{slot}Src": src, f"mat{slot}Dst": dst, f"mat{slot}Amt": amt, f"mat{slot}Bipolar": bipolar}

def filt(type=None, cutoff=None, res=None, drive=None, key=None, envamt=None):
    out = {}
    for k, v in [("filterType", type), ("cutoff", cutoff), ("resonance", res),
                 ("filterDrive", drive), ("keyTrack", key), ("filterEnvAmt", envamt)]:
        if v is not None:
            out[k] = v
    return out

def fx(dist=None, ch=None, dly=None, rv=None, eq=None):
    out = {}
    if dist: out.update({"distOn": 1, **{f"dist{k}": v for k, v in dist.items()}})
    if ch:   out.update({"chorusOn": 1, **{f"chorus{k}": v for k, v in ch.items()}})
    if dly:  out.update({"delayOn": 1, **{f"delay{k}": v for k, v in dly.items()}})
    if rv:   out.update({"reverbOn": 1, **{f"reverb{k}": v for k, v in rv.items()}})
    if eq:   out.update({"eqOn": 1, **eq})
    return out

def arp(mode=ARP_UP, div=A16, gate=0.6, octv=1, hold=0):
    return {"arpOn": 1, "arpMode": mode, "arpDiv": div, "arpGate": gate, "arpOct": octv, "arpHold": hold}

P = {}  # name -> (category, params)

# ============================== BASS ==============================
P["Dark Pressure"] = ("Bass", {
    **osc(1, SAW, level=0.8, drift=0.2), **osc(2, SQUARE, on=1, oct=-1, level=0.6, fine=6),
    "subOn": 1, "subLevel": 0.4,
    **filt(LP24, 340, 0.28, 0.35, 0.1, 0.45), **env("amp", 0.002, 0.4, 0.55, 0.12), **env("filt", 0.002, 0.32, 0.1, 0.15),
    "playMode": MONO, "glideTime": 0.035,
    **fx(eq={"eqLow": 2.5, "eqHigh": -1.5}),
})
P["Rust Bass"] = ("Bass", {
    **osc(1, PULSE, pw=0.27, level=0.8, drift=0.45), **osc(2, SAW, on=1, fine=9, level=0.55),
    "noiseType": 1, "noiseLevel": 0.07, "noiseTone": -0.4,
    **filt(LP12, 780, 0.42, 0.6, 0.0, 0.3), **env("amp", 0.003, 0.5, 0.7, 0.15), **env("filt", 0.002, 0.4, 0.2, 0.2),
    "playMode": LEGATO, "glideTime": 0.05,
    **fx(dist={"Drive": 0.55, "Tone": 3400, "Mix": 0.75}),
})
P["Night Driver"] = ("Bass", {
    **osc(1, SUPERSAW, det=14, level=0.85, uni=1),
    "subOn": 1, "subLevel": 0.35,
    **filt(LP24, 520, 0.22, 0.25, 0.0, 0.35), **env("amp", 0.002, 0.35, 0.75, 0.1), **env("filt", 0.001, 0.22, 0.05, 0.12),
    **fx(eq={"eqLow": 2.0}, ch={"Mix": 0.2, "Rate": 0.6}),
})
P["Low Orbit"] = ("Bass", {
    **osc(1, TRI, level=0.85), **osc(2, SINE, on=1, oct=-1, level=0.6),
    **filt(LP12, 850, 0.15, 0.1, 0.2, 0.0), **env("amp", 0.02, 0.4, 0.9, 0.3),
    **lfo(1, L_SINE, 0.4, dest=LD_CUT, amount=0.14, fade=0.8),
    **fx(ch={"Mix": 0.35, "Rate": 0.5, "Depth": 0.3}),
})
P["Concrete Bass"] = ("Bass", {
    **osc(1, SQUARE, level=0.8), **osc(2, SQUARE, on=1, semi=7, level=0.35, fine=-7),
    **filt(LP24, 640, 0.35, 0.7, 0.0, 0.5), **env("amp", 0.002, 0.45, 0.6, 0.1), **env("filt", 0.001, 0.16, 0.0, 0.1),
    "playMode": MONO, "notePriority": 2,
    **fx(dist={"Drive": 0.4, "Tone": 5200, "Mix": 0.6}, eq={"eqLow": 1.5, "eqMid": -1.0}),
})
P["Toxic Mono"] = ("Bass", {
    **osc(1, SAW, uni=3, det=26, spread=0.4, level=0.8), **osc(2, PULSE, on=1, oct=-1, pw=0.2, level=0.6),
    **filt(LP24, 420, 0.58, 0.4, 0.0, 0.62), **env("amp", 0.002, 0.5, 0.65, 0.12), **env("filt", 0.002, 0.3, 0.1, 0.15),
    **lfo(2, L_SH, sync=1, div=LV16, dest=LD_CUT, amount=0.12, retrig=0),
    "playMode": LEGATO, "glideTime": 0.08,
    **fx(dist={"Drive": 0.35, "Tone": 4200, "Mix": 0.5}),
})

# ============================== SUB BASS ==============================
P["Sub Voltage"] = ("Sub Bass", {
    **osc(1, SINE, level=0.9), "subOn": 1, "subWave": 1, "subLevel": 0.55,
    **filt(LP24, 2800, 0.05, 0.2), **env("amp", 0.002, 0.3, 0.9, 0.08),
    "playMode": MONO, "glideTime": 0.06,
    **fx(eq={"eqLow": 3.0}),
})
P["Chrome 808"] = ("Sub Bass", {
    **osc(1, SINE, level=0.95),
    **env("amp", 0.001, 1.3, 0.0, 0.5), **env("mod", 0.001, 0.09, 0.0, 0.09),
    "modEnvAmt": 0.55, "modEnvDest": MD_PITCH,
    **filt(LP24, 5000, 0.0, 0.3), "playMode": MONO,
    **fx(dist={"Drive": 0.35, "Tone": 2600, "Mix": 0.55}),
})
P["Deep Floor"] = ("Sub Bass", {
    **osc(1, SINE, level=0.9), **osc(2, TRI, on=1, level=0.3, fine=4),
    **filt(LP12, 900, 0.05, 0.35), **env("amp", 0.004, 0.5, 0.85, 0.15),
    **fx(eq={"eqLow": 2.0, "eqHigh": -3.0}),
})
P["Round Cellar"] = ("Sub Bass", {
    **osc(1, TRI, level=0.9), "subOn": 1, "subLevel": 0.5,
    **filt(LP24, 460, 0.12, 0.15, 0.0, 0.25), **env("amp", 0.003, 0.6, 0.75, 0.2), **env("filt", 0.002, 0.5, 0.2, 0.3),
    "playMode": MONO, "glideTime": 0.04,
})

# ============================== LEAD ==============================
P["Neon Bite"] = ("Lead", {
    **osc(1, SUPERSAW, det=22, level=0.8, uni=1), **osc(2, SAW, on=1, oct=1, level=0.45, fine=-8),
    **filt(LP24, 4800, 0.2, 0.3, 0.3, 0.25), **env("amp", 0.003, 0.3, 0.8, 0.2), **env("filt", 0.002, 0.4, 0.5, 0.25),
    **lfo(1, L_SINE, 5.4, dest=LD_PITCH, amount=0.035, fade=0.5),
    **fx(ch={"Mix": 0.35}, dly={"Sync": 1, "Div": D8, "Feedback": 0.35, "Mix": 0.25}, rv={"Mix": 0.18, "Size": 0.5}),
})
P["Cold Razor"] = ("Lead", {
    **osc(1, SQUARE, level=0.75), **osc(2, SAW, on=1, fine=12, level=0.6),
    **filt(LP12, 6500, 0.3, 0.2, 0.4, 0.0), **env("amp", 0.001, 0.2, 0.85, 0.12),
    **lfo(1, L_TRI, 6.2, dest=LD_PITCH, amount=0.03, fade=0.9),
    "playMode": MONO, "glideTime": 0.03,
    **fx(dly={"Sync": 1, "Div": D8T, "Feedback": 0.3, "Mix": 0.22}, eq={"eqHigh": 1.5}),
})
P["Digital Cry"] = ("Lead", {
    **osc(1, PULSE, pw=0.35, level=0.8), **osc(2, PULSE, on=1, pw=0.65, semi=12, level=0.4),
    **filt(LP24, 3800, 0.25, 0.15, 0.5, 0.3), **env("amp", 0.002, 0.35, 0.7, 0.25), **env("filt", 0.001, 0.5, 0.4, 0.3),
    **lfo(1, L_TRI, 0.7, dest=LD_PW, amount=0.4),
    **env("mod", 0.001, 0.12, 0.0, 0.1), "modEnvAmt": 0.3, "modEnvDest": MD_PITCH,
    **fx(dly={"Sync": 1, "Div": D4T, "Feedback": 0.4, "Mix": 0.3}, rv={"Mix": 0.2}),
})
P["Broken Signal"] = ("Lead", {
    **osc(1, SAW, level=0.8, drift=0.5), **osc(2, SQUARE, on=1, fine=-15, level=0.5),
    **filt(LP24, 2400, 0.45, 0.55, 0.0, 0.3),
    **lfo(2, L_SH, sync=1, div=LV16, dest=LD_CUT, amount=0.35, retrig=0),
    **env("amp", 0.002, 0.3, 0.75, 0.15),
    "playMode": MONO, "glideTime": 0.02,
    **fx(dist={"Drive": 0.5, "Tone": 5600, "Mix": 0.6}, dly={"Sync": 1, "Div": D16, "Feedback": 0.45, "Mix": 0.25}),
})
P["Blue Flame"] = ("Lead", {
    **osc(1, SAW, level=0.8), **osc(2, SAW, on=1, semi=7, level=0.5, fine=6),
    **filt(LP24, 2600, 0.3, 0.45, 0.35, 0.4), **env("amp", 0.004, 0.4, 0.75, 0.3), **env("filt", 0.003, 0.6, 0.45, 0.4),
    **lfo(1, L_SINE, 5.0, dest=LD_PITCH, amount=0.04, fade=1.1),
    **mat(1, MS_WHEEL, MDST_CUT, 0.35, 0),
    "playMode": LEGATO, "glideTime": 0.12,
    **fx(rv={"Mix": 0.22, "Size": 0.55}, ch={"Mix": 0.25}),
})
P["Acid Skyline"] = ("Lead", {
    **osc(1, SAW, level=0.85),
    **filt(LP24, 700, 0.72, 0.6, 0.2, 0.6), **env("amp", 0.001, 0.25, 0.7, 0.08), **env("filt", 0.001, 0.24, 0.05, 0.1),
    **mat(1, MS_VEL, MDST_CUT, 0.3, 0),
    "playMode": MONO, "glideTime": 0.1, "notePriority": 0,
    **fx(dist={"Drive": 0.45, "Tone": 6800, "Mix": 0.5}, dly={"Sync": 1, "Div": D8, "Feedback": 0.3, "Mix": 0.2}),
})

# ============================== PLUCK ==============================
P["Glass Steps"] = ("Pluck", {
    **osc(1, SINE, level=0.7), **osc(2, TRI, on=1, oct=1, level=0.55),
    **filt(LP12, 5200, 0.15, 0.0, 0.7, 0.3), **env("amp", 0.001, 0.28, 0.0, 0.3), **env("filt", 0.001, 0.2, 0.0, 0.2),
    **fx(dly={"Sync": 1, "Div": D8, "Feedback": 0.35, "Mix": 0.28}, rv={"Mix": 0.22, "Size": 0.45}),
})
P["Metro Pluck"] = ("Pluck", {
    **osc(1, SAW, level=0.8),
    **filt(LP24, 1150, 0.3, 0.2, 0.5, 0.55), **env("amp", 0.001, 0.3, 0.0, 0.25), **env("filt", 0.001, 0.14, 0.0, 0.14),
    **mat(1, MS_VEL, MDST_CUT, 0.4, 0),
    **fx(dly={"Sync": 1, "Div": D16, "Feedback": 0.4, "Mix": 0.3}),
})
P["Crystal Wire"] = ("Pluck", {
    **osc(1, PULSE, pw=0.33, oct=1, level=0.7), **osc(2, SINE, on=1, oct=1, semi=7, level=0.3),
    **filt(LP12, 4200, 0.2, 0.0, 0.8, 0.25), **env("amp", 0.001, 0.22, 0.0, 0.25), **env("filt", 0.001, 0.16, 0.0, 0.15),
    **fx(ch={"Mix": 0.3, "Rate": 1.2}, rv={"Mix": 0.25, "Size": 0.5}),
})
P["Soft Attack"] = ("Pluck", {
    **osc(1, TRI, level=0.85),
    **filt(LP12, 950, 0.1, 0.1, 0.4, 0.35), **env("amp", 0.003, 0.45, 0.0, 0.4), **env("filt", 0.002, 0.35, 0.0, 0.3),
    **fx(rv={"Mix": 0.3, "Size": 0.6, "Damp": 0.7}),
})
P["Arcade Dust"] = ("Pluck", {
    **osc(1, SQUARE, oct=1, level=0.65), **osc(2, SQUARE, on=1, level=0.5, fine=10),
    **filt(LP12, 3000, 0.25, 0.3, 0.6, 0.4), **env("amp", 0.001, 0.16, 0.0, 0.14), **env("filt", 0.001, 0.1, 0.0, 0.1),
    **fx(dist={"Drive": 0.3, "Tone": 3200, "Mix": 0.4}, dly={"Sync": 1, "Div": D16, "Feedback": 0.3, "Mix": 0.22}),
})
P["Velvet Pulse"] = ("Pluck", {
    **osc(1, PULSE, pw=0.45, level=0.8, drift=0.3),
    **filt(LP24, 750, 0.2, 0.25, 0.3, 0.45), **env("amp", 0.002, 0.5, 0.0, 0.45), **env("filt", 0.001, 0.3, 0.0, 0.25),
    **lfo(1, L_SINE, 0.35, dest=LD_PW, amount=0.25),
    **fx(ch={"Mix": 0.35, "Rate": 0.6}, rv={"Mix": 0.18}),
})

# ============================== KEYS ==============================
P["Analog Room"] = ("Keys", {
    **osc(1, SAW, level=0.7, drift=0.25), **osc(2, SQUARE, on=1, level=0.4, fine=-9),
    **filt(LP24, 2900, 0.15, 0.25, 0.5, 0.3), **env("amp", 0.003, 0.7, 0.5, 0.35), **env("filt", 0.002, 0.5, 0.3, 0.4),
    **mat(1, MS_VEL, MDST_CUT, 0.3, 0),
    **fx(ch={"Mix": 0.28, "Rate": 0.8}, rv={"Mix": 0.25, "Size": 0.4}),
})
P["Soft Machine"] = ("Keys", {
    **osc(1, TRI, level=0.8), **osc(2, SINE, on=1, oct=1, level=0.35),
    **filt(LP12, 1500, 0.1, 0.1, 0.6, 0.2), **env("amp", 0.004, 0.8, 0.6, 0.4),
    **fx(ch={"Mix": 0.4, "Rate": 0.45, "Depth": 0.45}, rv={"Mix": 0.2}),
})
P["Dusty Digital"] = ("Keys", {
    **osc(1, PULSE, pw=0.4, level=0.7, drift=0.55), "noiseType": 1, "noiseLevel": 0.06, "noiseTone": -0.5,
    **filt(LP12, 4800, 0.2, 0.2, 0.6, 0.25), **env("amp", 0.002, 0.6, 0.45, 0.3), **env("filt", 0.002, 0.45, 0.25, 0.3),
    **fx(rv={"Mix": 0.3, "Size": 0.5, "Damp": 0.85}),
})
P["Plastic Piano"] = ("Keys", {
    **osc(1, SQUARE, level=0.6), **osc(2, TRI, on=1, oct=1, level=0.45),
    **filt(LP24, 3600, 0.1, 0.0, 1.0, 0.4), **env("amp", 0.001, 0.9, 0.25, 0.35), **env("filt", 0.001, 0.6, 0.15, 0.4),
    **mat(1, MS_VEL, MDST_CUT, 0.45, 0), **mat(2, MS_VEL, MDST_AMP, 0.25, 0),
    **fx(rv={"Mix": 0.18, "Size": 0.35}),
})
P["Minor Lights"] = ("Keys", {
    **osc(1, SAW, uni=3, det=12, spread=0.7, level=0.7),
    **filt(LP24, 2200, 0.18, 0.15, 0.5, 0.25), **env("amp", 0.003, 0.7, 0.55, 0.4), **env("filt", 0.002, 0.6, 0.35, 0.45),
    **fx(dly={"Sync": 1, "Div": D4, "Feedback": 0.25, "Mix": 0.18}, rv={"Mix": 0.28, "Size": 0.55}),
})

# ============================== PAD ==============================
P["Empty Room"] = ("Pad", {
    **osc(1, SUPERSAW, det=18, level=0.7, uni=1),
    **filt(LP24, 1800, 0.12, 0.1, 0.2, 0.2), **env("amp", 0.8, 1.0, 0.85, 2.4), **env("filt", 1.2, 1.5, 0.6, 2.0),
    **fx(rv={"Mix": 0.42, "Size": 0.8}, ch={"Mix": 0.3, "Rate": 0.4}),
})
P["Frozen Cinema"] = ("Pad", {
    **osc(1, SAW, level=0.65, drift=0.3), **osc(2, SAW, on=1, oct=-1, level=0.5, fine=7),
    **filt(LP24, 1200, 0.15, 0.1, 0.15, 0.25), **env("amp", 1.6, 1.0, 0.9, 3.0), **env("filt", 2.2, 1.5, 0.55, 2.5),
    **lfo(1, L_SINE, 0.12, dest=LD_CUT, amount=0.18, fade=2.0),
    "stereoWidth": 1.4,
    **fx(rv={"Mix": 0.5, "Size": 0.9, "Damp": 0.3}),
})
P["Slow Horizon"] = ("Pad", {
    **osc(1, SAW, level=0.6), **osc(2, TRI, on=1, semi=7, level=0.45, fine=-6),
    **filt(LP12, 1000, 0.1, 0.05, 0.25, 0.3), **env("amp", 2.0, 1.2, 0.85, 2.8), **env("filt", 2.6, 2.0, 0.5, 3.0),
    **fx(ch={"Mix": 0.4, "Rate": 0.3, "Depth": 0.5, "Width": 1.0}, dly={"Sync": 1, "Div": D2, "Feedback": 0.3, "Mix": 0.15},
        rv={"Mix": 0.35, "Size": 0.75}),
})
P["Violet Air"] = ("Pad", {
    **osc(1, PULSE, pw=0.3, level=0.7, drift=0.35),
    **filt(BP12, 850, 0.3, 0.0, 0.3, 0.2), **env("amp", 0.7, 1.0, 0.8, 2.2),
    **lfo(1, L_TRI, 0.25, dest=LD_PW, amount=0.35, fade=1.0),
    "stereoWidth": 1.3,
    **fx(rv={"Mix": 0.48, "Size": 0.8, "Damp": 0.4}, ch={"Mix": 0.3}),
})
P["Wide Memory"] = ("Pad", {
    **osc(1, SUPERSAW, det=26, level=0.6, uni=1), **osc(2, SUPERSAW, on=1, oct=1, det=20, level=0.35),
    **filt(LP24, 2600, 0.1, 0.1, 0.3, 0.15), **env("amp", 1.1, 1.0, 0.85, 2.6),
    "stereoWidth": 1.6,
    **fx(dly={"Sync": 1, "Div": D4D, "Feedback": 0.35, "Mix": 0.2}, rv={"Mix": 0.4, "Size": 0.85}),
})
P["Dark Cloud"] = ("Pad", {
    **osc(1, SAW, oct=-1, level=0.7), "noiseType": 1, "noiseLevel": 0.1, "noiseTone": -0.6,
    **filt(LP24, 620, 0.2, 0.2, 0.1, 0.2), **env("amp", 1.8, 1.5, 0.85, 3.2), **env("filt", 2.4, 2.0, 0.5, 3.0),
    **fx(rv={"Mix": 0.45, "Size": 0.9, "Damp": 0.75}),
})

# ============================== ATMOSPHERE ==============================
P["Deep Field"] = ("Atmosphere", {
    **osc(1, SINE, level=0.6), **osc(2, SINE, on=1, semi=7, oct=1, level=0.4),
    **filt(LP12, 3200, 0.1, 0.0), **env("amp", 2.5, 2.0, 0.9, 3.5),
    **lfo(1, L_SINE, 0.08, dest=LD_PAN, amount=0.5, fade=1.5),
    **fx(dly={"Sync": 1, "Div": D2, "Feedback": 0.55, "Mix": 0.3}, rv={"Mix": 0.55, "Size": 0.95}),
})
P["Cold Static"] = ("Atmosphere", {
    **osc(1, TRI, level=0.5), "noiseType": 1, "noiseLevel": 0.35, "noiseTone": -0.2,
    **filt(BP12, 1200, 0.35, 0.0), **env("amp", 1.5, 1.0, 0.85, 2.5),
    **lfo(1, L_SINE, 0.1, dest=LD_CUT, amount=0.4, fade=1.0),
    **fx(rv={"Mix": 0.5, "Size": 0.85}),
})
P["Night Swarm"] = ("Atmosphere", {
    **osc(1, SAW, uni=7, det=42, spread=1.0, level=0.55, drift=0.5),
    **filt(LP24, 880, 0.2, 0.15, 0.2, 0.2), **env("amp", 1.5, 1.5, 0.8, 2.8), **env("filt", 2.0, 1.5, 0.5, 2.5),
    **lfo(2, L_SH, 0.5, dest=LD_PITCH, amount=0.02, retrig=0),
    "stereoWidth": 1.5,
    **fx(rv={"Mix": 0.4, "Size": 0.8}),
})
P["Hollow Earth"] = ("Atmosphere", {
    **osc(1, SQUARE, oct=-2, level=0.6), "subOn": 1, "subLevel": 0.4,
    **filt(LP24, 300, 0.25, 0.3, 0.0, 0.15), **env("amp", 2.8, 2.0, 1.0, 4.0),
    **lfo(1, L_TRI, 0.06, dest=LD_CUT, amount=0.25, fade=2.0),
    **fx(rv={"Mix": 0.4, "Size": 0.95, "Damp": 0.6}, eq={"eqLow": 2.0}),
})

# ============================== ARPEGGIO ==============================
P["Night Grid"] = ("Arpeggio", {
    **osc(1, SAW, level=0.8),
    **filt(LP24, 1500, 0.25, 0.2, 0.4, 0.5), **env("amp", 0.001, 0.25, 0.0, 0.2), **env("filt", 0.001, 0.18, 0.0, 0.15),
    **arp(ARP_UP, A16, 0.55, 2),
    **fx(dly={"Sync": 1, "Div": D8, "Feedback": 0.4, "Mix": 0.3}, rv={"Mix": 0.18}),
})
P["Fast Orbit"] = ("Arpeggio", {
    **osc(1, SQUARE, level=0.7), **osc(2, SQUARE, on=1, oct=1, level=0.35),
    **filt(LP12, 2600, 0.3, 0.15, 0.5, 0.4), **env("amp", 0.001, 0.18, 0.0, 0.12), **env("filt", 0.001, 0.12, 0.0, 0.1),
    **arp(ARP_UPDOWN, A16, 0.4, 3),
    **fx(dly={"Sync": 1, "Div": D16, "Feedback": 0.35, "Mix": 0.25}),
})
P["Electric Rain"] = ("Arpeggio", {
    **osc(1, PULSE, pw=0.3, oct=1, level=0.65),
    **filt(LP12, 3400, 0.2, 0.0, 0.7, 0.35), **env("amp", 0.001, 0.2, 0.0, 0.25), **env("filt", 0.001, 0.15, 0.0, 0.2),
    **arp(ARP_RANDOM, A8, 0.5, 2),
    **lfo(2, L_SH, sync=1, div=LV8, dest=LD_CUT, amount=0.18, retrig=0),
    **fx(rv={"Mix": 0.32, "Size": 0.6}, dly={"Sync": 1, "Div": D8T, "Feedback": 0.3, "Mix": 0.2}),
})
P["Pulse Sequence"] = ("Arpeggio", {
    **osc(1, SAW, level=0.75), "subOn": 1, "subLevel": 0.3,
    **filt(LP24, 720, 0.35, 0.4, 0.3, 0.45), **env("amp", 0.001, 0.3, 0.2, 0.15), **env("filt", 0.001, 0.2, 0.05, 0.12),
    **arp(ARP_UP, A8, 0.7, 1),
    **fx(dist={"Drive": 0.3, "Tone": 4000, "Mix": 0.4}, dly={"Sync": 1, "Div": D8D, "Feedback": 0.3, "Mix": 0.22}),
})
P["Shadow Steps"] = ("Arpeggio", {
    **osc(1, TRI, level=0.8), **osc(2, SINE, on=1, oct=-1, level=0.4),
    **filt(LP24, 1400, 0.2, 0.1, 0.5, 0.4), **env("amp", 0.002, 0.3, 0.0, 0.3), **env("filt", 0.001, 0.22, 0.0, 0.2),
    **arp(ARP_DOWN, A16, 0.6, 2),
    **fx(dly={"Sync": 1, "Div": D4, "Feedback": 0.3, "Mix": 0.25}, rv={"Mix": 0.25, "Size": 0.6}),
})

# ============================== FX ==============================
P["Reverse Space"] = ("FX", {
    **osc(1, SAW, uni=5, det=30, spread=0.9, level=0.6),
    **filt(LP24, 900, 0.3, 0.1, 0.0, 0.6), **env("amp", 3.5, 0.5, 1.0, 0.06), **env("filt", 3.5, 1.0, 1.0, 0.1),
    **fx(rv={"Mix": 0.55, "Size": 0.95}, dly={"Sync": 1, "Div": D4, "Feedback": 0.6, "Mix": 0.35}),
})
P["Signal Fall"] = ("FX", {
    **osc(1, SQUARE, level=0.7), **osc(2, SAW, on=1, fine=30, level=0.5),
    **env("amp", 0.005, 2.2, 0.0, 0.8), **env("mod", 0.001, 2.0, 0.0, 0.5),
    "modEnvAmt": -0.8, "modEnvDest": MD_PITCH,
    **filt(LP24, 3000, 0.3, 0.2),
    **lfo(1, L_SQR, 7.5, dest=LD_AMP, amount=-0.5),
    **fx(dly={"Sync": 1, "Div": D8, "Feedback": 0.5, "Mix": 0.35}, rv={"Mix": 0.3}),
})
P["Ghost Motion"] = ("FX", {
    **osc(1, TRI, level=0.4), "noiseType": 1, "noiseLevel": 0.45, "noiseTone": -0.1,
    **filt(BP12, 600, 0.55, 0.0), **env("amp", 1.2, 1.0, 0.9, 2.0),
    **lfo(1, L_SINE, 0.09, dest=LD_CUT, amount=0.6, fade=0.5),
    **fx(rv={"Mix": 0.6, "Size": 0.9, "Damp": 0.3}),
})
P["Broken Radio"] = ("FX", {
    **osc(1, PULSE, pw=0.15, level=0.6, drift=0.8), "noiseLevel": 0.4, "noiseTone": 0.4,
    **filt(BP12, 1800, 0.4, 0.6), **env("amp", 0.01, 0.5, 0.7, 0.2),
    **lfo(1, L_SQR, 6.0, dest=LD_AMP, amount=-0.65),
    **lfo(2, L_SH, 4.0, dest=LD_CUT, amount=0.3, retrig=0),
    **fx(dist={"Drive": 0.7, "Tone": 2400, "Mix": 0.8}),
})
P["Deep Impact"] = ("FX", {
    **osc(1, SINE, oct=-2, level=0.9), "noiseLevel": 0.25, "noiseTone": -0.7,
    **env("amp", 0.001, 2.6, 0.0, 1.0), **env("mod", 0.001, 0.35, 0.0, 0.2),
    "modEnvAmt": 0.7, "modEnvDest": MD_PITCH,
    **filt(LP24, 800, 0.2, 0.4), "playMode": MONO,
    **fx(rv={"Mix": 0.5, "Size": 1.0, "Damp": 0.5}, eq={"eqLow": 3.0}),
})

# ============================== EXPERIMENTAL ==============================
P["Fold Machine"] = ("Experimental", {
    **osc(1, SQUARE, level=0.7), **osc(2, SQUARE, on=1, fine=32, level=0.65),
    **filt(LP24, 1500, 0.5, 0.9), **env("amp", 0.002, 0.6, 0.6, 0.2),
    **lfo(2, L_SH, sync=1, div=LV8, dest=LD_PW, amount=0.5, retrig=0),
    **fx(dist={"Drive": 0.85, "Tone": 3600, "Mix": 0.9}),
})
P["Random Walk"] = ("Experimental", {
    **osc(1, TRI, level=0.75),
    **filt(LP12, 2000, 0.3, 0.1),
    **lfo(1, L_SH, sync=1, div=LV8, dest=LD_PITCH, amount=0.28, retrig=0),
    **env("amp", 0.005, 0.4, 0.6, 0.3),
    **fx(dly={"Sync": 1, "Div": D8D, "Feedback": 0.55, "Mix": 0.35}, rv={"Mix": 0.25}),
})
P["Metal Garden"] = ("Experimental", {
    **osc(1, SQUARE, level=0.6), **osc(2, SQUARE, on=1, semi=1, oct=1, level=0.55, fine=45),
    **filt(BP12, 2100, 0.6, 0.3), **env("amp", 0.001, 0.7, 0.3, 0.5), **env("filt", 0.001, 0.5, 0.2, 0.4),
    **fx(dly={"Sync": 1, "Div": D4T, "Feedback": 0.5, "Mix": 0.3}, rv={"Mix": 0.3, "Size": 0.7}),
})
P["Glitch Bloom"] = ("Experimental", {
    **osc(1, SAW, level=0.7), **osc(2, PULSE, on=1, pw=0.2, oct=1, level=0.5),
    **filt(LP24, 2400, 0.4, 0.3, 0.0, 0.5), **env("amp", 0.001, 0.12, 0.0, 0.1), **env("filt", 0.001, 0.08, 0.0, 0.08),
    **arp(ARP_RANDOM, A32, 0.5, 2),
    **lfo(2, L_SH, sync=1, div=LV16, dest=LD_CUT, amount=0.3, retrig=0),
    **fx(dist={"Drive": 0.4, "Tone": 5000, "Mix": 0.5}, dly={"Sync": 1, "Div": D16, "Feedback": 0.45, "Mix": 0.3}),
})

# ======================================================================
# v1.2 factory presets — the new BASIC HQ / WAVETABLE engines on show.
# Every entry sets qualityMode HIGH (the intended default for new sounds);
# LEGACY presets above remain byte-identical. Poly unless noted.
# ======================================================================

# ---------------- Bass ----------------
P["Neon Vector Bass"] = ("Bass", {
    **osc(1, engine=E_WT, wtpos=0.35, warpmode=W_ASYM, warpamt=0.4, level=0.8, uni=3, det=14, spread=0.5),
    **osc(2, SINE, engine=E_HQ, on=1, oct=-1, level=0.55),
    "qualityMode": Q_HIGH, "subOn": 1, "subLevel": 0.25,
    **filt(LADDER24, 380, 0.35, 0.3, 0.1, 0.5), **env("amp", 0.002, 0.4, 0.6, 0.12),
    **env("filt", 0.002, 0.28, 0.08, 0.15), **curves("amp", d=0.4),
    **mat(1, MS_WHEEL, MDST_O1WT, 0.5, 0),
    "playMode": MONO, "glideTime": 0.04,
    **fx(eq={"eqLow": 2.0}),
})
P["Sub Divide"] = ("Bass", {
    **osc(1, PULSE, engine=E_HQ, pw=0.22, level=0.75, drift=0.1),
    **osc(2, SINE, engine=E_HQ, on=1, oct=-1, level=0.7),
    "qualityMode": Q_HIGH,
    **filt(OTA24, 520, 0.3, 0.45, 0.0, 0.4), **env("amp", 0.002, 0.5, 0.7, 0.1),
    **env("filt", 0.001, 0.3, 0.12, 0.12),
    **mat(1, MS_VEL, MDST_CUT, 0.3, 0),
    **fx(dist={"Drive": 0.3, "Tone": 2800, "Mix": 0.5}, eq={"eqLow": 1.5}),
})

# ---------------- Lead ----------------
P["Chrome Cutter"] = ("Lead", {
    **osc(1, engine=E_WT, wtpos=0.25, warpmode=W_SYNC, warpamt=0.3, level=0.75, uni=3, det=10, spread=0.6),
    **osc(2, engine=E_WT, on=1, wtpos=0.6, semi=12, level=0.35),
    "qualityMode": Q_HIGH,
    **filt(LP24, 4200, 0.2, 0.15, 0.3, 0.3), **env("amp", 0.004, 0.3, 0.8, 0.2),
    **env("filt", 0.003, 0.4, 0.4, 0.25),
    **mat(1, MS_WHEEL, MDST_O1WARP, 0.6, 0), **mat(2, MS_AT, MDST_O1WT, 0.4, 0),
    **fx(dly={"Sync": 1, "Div": D8T, "Feedback": 0.3, "Mix": 0.22}, rv={"Mix": 0.18, "Size": 0.45}),
})
W["Chrome Cutter"] = {"osc1": "FM Chrome", "osc2": "Soft Glass"}
P["Glass Caller"] = ("Lead", {
    **osc(1, engine=E_WT, wtpos=0.4, level=0.8, uni=1, drift=0.2),
    "qualityMode": Q_HIGH,
    **filt(SEM12, 3200, 0.35, 0.2, 0.4, 0.35), **env("amp", 0.01, 0.35, 0.75, 0.3),
    **env("filt", 0.01, 0.5, 0.5, 0.3), **curves("amp", a=-0.3),
    **lfo(1, L_SINE, 5.2, dest=LD_WTPOS, amount=0.18, fade=0.9),
    "playMode": LEGATO, "glideTime": 0.06,
    **fx(rv={"Mix": 0.25, "Size": 0.55}),
})
W["Glass Caller"] = {"osc1": "Vocal Glass"}

# ---------------- Pad ----------------
P["Tidal Bloom"] = ("Pad", {
    **osc(1, engine=E_WT, wtpos=0.2, level=0.7, uni=5, det=16, spread=0.85, drift=0.3),
    **osc(2, engine=E_WT, on=1, wtpos=0.7, fine=-8, level=0.5),
    "qualityMode": Q_HIGH,
    **filt(LP12, 2600, 0.15, 0.0, 0.2, 0.2), **env("amp", 0.9, 1.2, 0.85, 1.6),
    **env("filt", 1.2, 1.5, 0.6, 1.4), **curves("amp", a=-0.5, r=-0.4),
    **lfo(1, L_SINE, 0.07, dest=LD_WTPOS, amount=0.35, fade=2.0),
    **fx(ch={"Mix": 0.35, "Rate": 0.4, "Depth": 0.4}, rv={"Mix": 0.4, "Size": 0.8}),
})
W["Tidal Bloom"] = {"osc1": "Slow Tide", "osc2": "Breath Pad"}
P["Velvet Choir"] = ("Pad", {
    **osc(1, engine=E_WT, wtpos=0.3, level=0.65, uni=5, det=12, spread=0.9),
    **osc(2, TRI, engine=E_HQ, on=1, oct=-1, level=0.4),
    "qualityMode": Q_HIGH,
    **filt(LP24, 1900, 0.2, 0.1, 0.25, 0.25), **env("amp", 0.7, 1.0, 0.8, 1.3),
    **env("filt", 0.9, 1.2, 0.55, 1.2), **curves("amp", a=-0.6),
    **mat(1, MS_WHEEL, MDST_O1WT, 0.6, 0), **mat(2, MS_LFO2, MDST_O1PAN, 0.25, 1),
    **lfo(2, L_SINE, 0.11, retrig=0),
    **fx(rv={"Mix": 0.35, "Size": 0.7, "Damp": 0.6}),
})
W["Velvet Choir"] = {"osc1": "Vowel Morph"}

# ---------------- Pluck ----------------
P["Bright Prime"] = ("Pluck", {
    **osc(1, SQUARE, engine=E_HQ, level=0.75, uni=3, det=8, spread=0.5),
    **osc(2, SAW, engine=E_HQ, on=1, semi=12, level=0.3),
    "qualityMode": Q_HIGH,
    **filt(SEM12, 1500, 0.3, 0.15, 0.5, 0.55), **env("amp", 0.001, 0.28, 0.0, 0.25),
    **env("filt", 0.001, 0.2, 0.0, 0.2), **curves("amp", d=0.5), **curves("filt", d=0.5),
    **mat(1, MS_VEL, MDST_CUT, 0.4, 0),
    **fx(dly={"Sync": 1, "Div": D8, "Feedback": 0.25, "Mix": 0.2}),
})
P["Orbit Pluck"] = ("Pluck", {
    **osc(1, engine=E_WT, wtpos=0.0, level=0.8, uni=1),
    "qualityMode": Q_HIGH,
    **filt(LP24, 3800, 0.25, 0.1, 0.4, 0.4), **env("amp", 0.001, 0.22, 0.0, 0.3),
    **env("filt", 0.001, 0.18, 0.0, 0.25), **curves("amp", d=0.6),
    **mat(1, MS_AENV, MDST_O1WT, 0.55, 0), **mat(2, MS_VEL, MDST_AMP, 0.25, 0),
    **fx(dly={"Sync": 1, "Div": D4T, "Feedback": 0.35, "Mix": 0.25}, rv={"Mix": 0.15, "Size": 0.4}),
})
W["Orbit Pluck"] = {"osc1": "Orbit"}

# ---------------- Keys ----------------
P["Glass Keys"] = ("Keys", {
    **osc(1, engine=E_WT, wtpos=0.55, level=0.7, uni=1, drift=0.15),
    **osc(2, SINE, engine=E_HQ, on=1, oct=1, level=0.2, pan=0.3),
    "qualityMode": Q_HIGH,
    **filt(LP12, 5200, 0.1, 0.0, 0.5, 0.2), **env("amp", 0.002, 0.6, 0.35, 0.5),
    **env("filt", 0.002, 0.7, 0.3, 0.5), **curves("amp", d=0.3, r=-0.3),
    **mat(1, MS_VEL, MDST_O1WT, 0.3, 0),
    **fx(ch={"Mix": 0.25, "Rate": 0.7, "Depth": 0.25}, rv={"Mix": 0.2, "Size": 0.5}),
})
W["Glass Keys"] = {"osc1": "Soft Glass"}
P["EP Drift"] = ("Keys", {
    **osc(1, TRI, engine=E_HQ, level=0.75, drift=0.35),
    **osc(2, SINE, engine=E_HQ, on=1, oct=1, semi=0, fine=4, level=0.25),
    "qualityMode": Q_HIGH,
    **filt(OTA24, 2400, 0.2, 0.3, 0.4, 0.3), **env("amp", 0.003, 0.8, 0.5, 0.4),
    **env("filt", 0.002, 0.6, 0.4, 0.4),
    **mat(1, MS_VEL, MDST_CUT, 0.35, 0), **mat(2, MS_AT, MDST_L1R, 0.2, 0),
    **lfo(1, L_SINE, 4.5, dest=LD_AMP, amount=0.08, fade=1.2),
    **fx(ch={"Mix": 0.3, "Rate": 0.9, "Depth": 0.3}, eq={"eqMid": -1.0, "eqHigh": 1.0}),
})

# ---------------- Sequence ----------------
P["Vector Steps"] = ("Sequence", {
    **osc(1, engine=E_WT, wtpos=0.15, level=0.75, uni=1),
    **osc(2, PULSE, engine=E_HQ, on=1, pw=0.3, oct=-1, level=0.45),
    "qualityMode": Q_HIGH,
    **filt(LP24, 1400, 0.4, 0.25, 0.2, 0.5), **env("amp", 0.001, 0.16, 0.0, 0.12),
    **env("filt", 0.001, 0.12, 0.0, 0.1),
    **arp(ARP_UPDOWN, A16, 0.55, 2),
    **lfo(1, L_TRI, sync=1, div=LV1, dest=LD_WTPOS, amount=0.5, retrig=0),
    **fx(dly={"Sync": 1, "Div": D8T, "Feedback": 0.3, "Mix": 0.2}),
})
W["Vector Steps"] = {"osc1": "Digital Steps"}
P["Async Runner"] = ("Sequence", {
    **osc(1, SAW, engine=E_HQ, level=0.8, uni=3, det=12, spread=0.4),
    "qualityMode": Q_HIGH, "subOn": 1, "subLevel": 0.3,
    **filt(LADDER24, 900, 0.5, 0.35, 0.15, 0.55), **env("amp", 0.001, 0.2, 0.1, 0.15),
    **env("filt", 0.001, 0.15, 0.0, 0.12), **curves("filt", d=0.6),
    **arp(ARP_RANDOM, A16, 0.5, 1),
    **mat(1, MS_LFO2, MDST_CUT, 0.25, 1),
    **lfo(2, L_SH, sync=1, div=LV8, retrig=0),
    **fx(dist={"Drive": 0.25, "Tone": 4200, "Mix": 0.4}, dly={"Sync": 1, "Div": D16, "Feedback": 0.3, "Mix": 0.18}),
})

# ---------------- Atmosphere ----------------
P["Breath Field"] = ("Atmosphere", {
    **osc(1, engine=E_WT, wtpos=0.1, level=0.6, uni=5, det=20, spread=1.0, drift=0.4),
    "qualityMode": Q_HIGH, "noiseType": 1, "noiseLevel": 0.06, "noiseTone": -0.5,
    **filt(LP12, 1600, 0.1, 0.0, 0.1, 0.3), **env("amp", 1.8, 2.0, 0.8, 2.5),
    **env("filt", 2.5, 2.0, 0.5, 2.0), **curves("amp", a=-0.7, r=-0.5),
    **lfo(1, L_SINE, 0.05, dest=LD_WTPOS, amount=0.6, fade=3.0),
    **fx(rv={"Mix": 0.5, "Size": 0.9, "Damp": 0.4}),
})
W["Breath Field"] = {"osc1": "Breath Pad"}
P["Slow Alloy"] = ("Atmosphere", {
    **osc(1, engine=E_WT, wtpos=0.0, warpmode=W_MIRROR, warpamt=0.3, level=0.6, uni=1),
    **osc(2, engine=E_WT, on=1, wtpos=0.5, oct=-1, level=0.4),
    "qualityMode": Q_HIGH,
    **filt(BP24, 1100, 0.35, 0.0, 0.0, 0.3), **env("amp", 1.2, 1.8, 0.7, 2.2),
    **env("filt", 1.5, 2.0, 0.5, 2.0),
    **mat(1, MS_LFO1, MDST_O1WT, 0.5, 1), **mat(2, MS_LFO2, MDST_O2WT, 0.4, 1),
    **lfo(1, L_SINE, 0.06, retrig=0), **lfo(2, L_TRI, 0.043, retrig=0),
    **fx(rv={"Mix": 0.45, "Size": 0.85}, dly={"Sync": 1, "Div": D2, "Feedback": 0.4, "Mix": 0.2}),
})
W["Slow Alloy"] = {"osc1": "Bell Alloy", "osc2": "Ring Shift"}

# ---------------- FX ----------------
P["Riser Fold"] = ("FX", {
    **osc(1, engine=E_WT, wtpos=0.0, warpmode=W_BENDP, warpamt=0.5, level=0.7, uni=5, det=30, spread=0.9),
    "qualityMode": Q_HIGH,
    **filt(HP12, 300, 0.3, 0.0, 0.0, 0.6), **env("amp", 2.5, 1.0, 1.0, 1.5),
    **env("filt", 3.5, 1.0, 1.0, 1.0), **env("mod", 3.5, 1.0, 1.0, 1.0),
    "modEnvAmt": 0.8, "modEnvDest": MD_PITCH,
    **mat(1, MS_MENV, MDST_O1WT, 0.9, 0),
    **fx(rv={"Mix": 0.4, "Size": 0.8}, dly={"Sync": 1, "Div": D8, "Feedback": 0.45, "Mix": 0.3}),
})
W["Riser Fold"] = {"osc1": "Fractal Fold"}
P["Glitch Signal"] = ("FX", {
    **osc(1, engine=E_WT, wtpos=0.5, level=0.75, uni=1),
    "qualityMode": Q_HIGH,
    **filt(BP12, 1800, 0.5, 0.2, 0.0, 0.4), **env("amp", 0.001, 0.3, 0.4, 0.2),
    **env("filt", 0.001, 0.2, 0.3, 0.2),
    **mat(1, MS_LFO2, MDST_O1WT, 0.8, 1), **mat(2, MS_LFO2, MDST_CUT, 0.3, 1),
    **lfo(2, L_SH, sync=1, div=LV16, retrig=0),
    **fx(dist={"Drive": 0.35, "Tone": 6000, "Mix": 0.45}, dly={"Sync": 1, "Div": D16, "Feedback": 0.5, "Mix": 0.35}),
})
W["Glitch Signal"] = {"osc1": "Glitch Line"}

# ---------------- Digital ----------------
P["Bit Sweep"] = ("Digital", {
    **osc(1, engine=E_WT, wtpos=0.0, level=0.8, uni=1),
    **osc(2, engine=E_WT, on=1, wtpos=0.3, semi=-12, level=0.4),
    "qualityMode": Q_HIGH,
    **filt(LP24, 6000, 0.15, 0.1, 0.3, 0.2), **env("amp", 0.002, 0.4, 0.6, 0.25),
    **env("filt", 0.002, 0.4, 0.5, 0.25),
    **lfo(1, L_TRI, 0.5, dest=LD_WTPOS, amount=0.5, fade=0.5),
    **mat(1, MS_WHEEL, MDST_O1WARP, 0.4, 0),
    **fx(ch={"Mix": 0.2, "Rate": 0.8, "Depth": 0.2}),
})
W["Bit Sweep"] = {"osc1": "Bitframe", "osc2": "Pulse Matrix"}
P["Comb Circuit"] = ("Digital", {
    **osc(1, engine=E_WT, wtpos=0.4, warpmode=W_SYNC, warpamt=0.25, level=0.75, uni=3, det=6, spread=0.6),
    "qualityMode": Q_HIGH,
    **filt(SEM12, 2600, 0.4, 0.1, 0.35, 0.4), **env("amp", 0.003, 0.5, 0.6, 0.3),
    **env("filt", 0.002, 0.5, 0.4, 0.3),
    **mat(1, MS_AT, MDST_O1WARP, 0.5, 0), **mat(2, MS_VEL, MDST_O1WT, 0.3, 0),
    **fx(dly={"Sync": 1, "Div": D8D, "Feedback": 0.35, "Mix": 0.25}, rv={"Mix": 0.15, "Size": 0.5}),
})
W["Comb Circuit"] = {"osc1": "Spectral Comb"}

# ---------------- Analog ----------------
P["Analog Estate"] = ("Analog", {
    **osc(1, SUPERSAW, engine=E_HQ, det=22, level=0.7, uni=1, drift=0.25),
    **osc(2, SAW, engine=E_HQ, on=1, oct=-1, fine=-6, level=0.45),
    "qualityMode": Q_HIGH, "subOn": 1, "subLevel": 0.2,
    **filt(LADDER24, 1500, 0.4, 0.4, 0.2, 0.45), **env("amp", 0.005, 0.6, 0.7, 0.35),
    **env("filt", 0.004, 0.5, 0.4, 0.35), **curves("amp", a=-0.2),
    **mat(1, MS_WHEEL, MDST_CUT, 0.45, 0), **mat(2, MS_VEL, MDST_DRIVE, 0.35, 0),
    **fx(ch={"Mix": 0.25, "Rate": 0.5, "Depth": 0.3}, eq={"eqLow": 1.0, "eqHigh": 0.5}),
})
P["Warm Stack"] = ("Analog", {
    **osc(1, engine=E_WT, wtpos=0.3, level=0.7, uni=3, det=10, spread=0.7, drift=0.3),
    **osc(2, TRI, engine=E_HQ, on=1, oct=-1, level=0.35),
    "qualityMode": Q_HIGH,
    **filt(OTA24, 1900, 0.3, 0.35, 0.25, 0.3), **env("amp", 0.01, 0.7, 0.75, 0.5),
    **env("filt", 0.01, 0.6, 0.5, 0.5), **curves("amp", r=-0.3),
    **lfo(1, L_SINE, 0.09, dest=LD_WTPOS, amount=0.25, fade=1.5, retrig=0),
    **fx(ch={"Mix": 0.3, "Rate": 0.45, "Depth": 0.35}, rv={"Mix": 0.18, "Size": 0.5}),
})
W["Warm Stack"] = {"osc1": "Vintage Stack"}

# ============================== descriptions ==============================
DESC = {
    "Dark Pressure":  "Heavy mono bass with a squashed LP24 bite and sub support.",
    "Rust Bass":      "Corroded pulse-and-saw bass driven through gritty distortion.",
    "Night Driver":   "Supersaw synthwave bass with a tight filter envelope snap.",
    "Low Orbit":      "Round triangle bass slowly breathing through the filter.",
    "Concrete Bass":  "Industrial square stack with a hard fifth and drive.",
    "Toxic Mono":     "Acidic legato bass with S&H cutoff jitter and drive.",
    "Sub Voltage":    "Pure sine-plus-square sub for maximum low end.",
    "Chrome 808":     "808-style knock: pitch-dropped sine with long decay.",
    "Deep Floor":     "Soft sine sub with a hint of triangle body.",
    "Round Cellar":   "Warm triangle sub with slow filter movement.",
    "Neon Bite":      "Bright supersaw lead with vibrato, delay and shimmer.",
    "Cold Razor":     "Cutting square/saw mono lead with tempo-locked echoes.",
    "Digital Cry":    "PWM lead with a pitch-drop attack and triplet delay.",
    "Broken Signal":  "Glitchy S&H-modulated lead with distorted edges.",
    "Blue Flame":     "Legato solo lead with expressive glide and mod-wheel cutoff.",
    "Acid Skyline":   "Screaming 303-style resonant mono line with drive.",
    "Glass Steps":    "Bell-like sine/triangle pluck with airy echoes.",
    "Metro Pluck":    "Punchy filtered saw pluck for tight sequences.",
    "Crystal Wire":   "Glassy pulse pluck with sparkle chorus.",
    "Soft Attack":    "Gentle triangle pluck with a velvet room tail.",
    "Arcade Dust":    "Chippy 8-bit style square pluck with grit.",
    "Velvet Pulse":   "Smooth PWM pluck with a slow-breathing width.",
    "Analog Room":    "Classic saw/square poly keys with warm chorus.",
    "Soft Machine":   "Mellow triangle keys with wide slow chorus.",
    "Dusty Digital":  "Lo-fi drifting keys with pink-noise air.",
    "Plastic Piano":  "Percussive velocity-sensitive digital keys.",
    "Minor Lights":   "Detuned unison keys with echo and hall.",
    "Empty Room":     "Slow supersaw pad drifting in a large hall.",
    "Frozen Cinema":  "Widescreen cinematic pad with glacial filter sweep.",
    "Slow Horizon":   "Layered fifth pad with long attack and echoes.",
    "Violet Air":     "Hollow band-pass pad with breathing pulse width.",
    "Wide Memory":    "Double supersaw pad stretched across the stereo field.",
    "Dark Cloud":     "Low ominous pad with pink-noise haze.",
    "Deep Field":     "Sparse sine layers echoing into deep space.",
    "Cold Static":    "Filtered noise-bed texture with slow sweeps.",
    "Night Swarm":    "Detuned seven-voice cluster with random pitch drift.",
    "Hollow Earth":   "Subterranean drone with slow filter breathing.",
    "Night Grid":     "Two-octave 1/16 arpeggio with synced echoes.",
    "Fast Orbit":     "Three-octave up/down arpeggio at high energy.",
    "Electric Rain":  "Random 1/8 arpeggio with S&H sparkle.",
    "Pulse Sequence": "Driving 1/8 arp with sub weight and drive.",
    "Shadow Steps":   "Descending minor-mood arpeggio with echo trails.",
    "Reverse Space":  "Swelling reverse-style riser into a huge wash.",
    "Signal Fall":    "Falling pitch effect with gated tremolo.",
    "Ghost Motion":   "Haunted band-pass noise drifting slowly.",
    "Broken Radio":   "Detuned static transmission with chopper AM.",
    "Deep Impact":    "Cinematic sub drop with noise rumble.",
    "Fold Machine":   "Aggressive folded squares with S&H pulse width.",
    "Random Walk":    "Melodic S&H pitch wander with dub echoes.",
    "Metal Garden":   "Inharmonic metallic clangs with resonant ring.",
    "Glitch Bloom":   "Hyperactive 1/32 random arp with bitcrush-like grit.",

    # ---- v1.2 wavetable/HQ presets ----
    "Neon Vector Bass": "Wavetable saw-stack bass with asymmetry warp; wheel scans the table.",
    "Sub Divide":       "HQ pulse bass over a pure sub sine through the OTA filter.",
    "Chrome Cutter":    "FM Chrome sync lead - wheel pushes the sync, aftertouch scans.",
    "Glass Caller":     "Vocal Glass legato lead with a slow position vibrato.",
    "Tidal Bloom":      "Slow Tide pad breathing across the table under an LFO tide.",
    "Velvet Choir":     "Vowel Morph pad - the wheel glides A-E-I-O-U across the choir.",
    "Bright Prime":     "Snappy HQ square pluck through the SEM-style filter.",
    "Orbit Pluck":      "Orbit wavetable pluck; the amp envelope rides the position.",
    "Glass Keys":       "Soft Glass keys with velocity-scanned sparkle.",
    "EP Drift":         "Drifting HQ triangle EP with OTA warmth and slow tremolo.",
    "Vector Steps":     "Digital Steps arp; a synced triangle LFO walks the table each bar.",
    "Async Runner":     "Random HQ saw sequence through a snappy ladder filter.",
    "Breath Field":     "Breath Pad atmosphere swelling across three octaves of air.",
    "Slow Alloy":       "Bell Alloy and Ring Shift drifting on free LFOs with mirror warp.",
    "Riser Fold":       "Fractal Fold riser - the mod envelope drags pitch and position up.",
    "Glitch Signal":    "Glitch Line stutter - S&H jumps the table and the band-pass.",
    "Bit Sweep":        "Bitframe crush sweep over a Pulse Matrix sub layer.",
    "Comb Circuit":     "Spectral Comb with light sync warp - aftertouch tightens the comb.",
    "Analog Estate":    "HQ supersaw estate pad-lead through the ladder, velocity drives.",
    "Warm Stack":       "Vintage Stack beating slowly through the OTA filter.",
}

# ============================== write ==============================

# factory wavetable bank names (must mirror Source/Engine/WavetableFactory.cpp)
VALID_BANKS = {
    "Prime Shapes", "Analog Warmth", "Vintage Stack",
    "Bitframe", "Pulse Matrix", "Digital Steps",
    "Harmonic Rise", "Odd Order", "Spectral Comb",
    "Vowel Morph", "Formant Sweep", "Vocal Glass",
    "Bell Alloy", "FM Chrome", "Ring Shift",
    "Orbit", "Slow Tide", "Wave Traveller",
    "Soft Glass", "Breath Pad", "Velvet",
    "Saw Stack", "Growl Formant", "Torn Edge",
    "Random Walk", "Fractal Fold", "Glitch Line",
}

# original 51 preset names (frozen: favorites/state are name-keyed)
LEGACY_COUNT = 51

def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".json"):
            os.remove(os.path.join(OUT, f))
    cats = {}
    bundle = []
    missing_desc = [n for n in P if n not in DESC]
    assert not missing_desc, f"presets missing descriptions: {missing_desc}"
    for name, refs in W.items():
        assert name in P, f"wavetable refs for unknown preset: {name}"
        for bank in refs.values():
            assert bank in VALID_BANKS, f"unknown wavetable bank '{bank}' in preset '{name}'"
    for name, (cat, params) in P.items():
        cats.setdefault(cat, []).append(name)
        doc = {"name": name, "category": cat, "author": AUTHOR,
               "description": DESC[name], "format": "YDCore-1",
               "params": {k: (round(v, 6) if isinstance(v, float) else v) for k, v in sorted(params.items())}}
        if name in W:
            doc["wavetables"] = W[name]      # appended key; older loaders ignore it
        bundle.append(doc)
        safe = re.sub(r"[^A-Za-z0-9]+", "_", name)
        with open(os.path.join(OUT, f"{safe}.json"), "w") as fh:
            json.dump(doc, fh, indent=1)
    # single-file bundle: lets a fresh checkout build with zero interpreter deps
    # (CMake splits it at configure time when the individual files are absent)
    with open(os.path.join(OUT, "factory_bundle.json"), "w") as fh:
        json.dump(bundle, fh, indent=1)
    total = len(P)
    print(f"Wrote {total} presets (+ factory_bundle.json):")
    for c in ["Bass", "Sub Bass", "Lead", "Pluck", "Keys", "Pad", "Atmosphere", "Arpeggio",
              "FX", "Experimental", "Sequence", "Digital", "Analog"]:
        print(f"  {c}: {len(cats.get(c, []))}")
    assert total >= LEGACY_COUNT + 18, f"need the 51 legacy presets plus 18+ v1.2 presets, got {total}"
    legacy_cats = {c: v for c, v in cats.items() if c not in ("Sequence", "Digital", "Analog")}
    assert all(len(v) >= 4 for v in legacy_cats.values()), "each legacy category needs 4+"
    assert all(len(v) >= 2 for v in cats.values()), "each category needs 2+"
    return 0

if __name__ == "__main__":
    sys.exit(main())
