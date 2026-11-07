#!/usr/bin/env python3
"""Makes Helix Native's side of rs-tonehost from gear.json and the Helix install's own model files.

    make_presets.py OUTDIR

OUTDIR gets setlist.hls, one preset per Helix amp to import once, and tones.json, what the host's config needs: the
presets in order and the rules from the game's knobs to parameters. Every value not set here is Line 6's default, read
from the install, and nothing of Line 6's is kept in the repository. Each preset is the same chain, so the knobs and
switches mean the same everywhere: octave up and down, wah, fuzz, metal, drive, amp and its default cab, compressor, then
chorus, flanger, phaser, tremolo, delay and reverb on the second path, as a path has room for eight blocks. README.md has
what was learned about Helix's files.
"""
import base64, json, os, sys, time, zlib

RES = "/Library/Application Support/Line 6/Helix Native/res"
NATIVE = 2162944  # Helix Native in Helix compatibility mode
ORDER = ["octave_up", "octave_down", "wah", "fuzz", "metal", "drive", None, "compressor",  # None: the amp
         "chorus", "flanger", "phaser", "tremolo", "delay", "reverb"]
PATH_ONE = 8  # blocks on the first path; the rest go on the second
TO_PATH_2A = 2  # @output of the first path
SWITCH = 16  # Audio Unit parameters: knobs 0-15, switches 16-31
AMP_KNOBS = {"Drive": 0, "Bass": 1, "Mid": 2, "Treble": 3, "Presence": 4}
BLOCK_TYPE = {"drive": 0, "compressor": 0, "chorus": 0, "phaser": 0, "tremolo": 0, "delay": 0, "reverb": 0}


def models():
    out = {}
    for f in os.listdir(RES):
        if f.endswith(".models"):
            data = json.load(open(os.path.join(RES, f)))
            for m in data if isinstance(data, list) else []:
                if isinstance(m, dict) and m.get("symbolicID"):
                    out[m["symbolicID"]] = m
    return out


def defaults(m):
    return {p["symbolicID"]: p["default"] for p in m["params"] if not p["symbolicID"].startswith("@") and "default" in p}


def block(m, position, enabled, extra=None):
    b = {"@model": m["symbolicID"], "@position": position, "@path": 0, "@type": 0, "@enabled": enabled, "@no_snapshot_bypass": False, "@stereo": False}
    b.update(defaults(m))
    b.update(extra or {})
    return b


def preset(name, amp, base, M, gear):
    h = json.loads(json.dumps(base))
    t = h["data"]["tone"] if "data" in h else h["tone"]
    d, second = t["dsp0"], t["dsp1"]
    for dsp in (d, second):
        for k in [k for k in dsp if k.startswith(("block", "cab"))]:
            del dsp[k]
    d["outputA"]["@output"] = TO_PATH_2A
    second["inputA"]["@input"] = 0
    second["outputA"]["@output"] = 1
    assign, knob_default, blocks = [None] * (SWITCH + 13), {}, {"dsp0": {}, "dsp1": {}}
    sw = 0
    for index, kind in enumerate(ORDER):
        dsp_name, pos = ("dsp0", index) if index < PATH_ONE else ("dsp1", index - PATH_ONE)
        key = "block%d" % pos
        if kind is None:
            m = M["HD2_" + amp]
            a = block(m, pos, True, {"@type": 3, "@cab": "cab0", "@bypassvolume": 1})
            a.pop("@stereo")
            d[key] = a
            cab = M[m.get("ircablink") or "HD2_CabMicIr_4x12Greenback25"]
            d["cab0"] = {"@model": cab["symbolicID"], "@enabled": True}
            d["cab0"].update(defaults(cab))
            blocks["dsp0"][key] = True
            have = defaults(m)
            for p, k in AMP_KNOBS.items():
                if p in have:
                    assign[k] = {"@param": "dsp0.%s.%s" % (key, p), "@min": 0, "@max": 1}
                    knob_default[str(k)] = have[p]
            continue
        spec = gear["blocks"][kind]
        m = M[spec["model"]]
        dsp = t[dsp_name]
        dsp[key] = block(m, pos, False, spec.get("set"))
        blocks[dsp_name][key] = False
        ranges = {p["symbolicID"]: (p["min"], p["max"]) for p in m["params"] if "min" in p}
        for p, k in spec.get("knobs", {}).items():
            lo, hi = ranges[p]
            if p == "Time":
                hi = 2.0  # of 8 s: a knob's whole travel for the times the game's pedals have
            assign[k] = {"@param": "%s.%s.%s" % (dsp_name, key, p), "@min": lo, "@max": hi}
            knob_default[str(k)] = (dsp[key][p] - lo) / (hi - lo)
        assign[SWITCH + sw] = {"@param": "%s.%s.@enabled" % (dsp_name, key), "@min": False, "@max": True}
        sw += 1
    while assign and assign[-1] is None:
        assign.pop()
    t["automation"] = {"@assignments": assign}
    t.pop("footswitch", None)
    t.pop("controller", None)
    d["inputA"]["noiseGate"] = False
    for s in [k for k in t if k.startswith("snapshot")]:
        if "blocks" in t[s]:
            t[s]["blocks"] = json.loads(json.dumps(blocks))
        t[s].pop("controllers", None)
    meta = h["data"]["meta"] if "data" in h else h["meta"]
    meta["name"] = name[:16]
    return (h["data"] if "data" in h else h), knob_default


