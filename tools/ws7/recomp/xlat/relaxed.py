"""Functions translated without per-access TSO fences ("relaxed"), and the imports they may call.

Under RC_MO_TSO every guest load is followed by `dmb ishld` and every guest store preceded by
`dmb ishst` (runtime/rc.h). In a sample loop that is a fence per sample and channel, about half the
time of the Wwise effect kernels below. A function listed here is emitted with plain accesses
(RC_LDR/RC_STR, runtime/rc_int.h) and fences at its boundary instead:

  * RC_RELAX_ENTER (dmb ishst) at entry and after every call it makes: stores before the function
    (or before the call returned) are ordered before its first store;
  * RC_RELAX_EXIT (dmb ishld) at every return, tail call and before every call: its loads are
    ordered before everything that follows. Its stores are ordered before later stores by those
    stores' own fences (every fenced store has one in front, every relaxed function at entry).

Seen from other threads the function then behaves like one block: its accesses keep x86 order
against everything before and after it, only their order among themselves is weakened. That is
safe for code that does not synchronise through plain memory inside its own body, which is what
each entry here was checked for by hand (from the x86 code): a DSP kernel over the effect's own
state and the audio buffers of one render pass, run on the audio thread, with no flag or index it
publishes to another thread, no spin-wait on memory (a plain load may be hoisted out of a loop),
and no lock-prefixed or xchg instruction (Translator.relax_ok also rejects those, and any call
other than to another relaxed function or one of PURE_IMPORTS, falling back to the fenced form
with a warning).

RC_RELAXED=0 in the environment turns the list off (A/B tests of a suspected memory-order problem).
Addresses are function starts of the original image; the names are what the code was identified as.
"""

RELAXED = {
    # Wwise RoomVerb (AkRoomVerb plug-in range 0x100a9e2f4-0x100aa58f9)
    0x100aa4a60: "RoomVerb: 4-line feedback delay network, low-pass in the loop, Householder mix",
    0x100aa4e70: "RoomVerb: the same network with separate left/right outputs",
    0x100aa4208: "RoomVerb: early-reflection multi-tap delay (two tap sets, 4 frames per step)",
    0x100aa3914: "RoomVerb: Schroeder allpass diffusor on a delay line",
    # Wwise Parametric EQ (0x100a99c08-0x100a9a828)
    0x100a9a508: "ParametricEQ: biquad (direct form I) over every channel",
    # Wwise Peak Limiter (0x100a9a828-0x100a9c164)
    0x100a9ae44: "PeakLimiter: look-ahead peak follower and gain over every channel",
    # Wwise Expander (0x100a6a0bc-0x100a6ab76)
    0x100a6a27c: "Expander: envelope follower and gain computer (calls expf)",
    # Wwise Meter (0x100a7f720-) and the AkAudioBuffer helper it calls
    0x100a7f880: "Meter: peak/RMS of the buffer into the meter's own history (calls log10)",
    0x100a6c5dc: "AkAudioBuffer::ZeroPadToMaxFrames (bzero of each channel's tail)",
    # Wwise Vorbis decoder (0x100b292d0-): codebook decode over the packet being decoded
    0x100b2e570: "Vorbis: decode one codebook entry and dequantise it into a vector",
    0x100b2e350: "Vorbis: Huffman decode of one codebook entry",
    0x100b2f480: "Vorbis: bit reader, look at the next n bits",
    0x100b2f510: "Vorbis: bit reader, advance by n bits",
    # game: the note detector's analysis window
    0x10010da10: "analysis window: buffer *= raised cosine, 4 samples per step (calls vcosf)",
}

# Imports a relaxed function may call without fences around the call: native functions that
# touch no guest memory beyond their arguments' buffers and never call back into guest code.
PURE_IMPORTS = {
    "_expf", "_exp", "_log10", "_log10f", "_logf", "_log", "_powf", "_pow", "_sqrtf", "_sqrt",
    "_cosf", "_cos", "_sinf", "_sin", "_floorf", "_floor", "_ceilf", "_ceil", "_fabsf", "_fabs",
    "_tanf", "_tan", "_atan2f", "_atan2", "___exp10", "___exp10f", "_vcosf", "_vsinf", "___bzero",
    "_memset", "_memcpy", "_memmove",
}
