// openbv: force-included when compiling the engine (src/engine, the dk and dko DLLs). See openbv_link.h.
#ifndef OPENBV_ENGINE_NAMES_H
#define OPENBV_ENGINE_NAMES_H
#define CString      dkString
#define CVector2i    dkVector2i
#define CVector2f    dkVector2f
#define CVector3i    dkVector3i
#define CVector3f    dkVector3f
#define CVector4f    dkVector4f
#define CMatrix3x3f  dkMatrix3x3f
#define dk_sqrtf     dkEngine_sqrtf
#include "openbv_link.h"
#endif