def rules(gear):
    """The game's knobs onto parameters, the same for every preset. A value is scale * knob + offset."""
    # The input: 39.5 comes from a fit of a JCM800 with a drive pedal, and is too much for a tone with no pedal
    pedals = sorted(gear["blocks"]["drive"]["gear"])
    f = [{"param": 0, "from": "amp_gain", "scale": 0.93, "offset": 0},
         {"param": 32, "from": "amp_gain", "scale": 39.5, "offset": -14, "with": pedals},
         {"param": 32, "from": "amp_gain", "scale": 28, "offset": -14, "without": pedals}]
    for suffix, k in (("_Bass", 1), ("_Mid", 2), ("_Treble", 3), ("_Pres", 4)):
        f.append({"param": k, "from": suffix, "contains": "Amp_", "scale": 0.01, "offset": 0})
    how = {("drive", "Gain"): ("_Gain", 0.0098, 0), ("drive", "Tone"): ("_Tone", 0.01, 0),
           ("compressor", "Threshold"): ("_Threshold", 1 / 60, 1), ("fuzz", "Sustain"): ("_Gain", 0.0098, 0), ("metal", "Drive"): ("_Gain", 0.0098, 0),
           ("chorus", "Mix"): ("_Mix", 0.01, 0), ("flanger", "Rate"): ("_Rate", 0.01, 0),
           ("delay", "Time"): ("_Time", 0.0005, 0), ("delay", "Feedback"): ("_Feedback", 0.01, 0), ("delay", "Mix"): ("_Mix", 0.01, 0),
           ("reverb", "Mix"): ("_Mix", 0.01, 0)}
    other = {"_Comp": (-1 / 150, 1), "_Compress": (-1 / 150, 1), "_Smash": (-1 / 150, 1), "_Limit": (-1 / 150, 1), "_Boost": (0.0098, 0), "_RateTen": (0.1, 0), "_GainWide": (0.0065, 0)}
    sw = 0
    for kind in [k for k in ORDER if k]:
        spec = gear["blocks"][kind]
        f.append({"param": SWITCH + sw, "present": sorted(spec["gear"])})
        sw += 1
        for key, names in spec["gear"].items():
            for p, k in spec.get("knobs", {}).items():
                suffix, scale, offset = how[(kind, p)]
                if p in names:
                    suffix = names[p]
                    scale, offset = other.get(suffix, (scale, offset))
                    suffix = {"_RateTen": "_Rate", "_GainWide": "_Gain"}.get(suffix, suffix)  # the knob's real name, on another scale
                f.append({"param": k, "from": suffix, "prefix": key + "_", "scale": round(scale, 6), "offset": offset})
    return f


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    here = os.path.dirname(os.path.abspath(__file__))
    gear = json.load(open(os.path.join(here, "gear.json")))
    M = models()
    base = json.load(open(os.path.join(here, "base.hlx")))
    used = sorted(set(gear["amps"].values()) | set(gear["fallback"].values()))
    missing = [a for a in used if "HD2_" + a not in M]
    if missing:
        sys.exit("not in this Helix: %s" % missing)
    presets, templates = [], {}
    for i, amp in enumerate(used):
        p, knobs = preset(amp[3:], amp, base, M, gear)
        presets.append(p)
        templates[amp] = {"slot": i, "state": amp + ".plist", "output_db": 0, "params": knobs}
        if amp in gear["high_gain"]:  # gain enough of their own: less drive and input for the same knob, a tighter gate
            templates[amp]["tight"] = 1.3
            templates[amp]["follow"] = [{"param": 0, "from": "amp_gain", "scale": 0.6, "offset": 0}, {"param": 32, "from": "amp_gain", "scale": 18, "offset": -14}]
    filler = json.loads(json.dumps(presets[-1]))
    filler["meta"]["name"] = "New Preset"
    while len(presets) < 128:
        presets.append(filler)
    raw = json.dumps({"meta": {"name": "RS Tone Host"}, "presets": presets}).encode()
    hls = {"version": 2, "meta": {"device_version": presets[0].get("device_version", 0), "device": NATIVE, "application": "Helix Native",
                                  "modifieddate": int(time.time()), "name": "RS Tone Host", "appversion": presets[0].get("device_version", 0)},
           "encoding": "Base64", "encoded_data": base64.b64encode(zlib.compress(raw)).decode(),
           "compression": {"crc32": zlib.crc32(raw) & 0xffffffff, "decompressed_size": len(raw), "type": "zlib"}, "schema": "L6Setlist"}
    json.dump(hls, open(os.path.join(out, "setlist.hls"), "w"), indent=2)
    by = {k + "_": v for k, v in gear["amps"].items()}
    json.dump({"templates": templates, "by_prefix": by, "fallback": gear["fallback"], "follow": rules(gear),
               "drive_pedals": sorted(k for b in ("drive", "fuzz", "metal") for k in gear["blocks"][b]["gear"])}, open(os.path.join(out, "tones.json"), "w"), indent=1)
    print("%d presets, %d game amps, %d rules" % (len(used), len(gear["amps"]), len(rules(gear))))


if __name__ == "__main__":
    main()
