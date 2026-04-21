"""python/spu94/reverb.py

Phase 6 Plan 2 Task 2: the SPU94 class (D-01 secondary surface).

Ergonomic wrapper over ``api.py`` — owns the state handle so the caller
doesn't pass it explicitly. Every method forwards 1:1 to ``api.py``.
No hidden state, no second implementation — just a polite panel on top
of the raw-panel primary surface.

Usage
-----
    import numpy as np
    import spu94

    n = 44100
    L_in  = np.zeros(n, dtype=np.int16)
    R_in  = np.zeros(n, dtype=np.int16)
    L_out = np.zeros(n, dtype=np.int16)
    R_out = np.zeros(n, dtype=np.int16)

    with spu94.SPU94() as rev:
        rev.load_preset("hall")
        rev.process(L_in, R_in, L_out, R_out)
        rev.flush(L_out, R_out)  # drain the tail
"""
from typing import Optional, Tuple

import numpy as np

from . import api


class SPU94:
    """Handle-owning wrapper over the SPU-94 reverb engine.

    Attributes
    ----------
    state : ctypes.c_void_p
        The underlying opaque state handle. Exposed for callers who want
        to mix class-based and raw-panel styles — e.g. pass ``rev.state``
        to ``spu94.process(rev.state, ...)`` directly.
    work_buf_size : int
        The bytes of reverb work buffer allocated at construction.

    Lifecycle
    ---------
    ``SPU94()`` allocates state + work buffer immediately; ``destroy()``
    zeros them and sets ``self._state = None``. After destruction, any
    method except a second ``destroy()`` raises
    ``RuntimeError("SPU94 instance has been destroyed")``. The
    double-destroy guard is idempotent — calling destroy twice is
    legal and harmless. The class is a context manager: ``with SPU94()
    as rev:`` destroys on exit even if the body raises.
    """

    def __init__(self, work_buf_size: int = 8192):
        self._state = api.init(work_buf_size=work_buf_size)
        self._work_buf_size = work_buf_size

    # --- handle / lifecycle --------------------------------------------

    @property
    def state(self):
        """Return the opaque state handle. Raises RuntimeError if the
        instance has already been destroyed."""
        if self._state is None:
            raise RuntimeError("SPU94 instance has been destroyed")
        return self._state

    @property
    def work_buf_size(self) -> int:
        return self._work_buf_size

    def reset(self) -> None:
        """Restore state to post-init invariants."""
        api.reset(self.state)

    def destroy(self) -> None:
        """Zero state bytes and invalidate this instance. Idempotent —
        calling twice is legal; calling any other method afterward
        raises RuntimeError("SPU94 instance has been destroyed")."""
        if self._state is not None:
            api.destroy(self._state)
            self._state = None

    def __enter__(self) -> "SPU94":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.destroy()

    def __del__(self) -> None:
        # Defensive: if the caller forgot to destroy, do it on GC.
        # Exceptions in __del__ are squashed — logging a traceback at
        # interpreter shutdown is worse than the missed zero.
        try:
            self.destroy()
        except Exception:
            pass

    def __repr__(self) -> str:
        status = "destroyed" if self._state is None else "live"
        return f"<spu94.SPU94 work_buf={self._work_buf_size}B state={status}>"

    # --- audio processing ----------------------------------------------

    def tick(self) -> None:
        """Advance one 22.05 kHz tick (commits pending register writes)."""
        api.tick(self.state)

    def process(self,
                L_in: Optional[np.ndarray],
                R_in: Optional[np.ndarray],
                L_out: np.ndarray,
                R_out: np.ndarray) -> None:
        """Block-process stereo int16 @ 44.1 kHz.

        Forwards to ``api.process(self.state, ...)``. See that function
        for the strict numpy contract (int16 / C-contiguous / equal
        length / L_in and R_in may be None for silence)."""
        api.process(self.state, L_in, R_in, L_out, R_out)

    def flush(self, L_out: np.ndarray, R_out: np.ndarray) -> None:
        """Drain the decaying reverb tail into L_out / R_out. Same numpy
        contract as ``process`` — int16, C-contig, equal length."""
        api.flush(self.state, L_out, R_out)

    # --- preset + registers --------------------------------------------

    def load_preset(self, preset) -> int:
        """Load one of the 10 factory presets. Accepts Preset enum,
        case-insensitive string name, or int id. Returns
        ``SPU94_OK`` / ``SPU94_UNKNOWN_REG``."""
        return api.load_preset(self.state, preset)

    def _reg_type(self, reg) -> int:
        """Look up a register's signedness family. Returns 0 for i16,
        1 for u16. Accepts Register enum, name string, or int."""
        from ._binding import _lib
        from . import Register
        if isinstance(reg, Register):
            reg_int = int(reg)
        elif isinstance(reg, bool):
            # bool subclasses int — reject explicitly to match api._coerce_reg.
            raise TypeError("register key must be Register, int, or str; got bool")
        elif isinstance(reg, int):
            reg_int = reg
        elif isinstance(reg, str):
            reg_int = int(Register[reg])
        else:
            raise TypeError(
                f"register key must be Register, int, or str; got {type(reg).__name__}"
            )
        return _lib.spu94_reg_type(reg_int), reg_int

    def set_reg(self, reg, value: int) -> int:
        """Dispatch to ``set_reg_i16`` or ``set_reg_u16`` based on the
        register's signedness family. Ergonomic single-entry point —
        matches the C-side facade wrappers in spirit (one call per reg
        name regardless of type)."""
        reg_type, reg_int = self._reg_type(reg)
        if reg_type == 0:  # SPU94_REG_TYPE_I16
            return api.set_reg_i16(self.state, reg_int, value)
        return api.set_reg_u16(self.state, reg_int, value)

    def get_reg(self, reg):
        """Dispatch to ``get_reg_i16`` or ``get_reg_u16`` by signedness.
        Returns the signed or unsigned reading as Python int."""
        reg_type, reg_int = self._reg_type(reg)
        if reg_type == 0:
            return api.get_reg_i16(self.state, reg_int)
        return api.get_reg_u16(self.state, reg_int)

    def get_reg_pending(self, reg):
        """Read the staged (not-yet-latched) register value — see
        ``api.get_reg_*_pending`` for the D-06 pending-write contract."""
        reg_type, reg_int = self._reg_type(reg)
        if reg_type == 0:
            return api.get_reg_i16_pending(self.state, reg_int)
        return api.get_reg_u16_pending(self.state, reg_int)

    def snapshot(self) -> Tuple[int, ...]:
        """Return a 35-tuple of the current active register values,
        indexed by ``Register``."""
        return api.snapshot_registers(self.state)

    @property
    def buffer_address(self) -> int:
        """Current work-buffer write cursor (0..0x7FFFE)."""
        return api.get_buffer_address(self.state)

    @property
    def latency_samples(self) -> int:
        """Fixed FIR-chain round-trip group delay in samples (58)."""
        return api.get_latency_samples()
