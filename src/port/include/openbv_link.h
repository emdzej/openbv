// openbv: the original engine was a set of DLLs (dkc, dkgl, dkp, ... and dko), each with its own
// copy of CString, CVector* and CMatrix3x3f, and the game had a third, slightly different copy
// (EPSILON, toInt, ...). Linked into one wasm module those classes would collide, so the engine's
// copies are renamed when the engine is compiled (engine_names.h). The engine functions that pass
// them across the old DLL boundary get one fixed link name on both sides with OPENBV_LINK; the
// layouts are the same, as they had to be between the DLLs.
#ifndef OPENBV_LINK_H
#define OPENBV_LINK_H
#ifdef OPENBV_GASM
#define OPENBV_LINK(name) __asm__("openbv_" #name)
#else
#define OPENBV_LINK(name)
#endif
#endif
