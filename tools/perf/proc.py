"""A process' CPU time, memory and threads from libproc, without a sampler in the way: what `ps` and `footprint`
print, read directly (no root needed for one's own processes). CPU times are converted to seconds here."""
import ctypes
import ctypes.util
import subprocess

_libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
_libproc = ctypes.CDLL(ctypes.util.find_library("proc"), use_errno=True)


class _Timebase(ctypes.Structure):
    _fields_ = [("numer", ctypes.c_uint32), ("denom", ctypes.c_uint32)]


_tb = _Timebase()
_libc.mach_timebase_info(ctypes.byref(_tb))
_TICK = _tb.numer / _tb.denom / 1e9  # seconds per unit of the kernel's CPU times


class _RusageV4(ctypes.Structure):  # <sys/resource.h> rusage_info_v4
    _fields_ = [("ri_uuid", ctypes.c_uint8 * 16)] + [(n, ctypes.c_uint64) for n in (
        "ri_user_time", "ri_system_time", "ri_pkg_idle_wkups", "ri_interrupt_wkups", "ri_pageins",
        "ri_wired_size", "ri_resident_size", "ri_phys_footprint", "ri_proc_start_abstime",
        "ri_proc_exit_abstime", "ri_child_user_time", "ri_child_system_time", "ri_child_pkg_idle_wkups",
        "ri_child_interrupt_wkups", "ri_child_pageins", "ri_child_elapsed_abstime", "ri_diskio_bytesread",
        "ri_diskio_byteswritten", "ri_cpu_time_qos_default", "ri_cpu_time_qos_maintenance",
        "ri_cpu_time_qos_background", "ri_cpu_time_qos_utility", "ri_cpu_time_qos_legacy",
        "ri_cpu_time_qos_user_initiated", "ri_cpu_time_qos_user_interactive", "ri_billed_system_time",
        "ri_serviced_system_time", "ri_logical_writes", "ri_lifetime_max_phys_footprint", "ri_instructions",
        "ri_cycles", "ri_billed_energy", "ri_serviced_energy", "ri_interval_max_phys_footprint",
        "ri_runnable_time")]


class _ThreadInfo(ctypes.Structure):  # <sys/proc_info.h> proc_threadinfo
    _fields_ = [("pth_user_time", ctypes.c_uint64), ("pth_system_time", ctypes.c_uint64),
                ("pth_cpu_usage", ctypes.c_int32), ("pth_policy", ctypes.c_int32),
                ("pth_run_state", ctypes.c_int32), ("pth_flags", ctypes.c_int32),
                ("pth_sleep_time", ctypes.c_int32), ("pth_curpri", ctypes.c_int32),
                ("pth_priority", ctypes.c_int32), ("pth_maxpriority", ctypes.c_int32),
                ("pth_name", ctypes.c_char * 64)]


_PROC_PIDTHREADINFO, _PROC_PIDLISTTHREADS, _RUSAGE_INFO_V4 = 5, 6, 4


def usage(pid):
    """{"cpu": seconds of CPU so far, "footprint", "rss": bytes, "instructions", "cycles"} or None when the
    process is gone."""
    r = _RusageV4()
    if _libproc.proc_pid_rusage(pid, _RUSAGE_INFO_V4, ctypes.byref(r)) != 0:
        return None
    return {"cpu": (r.ri_user_time + r.ri_system_time) * _TICK, "footprint": r.ri_phys_footprint,
            "rss": r.ri_resident_size, "instructions": r.ri_instructions,
            "cycles": r.ri_cycles}


def threads(pid):
    """{thread handle: (name, seconds of CPU so far)}. A thread without a name has ""."""
    buf = (ctypes.c_uint64 * 1024)()
    n = _libproc.proc_pidinfo(pid, _PROC_PIDLISTTHREADS, ctypes.c_uint64(0), buf, ctypes.sizeof(buf))
    out = {}
    for h in buf[:max(n, 0) // 8]:
        t = _ThreadInfo()
        if _libproc.proc_pidinfo(pid, _PROC_PIDTHREADINFO, ctypes.c_uint64(h), ctypes.byref(t),
                                 ctypes.sizeof(t)) == ctypes.sizeof(t):
            out[h] = (t.pth_name.decode("utf-8", "replace"), (t.pth_user_time + t.pth_system_time) / 1e9)
    return out


def thread_cpu(before, after, seconds):
    """[(name, % of a core)] of the threads in both, busiest first; names that repeat are numbered."""
    rows, seen = [], {}
    for h, (name, cpu) in after.items():
        if h in before:
            rows.append((name or "(no name)", round((cpu - before[h][1]) / seconds * 100, 2)))
    rows.sort(key=lambda r: -r[1])
    out = []
    for name, pct in rows:
        seen[name] = seen.get(name, 0) + 1
        out.append((name if seen[name] == 1 else "%s #%d" % (name, seen[name]), pct))
    return out


def pids_named(name):
    """Processes whose executable is called name."""
    r = subprocess.run(["pgrep", "-x", name], capture_output=True, text=True)
    return [int(p) for p in r.stdout.split()]


def gpu_percent():
    """The GPU's "Device Utilization %" (the whole system's, so nothing else should be drawing), or None."""
    r = subprocess.run(["ioreg", "-r", "-d", "1", "-w", "0", "-c", "IOAccelerator"], capture_output=True, text=True)
    i = r.stdout.find('"Device Utilization %"=')
    if i < 0:
        return None
    digits = ""
    for c in r.stdout[i + len('"Device Utilization %"='):]:
        if not c.isdigit():
            break
        digits += c
    return int(digits) if digits else None


def frontmost_pid():
    """The pid of the app in front, or None."""
    r = subprocess.run("lsappinfo info -only pid $(lsappinfo front)", shell=True, capture_output=True, text=True)
    digits = "".join(c for c in r.stdout if c.isdigit())
    return int(digits) if digits else None
