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
The functions are listed by name in tools/targets/names/relaxed.list, with what the code was identified as;
their addresses (function starts of the original image) are in the target's symbols.map.
"""
import os
import sys

def _relaxed():
    """{address: description} for the target being built ($RC_TARGET, default mac-x64), from the names in
    tools/targets/names/relaxed.list and the target's symbols.map."""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "targets"))
    import targets
    t = targets.Target(os.environ.get("RC_TARGET") or targets.DEFAULT_TARGET)
    out = {}
    path = os.path.join(targets.HERE, "names", "relaxed.list")
    with open(path) as f:
        for ln in f:
            s = ln.split("#", 1)
            name = s[0].strip()
            if name and t.addr(name) is not None:
                out[t.addr(name)] = s[1].strip() if len(s) > 1 else name
    return out


RELAXED = _relaxed()

# Imports a relaxed function may call without fences around the call: native functions that
# touch no guest memory beyond their arguments' buffers and never call back into guest code.
PURE_IMPORTS = {
    "_expf", "_exp", "_log10", "_log10f", "_logf", "_log", "_powf", "_pow", "_sqrtf", "_sqrt",
    "_cosf", "_cos", "_sinf", "_sin", "_floorf", "_floor", "_ceilf", "_ceil", "_fabsf", "_fabs",
    "_tanf", "_tan", "_atan2f", "_atan2", "___exp10", "___exp10f", "_vcosf", "_vsinf", "___bzero",
    "_memset", "_memcpy", "_memmove",
}
