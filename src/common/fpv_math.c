/* Vector and pose helpers for the camera's poses (fpv.h has the layout). No engine calls; cost: a few flops each. */
#include <math.h>
#include <string.h>
#include "fpv.h"

FpvVec3 fpv_v3(float x, float y, float z)
{
    FpvVec3 v = {x, y, z};
    return v;
}

FpvVec3 fpv_add(FpvVec3 a, FpvVec3 b) { return fpv_v3(a.x + b.x, a.y + b.y, a.z + b.z); }
FpvVec3 fpv_sub(FpvVec3 a, FpvVec3 b) { return fpv_v3(a.x - b.x, a.y - b.y, a.z - b.z); }
FpvVec3 fpv_scale(FpvVec3 a, float s) { return fpv_v3(a.x * s, a.y * s, a.z * s); }
float fpv_dot(FpvVec3 a, FpvVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

FpvVec3 fpv_cross(FpvVec3 a, FpvVec3 b)
{
    return fpv_v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

int fpv_normalize(FpvVec3* a)
{
    float length = sqrtf(fpv_dot(*a, *a));
    if (!(length > 1e-6f)) return 0;
    *a = fpv_scale(*a, 1.0f / length);
    return 1;
}

FpvVec3 fpv_pose_right(const float* pose) { return fpv_v3(pose[0], pose[1], pose[2]); }
FpvVec3 fpv_pose_forward(const float* pose) { return fpv_v3(pose[3], pose[4], pose[5]); }
FpvVec3 fpv_pose_up(const float* pose) { return fpv_v3(pose[6], pose[7], pose[8]); }
FpvVec3 fpv_pose_position(const float* pose) { return fpv_v3(pose[9], pose[10], pose[11]); }

static void Put(float* at, FpvVec3 v)
{
    at[0] = v.x;
    at[1] = v.y;
    at[2] = v.z;
}

int fpv_pose_look(const float* keep, FpvVec3 position, FpvVec3 forward, FpvVec3 up, float* out)
{
    memcpy(out, keep, 16 * sizeof(float));
    if (!fpv_normalize(&forward)) return 0;
    FpvVec3 right = fpv_cross(forward, up);
    if (!fpv_normalize(&right)) return 0;
    up = fpv_cross(right, forward);
    Put(out, right);
    Put(out + 3, forward);
    Put(out + 6, up);
    Put(out + 9, position);
    return 1;
}

FpvVec3 fpv_pose_point(const float* pose, FpvVec3 local)
{
    FpvVec3 p = fpv_pose_position(pose);
    p = fpv_add(p, fpv_scale(fpv_pose_right(pose), local.x));
    p = fpv_add(p, fpv_scale(fpv_pose_forward(pose), local.y));
    return fpv_add(p, fpv_scale(fpv_pose_up(pose), local.z));
}
