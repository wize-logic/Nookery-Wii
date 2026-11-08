/* The SDK's paired-single matrix, vector and quaternion functions are
 * MWCC assembly: on the Wii they are the C versions (Aurora's C library,
 * extern/aurora/lib/dolphin/mtx, built by cmake/Wii.cmake). Generated from
 * the declarations in extern/aurora/include/dolphin/mtx*.h. */

#include <dolphin/mtx.h>

void PSMTXIdentity(Mtx m)
{
    C_MTXIdentity(m);
}

void PSMTXCopy(const Mtx src, Mtx dst)
{
    C_MTXCopy(src, dst);
}

void PSMTXConcat(const Mtx a, const Mtx b, Mtx ab)
{
    C_MTXConcat(a, b, ab);
}

void PSMTXConcatArray(const Mtx a, const Mtx* srcBase, Mtx* dstBase, u32 count)
{
    C_MTXConcatArray(a, srcBase, dstBase, count);
}

void PSMTXTranspose(const Mtx src, Mtx xPose)
{
    C_MTXTranspose(src, xPose);
}

u32 PSMTXInverse(const Mtx src, Mtx inv)
{
    return C_MTXInverse(src, inv);
}

u32 PSMTXInvXpose(const Mtx src, Mtx invX)
{
    return C_MTXInvXpose(src, invX);
}

void PSMTXMultVec(const Mtx m, const Vec* src, Vec* dst)
{
    C_MTXMultVec(m, src, dst);
}

void PSMTXMultVecArray(const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count)
{
    C_MTXMultVecArray(m, srcBase, dstBase, count);
}

void PSMTXMultVecSR(const Mtx m, const Vec* src, Vec* dst)
{
    C_MTXMultVecSR(m, src, dst);
}

void PSMTXMultVecArraySR(const Mtx m, const Vec* srcBase, Vec* dstBase, u32 count)
{
    C_MTXMultVecArraySR(m, srcBase, dstBase, count);
}

void PSMTXROMultVecArray(const ROMtx m, const Vec *srcBase, Vec *dstBase, u32 count)
{
    C_MTXROMultVecArray(m, srcBase, dstBase, count);
}

void PSMTXQuat(Mtx m, const Quaternion* q)
{
    C_MTXQuat(m, q);
}

void PSMTXReflect(Mtx m, const Vec* p, const Vec* n)
{
    C_MTXReflect(m, p, n);
}

void PSMTXTrans(Mtx m, f32 xT, f32 yT, f32 zT)
{
    C_MTXTrans(m, xT, yT, zT);
}

void PSMTXTransApply(const Mtx src, Mtx dst, f32 xT, f32 yT, f32 zT)
{
    C_MTXTransApply(src, dst, xT, yT, zT);
}

void PSMTXScale(Mtx m, f32 xS, f32 yS, f32 zS)
{
    C_MTXScale(m, xS, yS, zS);
}

void PSMTXScaleApply(const Mtx src, Mtx dst, f32 xS, f32 yS, f32 zS)
{
    C_MTXScaleApply(src, dst, xS, yS, zS);
}

void PSMTXRotRad(Mtx m, char axis, f32 rad)
{
    C_MTXRotRad(m, axis, rad);
}

void PSMTXRotTrig(Mtx m, char axis, f32 sinA, f32 cosA)
{
    C_MTXRotTrig(m, axis, sinA, cosA);
}

void PSMTXRotAxisRad(Mtx m, const Vec* axis, f32 rad)
{
    C_MTXRotAxisRad(m, axis, rad);
}

void PSMTXReorder(const Mtx src, ROMtx dest)
{
    C_MTXReorder(src, dest);
}






void PSVECAdd(const Vec* a, const Vec* b, Vec* ab)
{
    C_VECAdd(a, b, ab);
}

void PSVECSubtract(const Vec* a, const Vec* b, Vec* a_b)
{
    C_VECSubtract(a, b, a_b);
}

void PSVECScale(const Vec* src, Vec* dst, f32 scale)
{
    C_VECScale(src, dst, scale);
}

f32 PSVECDotProduct(const Vec* a, const Vec* b)
{
    return C_VECDotProduct(a, b);
}

void PSVECCrossProduct(const Vec* a, const Vec* b, Vec* axb)
{
    C_VECCrossProduct(a, b, axb);
}

void PSQUATAdd(const Quaternion* p, const Quaternion* q, Quaternion* r)
{
    C_QUATAdd(p, q, r);
}

void PSQUATSubtract(const Quaternion* p, const Quaternion* q, Quaternion* r)
{
    C_QUATSubtract(p, q, r);
}

void PSQUATMultiply(const Quaternion* p, const Quaternion* q, Quaternion* pq)
{
    C_QUATMultiply(p, q, pq);
}

void PSQUATDivide(const Quaternion* p, const Quaternion* q, Quaternion* r)
{
    C_QUATDivide(p, q, r);
}

void PSQUATScale(const Quaternion* q, Quaternion* r, f32 scale)
{
    C_QUATScale(q, r, scale);
}

f32 PSQUATDotProduct(const Quaternion* p, const Quaternion* q)
{
    return C_QUATDotProduct(p, q);
}

void PSQUATNormalize(const Quaternion* src, Quaternion* unit)
{
    C_QUATNormalize(src, unit);
}

void PSQUATInverse(const Quaternion* src, Quaternion* inv)
{
    C_QUATInverse(src, inv);
}
