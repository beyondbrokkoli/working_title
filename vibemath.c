#include <immintrin.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

// ========================================================
// CROSS-PLATFORM FFI EXPORT MACRO
// ========================================================
#ifdef _WIN32
    // Windows DLL export
    #define EXPORT __declspec(dllexport)
#else
    // Linux/macOS Shared Object export
    #define EXPORT __attribute__((visibility("default")))
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ========================================================
// CROSS-PLATFORM THREADING BRIDGE (Mutex & CondVars)
// ========================================================
#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    typedef HANDLE vmath_thread_t;
    typedef CRITICAL_SECTION vmath_mutex_t;
    typedef CONDITION_VARIABLE vmath_cond_t;
    #define THREAD_FUNC DWORD WINAPI
    #define THREAD_RETURN_VAL 0
    static vmath_thread_t vmath_thread_start(DWORD (WINAPI *func)(LPVOID), void* arg) { return CreateThread(NULL, 0, func, arg, 0, NULL); }
    static void vmath_thread_join(vmath_thread_t thread) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); }
    static void vmath_mutex_init(vmath_mutex_t* m) { InitializeCriticalSection(m); }
    static void vmath_mutex_lock(vmath_mutex_t* m) { EnterCriticalSection(m); }
    static void vmath_mutex_unlock(vmath_mutex_t* m) { LeaveCriticalSection(m); }
    static void vmath_mutex_destroy(vmath_mutex_t* m) { DeleteCriticalSection(m); }
    static void vmath_cond_init(vmath_cond_t* cv) { InitializeConditionVariable(cv); }
    static void vmath_cond_wait(vmath_cond_t* cv, vmath_mutex_t* m) { SleepConditionVariableCS(cv, m, INFINITE); }
    static void vmath_cond_broadcast(vmath_cond_t* cv) { WakeAllConditionVariable(cv); }
    static void vmath_cond_destroy(vmath_cond_t* cv) { }
#else
    #include <pthread.h>
    typedef pthread_t vmath_thread_t;
    typedef pthread_mutex_t vmath_mutex_t;
    typedef pthread_cond_t vmath_cond_t;
    #define THREAD_FUNC void*
    #define THREAD_RETURN_VAL NULL
    static vmath_thread_t vmath_thread_start(void* (*func)(void*), void* arg) { pthread_t thread; pthread_create(&thread, NULL, func, arg); return thread; }
    static void vmath_thread_join(vmath_thread_t thread) { pthread_join(thread, NULL); }
    static void vmath_mutex_init(vmath_mutex_t* m) { pthread_mutex_init(m, NULL); }
    static void vmath_mutex_lock(vmath_mutex_t* m) { pthread_mutex_lock(m); }
    static void vmath_mutex_unlock(vmath_mutex_t* m) { pthread_mutex_unlock(m); }
    static void vmath_mutex_destroy(vmath_mutex_t* m) { pthread_mutex_destroy(m); }
    static void vmath_cond_init(vmath_cond_t* cv) { pthread_cond_init(cv, NULL); }
    static void vmath_cond_wait(vmath_cond_t* cv, vmath_mutex_t* m) { pthread_cond_wait(cv, m); }
    static void vmath_cond_broadcast(vmath_cond_t* cv) { pthread_cond_broadcast(cv); }
    static void vmath_cond_destroy(vmath_cond_t* cv) { pthread_cond_destroy(cv); }
#endif

// ========================================================
// THE OS-SLEEP THREAD POOL STATE (Physics Only Now!)
// ========================================================
vmath_mutex_t g_phys_mutex;
vmath_cond_t  g_phys_cv_start;
vmath_cond_t  g_phys_cv_done;
int g_phys_sig  = 0;
int g_phys_done = 1;

// ========================================================================
// FAST AVX2 TRIGONOMETRY (Minimax Approximations)
// ========================================================================
static inline __m256 wrap_pi_avx(__m256 x) {
    __m256 inv_two_pi = _mm256_set1_ps(1.0f / (2.0f * M_PI));
    __m256 two_pi = _mm256_set1_ps(2.0f * M_PI);
    __m256 q = _mm256_round_ps(_mm256_mul_ps(x, inv_two_pi), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    return _mm256_fnmadd_ps(q, two_pi, x);
}
static inline __m256 fast_sin_avx(__m256 x) {
    x = wrap_pi_avx(x); 
    __m256 B = _mm256_set1_ps(4.0f / M_PI), C = _mm256_set1_ps(-4.0f / (M_PI * M_PI));
    __m256 x_abs = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), x);
    __m256 y = _mm256_fmadd_ps(_mm256_mul_ps(C, x_abs), x, _mm256_mul_ps(B, x));
    __m256 P = _mm256_set1_ps(0.225f);
    __m256 y_abs = _mm256_andnot_ps(_mm256_set1_ps(-0.0f), y);
    return _mm256_fmadd_ps(_mm256_fmadd_ps(y_abs, y, _mm256_sub_ps(_mm256_setzero_ps(), y)), P, y);
}
static inline __m256 fast_cos_avx(__m256 x) { return fast_sin_avx(_mm256_add_ps(x, _mm256_set1_ps(M_PI / 2.0f))); }
static inline __m256 fast_trig_noise_avx(__m256 nx, __m256 ny, __m256 nz, __m256 time) {
    __m256 v1 = fast_sin_avx(_mm256_add_ps(_mm256_mul_ps(nx, _mm256_set1_ps(3.1f)), time));
    __m256 v2 = fast_cos_avx(_mm256_add_ps(_mm256_mul_ps(ny, _mm256_set1_ps(2.8f)), time));
    __m256 v3 = fast_sin_avx(_mm256_add_ps(_mm256_mul_ps(nz, _mm256_set1_ps(3.4f)), time));
    __m256 out = _mm256_add_ps(v1, _mm256_add_ps(v2, v3));
    __m256 time2 = _mm256_mul_ps(time, _mm256_set1_ps(1.8f));
    __m256 v4 = fast_sin_avx(_mm256_add_ps(_mm256_mul_ps(nx, _mm256_set1_ps(7.2f)), time2));
    __m256 v5 = fast_cos_avx(_mm256_add_ps(_mm256_mul_ps(ny, _mm256_set1_ps(6.5f)), time2));
    __m256 v6 = fast_sin_avx(_mm256_add_ps(_mm256_mul_ps(nz, _mm256_set1_ps(8.1f)), time2));
    __m256 oct2 = _mm256_mul_ps(_mm256_add_ps(v4, _mm256_add_ps(v5, v6)), _mm256_set1_ps(0.35f));
    return _mm256_mul_ps(_mm256_add_ps(out, oct2), _mm256_set1_ps(0.25f));
}

// ========================================================================
// CORE STRUCTS
// ========================================================================
typedef struct {
    float x, y, z;
    float yaw, pitch;
    float fov;
    float fwx, fwy, fwz;
    float rtx, rty, rtz;
    float upx, upy, upz;
} CameraState;

typedef struct {
    float *Obj_X, *Obj_Y, *Obj_Z, *Obj_Radius;
    float *Obj_FWX, *Obj_FWY, *Obj_FWZ;
    float *Obj_RTX, *Obj_RTY, *Obj_RTZ;
    float *Obj_UPX, *Obj_UPY, *Obj_UPZ;
    int *Obj_VertStart, *Obj_VertCount;
    int *Obj_TriStart, *Obj_TriCount;

    float *Vert_LX, *Vert_LY, *Vert_LZ;
    float *Vert_PX, *Vert_PY, *Vert_PZ; bool *Vert_Valid;

    int *Tri_V1, *Tri_V2, *Tri_V3;
    uint32_t *Tri_BakedColor, *Tri_ShadedColor; bool *Tri_Valid;
    float *Tri_MinY, *Tri_MaxY;
    float *Tri_LNX, *Tri_LNY, *Tri_LNZ;

    float *Swarm_PX[2]; float *Swarm_PY[2]; float *Swarm_PZ[2];
    float *Swarm_VX[2]; float *Swarm_VY[2]; float *Swarm_VZ[2];
    int *Swarm_Indices[2];

    float *Swarm_Seed;
    int Swarm_State;
    float Swarm_GravityBlend;
    float Swarm_MetalBlend;
    float Swarm_ParadoxBlend;

    int *Swarm_TempIndices;
    float *Swarm_Distances;
    float *Swarm_TempDistances;
} RenderMemory;

typedef struct {
    int command_count; int* queue; RenderMemory* mem;
    float time; float dt; int read_idx; int write_idx;
} PhysicsThreadPayload;

PhysicsThreadPayload g_physics_payload;
vmath_thread_t g_physics_thread;

// GLOBALS
int       g_canvas_w;
int       g_canvas_h;
float     g_half_w;
float     g_half_h;
RenderMemory* g_mem ;
CameraState* g_cam;
int* g_queue;

EXPORT void vmath_bind_engine(RenderMemory* mem, CameraState* cam, int* queue) {
    g_mem = mem; g_cam = cam; g_queue = queue;
}

// Notice how we removed the screen pointers! AVX2 no longer cares about rasterizing.
EXPORT void vmath_set_resolution(int w, int h, uint32_t* screen_ptr, float* z_buffer) {
    g_canvas_w = w; g_canvas_h = h; g_half_w = w * 0.5f; g_half_h = h * 0.5f;
}


// ========================================================================
// ALL PHYSICS KERNELS (Bundled into a collapsed region for brevity)
// ========================================================================

EXPORT void vmath_generate_torus(
    int count, 
    float* lx, float* ly, float* lz, 
    float time, float major_R, float minor_r
) {
    for (int i = 0; i < count; i++) {
        // fmodf safely wraps the angles to a standard 0 to 2*PI circle!
        float theta = fmodf((float)i * 0.12345f + time, 2.0f * (float)M_PI);
        float phi   = fmodf((float)i * 0.54321f + (time * 0.5f), 2.0f * (float)M_PI);
        
        float tube = major_R + minor_r * cosf(theta);
        
        lx[i] = tube * cosf(phi);
        ly[i] = tube * sinf(phi);
        lz[i] = minor_r * sinf(theta);
    }
}
EXPORT void vmath_swarm_generate_quads(int count, float* px, float* py, float* pz, float* lx, float* ly, float* lz, float size, CameraState* cam, float HALF_W, float HALF_H, int* indices) {
    // ... [Keep your exact implementation here, it works perfectly] ...
    float dead_x = cam->x - cam->fwx * 1000.0f; float dead_y = cam->y - cam->fwy * 1000.0f; float dead_z = cam->z - cam->fwz * 1000.0f;
    int v_idx = 0;
    for (int i = 0; i < count; i++) {
        int actual_id = indices[i];
        float p_x = px[actual_id]; float p_y = py[actual_id]; float p_z = pz[actual_id];
        float dx = p_x - cam->x; float dy = p_y - cam->y; float dz = p_z - cam->z;
        float cz = dz * cam->fwz + dy * cam->fwy + dx * cam->fwx;
        float depth = fmaxf(0.1f, cz);
        float cx = dz * cam->rtz + dy * cam->rty + dx * cam->rtx;
        float cy = dz * cam->upz + dy * cam->upy + dx * cam->upx;
        float frustum_w = ((HALF_W * depth) / cam->fov) + size; float frustum_h = ((HALF_H * depth) / cam->fov) + size;
        bool is_visible = (cz + size >= 0.1f) && (fabsf(cx) <= frustum_w) && (fabsf(cy) <= frustum_h);
        if (is_visible) {
            lx[v_idx] = p_x;        ly[v_idx] = p_y + size; lz[v_idx] = p_z;        v_idx++;
            lx[v_idx] = p_x - size; ly[v_idx] = p_y - size; lz[v_idx] = p_z + size; v_idx++;
            lx[v_idx] = p_x + size; ly[v_idx] = p_y - size; lz[v_idx] = p_z + size; v_idx++;
            lx[v_idx] = p_x;        ly[v_idx] = p_y - size; lz[v_idx] = p_z - size; v_idx++;
        } else {
            lx[v_idx] = dead_x; ly[v_idx] = dead_y; lz[v_idx] = dead_z; v_idx++;
            lx[v_idx] = dead_x; ly[v_idx] = dead_y; lz[v_idx] = dead_z; v_idx++;
            lx[v_idx] = dead_x; ly[v_idx] = dead_y; lz[v_idx] = dead_z; v_idx++;
            lx[v_idx] = dead_x; ly[v_idx] = dead_y; lz[v_idx] = dead_z; v_idx++;
        }
    }
}

// ... [KEEP ALL YOUR SWARM PHYSICS FUNCTIONS EXACTLY AS THEY WERE] ...
// Boilerplate Spring Physics Macro to keep the shape functions perfectly clean
#define APPLY_SPRING_PHYSICS() \
    __m256 v_px = _mm256_loadu_ps(&px[i]), v_py = _mm256_loadu_ps(&py[i]), v_pz = _mm256_loadu_ps(&pz[i]); \
    __m256 v_vx = _mm256_loadu_ps(&vx[i]), v_vy = _mm256_loadu_ps(&vy[i]), v_vz = _mm256_loadu_ps(&vz[i]); \
    v_vx = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_sub_ps(v_tx, v_px), v_k, v_vx), v_damp); \
    v_vy = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_sub_ps(v_ty, v_py), v_k, v_vy), v_damp); \
    v_vz = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_sub_ps(v_tz, v_pz), v_k, v_vz), v_damp); \
    _mm256_storeu_ps(&px[i], _mm256_fmadd_ps(v_vx, v_dt, v_px)); \
    _mm256_storeu_ps(&py[i], _mm256_fmadd_ps(v_vy, v_dt, v_py)); \
    _mm256_storeu_ps(&pz[i], _mm256_fmadd_ps(v_vz, v_dt, v_pz)); \
    _mm256_storeu_ps(&vx[i], v_vx); _mm256_storeu_ps(&vy[i], v_vy); _mm256_storeu_ps(&vz[i], v_vz);

EXPORT void vmath_swarm_update_velocities(int count, float* px_in, float* py_in, float* pz_in, float* vx_in, float* vy_in, float* vz_in, float* px_out, float* py_out, float* pz_out, float* vx_out, float* vy_out, float* vz_out, float minX, float maxX, float minY, float maxY, float minZ, float maxZ, float dt, float gravity) {
    for (int i=0; i<count; i++) {
        float px = px_in[i], py = py_in[i], pz = pz_in[i]; float vx = vx_in[i], vy = vy_in[i], vz = vz_in[i];
        vy -= (gravity * dt); vx *= 0.995f; vy *= 0.995f; vz *= 0.995f;
        px += vx * dt; py += vy * dt; pz += vz * dt;
        if (px < minX) { px = minX; vx = fabsf(vx) * 0.8f; } else if (px > maxX) { px = maxX; vx = fabsf(vx) * -0.8f; }
        if (py < minY) { py = minY; vy = fabsf(vy) * 0.8f; } else if (py > maxY) { py = maxY; vy = fabsf(vy) * -0.8f; }
        if (pz < minZ) { pz = minZ; vz = fabsf(vz) * 0.8f; } else if (pz > maxZ) { pz = maxZ; vz = fabsf(vz) * -0.8f; }
        px_out[i] = px; py_out[i] = py; pz_out[i] = pz; vx_out[i] = vx; vy_out[i] = vy; vz_out[i] = vz;
    }
}
EXPORT void vmath_swarm_bundle(int count, float* px, float* py, float* pz, float* vx, float* vy, float* vz, float* seed, float cx, float cy, float cz, float time, float dt) {
    __m256 v_cx = _mm256_set1_ps(cx), v_cy = _mm256_set1_ps(cy), v_cz = _mm256_set1_ps(cz);
    __m256 v_r = _mm256_set1_ps(2000.0f + 400.0f * sinf(time * 6.0f));
    __m256 v_golden = _mm256_set1_ps(2.39996323f);
    __m256 v_1 = _mm256_set1_ps(1.0f), v_2 = _mm256_set1_ps(2.0f);
    __m256 v_dt = _mm256_set1_ps(dt), v_k = _mm256_set1_ps(4.0f * dt), v_damp = _mm256_set1_ps(0.92f);

    int i = 0; // <--- EXTRACTED FOR THE SCALAR TAIL!
    for (; i <= count - 8; i += 8) {
        __m256 v_s = _mm256_loadu_ps(&seed[i]);
        __m256 v_i = _mm256_set_ps(i+7, i+6, i+5, i+4, i+3, i+2, i+1, i);

        __m256 v_phi = _mm256_mul_ps(v_i, v_golden);

        // Math Hack: No acos needed! cos(theta) = 1-2s. sin(theta) = 2*sqrt(s*(1-s))
        __m256 v_cos_theta = _mm256_fnmadd_ps(v_2, v_s, v_1);
        __m256 v_sin_theta = _mm256_mul_ps(v_2, _mm256_sqrt_ps(_mm256_mul_ps(v_s, _mm256_sub_ps(v_1, v_s))));

        __m256 v_tx = _mm256_fmadd_ps(v_r, _mm256_mul_ps(v_sin_theta, fast_cos_avx(v_phi)), v_cx);
        __m256 v_ty = _mm256_fmadd_ps(v_r, v_cos_theta, v_cy);
        __m256 v_tz = _mm256_fmadd_ps(v_r, _mm256_mul_ps(v_sin_theta, fast_sin_avx(v_phi)), v_cz);

        APPLY_SPRING_PHYSICS();
    }
    // ========================================================
    // SCALAR TAIL LOOP (For Safety - Mod 8 remainders)
    // ========================================================
    float r = 2000.0f + 400.0f * sinf(time * 6.0f);
    float golden = 2.39996323f;
    float k = 4.0f * dt;
    float damp = 0.92f;

    for (; i < count; i++) {
        float s = seed[i];
        float phi = (float)i * golden;

        // Math Hack: No acos needed!
        float cos_theta = 1.0f - 2.0f * s;
        float sin_theta = 2.0f * sqrtf(s * (1.0f - s));

        float tx = cx + r * sin_theta * cosf(phi);
        float ty = cy + r * cos_theta;
        float tz = cz + r * sin_theta * sinf(phi);

        // SPRING PHYSICS
        float p_x = px[i], p_y = py[i], p_z = pz[i];
        float v_x = vx[i], v_y = vy[i], v_z = vz[i];

        v_x = (v_x + (tx - p_x) * k) * damp;
        v_y = (v_y + (ty - p_y) * k) * damp;
        v_z = (v_z + (tz - p_z) * k) * damp;

        px[i] = p_x + v_x * dt;
        py[i] = p_y + v_y * dt;
        pz[i] = p_z + v_z * dt;

        vx[i] = v_x;
        vy[i] = v_y;
        vz[i] = v_z;
    }
}

EXPORT void vmath_swarm_galaxy(int count, float* px, float* py, float* pz, float* vx, float* vy, float* vz, float* seed, float cx, float cy, float cz, float time, float dt) {
    __m256 v_cx = _mm256_set1_ps(cx), v_cy = _mm256_set1_ps(cy), v_cz = _mm256_set1_ps(cz);
    __m256 v_time_ang = _mm256_set1_ps(time * 1.5f), v_time_z = _mm256_set1_ps(time * 3.0f);
    __m256 v_dt = _mm256_set1_ps(dt), v_k = _mm256_set1_ps(4.0f * dt), v_damp = _mm256_set1_ps(0.92f);

    int i = 0;
    for (; i <= count - 8; i += 8) {
        __m256 v_s = _mm256_loadu_ps(&seed[i]);
        __m256 v_angle = _mm256_fmadd_ps(v_s, _mm256_set1_ps(3.14159f * 30.0f), v_time_ang);
        __m256 v_r = _mm256_fmadd_ps(v_s, _mm256_set1_ps(14000.0f), _mm256_set1_ps(1000.0f));

        __m256 v_tx = _mm256_fmadd_ps(v_r, fast_cos_avx(v_angle), v_cx);
        __m256 v_ty = _mm256_fmadd_ps(_mm256_set1_ps(800.0f), fast_sin_avx(_mm256_fnmadd_ps(v_time_z, _mm256_set1_ps(1.0f), _mm256_mul_ps(v_s, _mm256_set1_ps(40.0f)))), v_cy);
        __m256 v_tz = _mm256_fmadd_ps(v_r, fast_sin_avx(v_angle), v_cz);

        APPLY_SPRING_PHYSICS();
    }
    // ========================================================
    // SCALAR TAIL LOOP (For Safety - Mod 8 remainders)
    // ========================================================
    float k = 4.0f * dt;
    float damp = 0.92f;
    float time_ang = time * 1.5f;
    float time_z = time * 3.0f;

    for (; i < count; i++) {
        float s = seed[i];

        // 1. Calculate Galaxy Arms & Radius
        float angle = s * (3.14159f * 30.0f) + time_ang;
        float r = s * 14000.0f + 1000.0f;

        // 2. Target Positions (with the fnmadd Y-wobble math)
        float tx = cx + r * cosf(angle);
        float ty = cy + 800.0f * sinf((s * 40.0f) - time_z);
        float tz = cz + r * sinf(angle);

        // 3. SPRING PHYSICS
        float p_x = px[i], p_y = py[i], p_z = pz[i];
        float v_x = vx[i], v_y = vy[i], v_z = vz[i];

        v_x = (v_x + (tx - p_x) * k) * damp;
        v_y = (v_y + (ty - p_y) * k) * damp;
        v_z = (v_z + (tz - p_z) * k) * damp;

        px[i] = p_x + v_x * dt;
        py[i] = p_y + v_y * dt;
        pz[i] = p_z + v_z * dt;

        vx[i] = v_x;
        vy[i] = v_y;
        vz[i] = v_z;
    }
}
EXPORT void vmath_swarm_tornado(int count, float* px, float* py, float* pz, float* vx, float* vy, float* vz, float* seed, float cx, float cy, float cz, float time, float dt) {
    __m256 v_cx = _mm256_set1_ps(cx), v_cy = _mm256_set1_ps(cy), v_cz = _mm256_set1_ps(cz);
    __m256 v_time_ang = _mm256_set1_ps(time * 4.0f);
    __m256 v_dt = _mm256_set1_ps(dt), v_k = _mm256_set1_ps(4.0f * dt), v_damp = _mm256_set1_ps(0.92f);

    int i = 0;
    for (; i <= count - 8; i += 8) {
        __m256 v_s = _mm256_loadu_ps(&seed[i]);
        __m256 v_height = _mm256_fnmadd_ps(_mm256_set1_ps(-24000.0f), v_s, _mm256_set1_ps(-12000.0f));
        __m256 v_angle = _mm256_fnmadd_ps(v_time_ang, _mm256_set1_ps(1.0f), _mm256_mul_ps(v_s, _mm256_set1_ps(3.14159f * 30.0f)));
        __m256 v_r = _mm256_fmadd_ps(v_s, _mm256_set1_ps(4000.0f), _mm256_set1_ps(2000.0f));

        __m256 v_tx = _mm256_fmadd_ps(v_r, fast_cos_avx(v_angle), v_cx);
        __m256 v_ty = _mm256_add_ps(v_cy, v_height);
        __m256 v_tz = _mm256_fmadd_ps(v_r, fast_sin_avx(v_angle), v_cz);

        APPLY_SPRING_PHYSICS();
    }
    // ========================================================
    // SCALAR TAIL LOOP (For Safety - Mod 8 remainders)
    // ========================================================
    float k = 4.0f * dt;
    float damp = 0.92f;
    float time_ang = time * 4.0f;

    for (; i < count; i++) {
        float s = seed[i];

        // 1. Calculate Tornado Structure
        float height = 24000.0f * s - 12000.0f;
        float angle = (s * 3.14159f * 30.0f) - time_ang;
        float r = s * 4000.0f + 2000.0f;

        // 2. Target Positions
        float tx = cx + r * cosf(angle);
        float ty = cy + height;
        float tz = cz + r * sinf(angle);

        // 3. SPRING PHYSICS
        float p_x = px[i], p_y = py[i], p_z = pz[i];
        float v_x = vx[i], v_y = vy[i], v_z = vz[i];

        v_x = (v_x + (tx - p_x) * k) * damp;
        v_y = (v_y + (ty - p_y) * k) * damp;
        v_z = (v_z + (tz - p_z) * k) * damp;

        px[i] = p_x + v_x * dt;
        py[i] = p_y + v_y * dt;
        pz[i] = p_z + v_z * dt;

        vx[i] = v_x;
        vy[i] = v_y;
        vz[i] = v_z;
    }
}
EXPORT void vmath_swarm_gyroscope(int count, float* px, float* py, float* pz, float* vx, float* vy, float* vz, float* seed, float cx, float cy, float cz, float time, float dt) {
    __m256 v_cx = _mm256_set1_ps(cx), v_cy = _mm256_set1_ps(cy), v_cz = _mm256_set1_ps(cz);
    __m256 v_r = _mm256_set1_ps(7000.0f);
    __m256 v_time_ang = _mm256_set1_ps(time * 2.5f);
    __m256 v_dt = _mm256_set1_ps(dt), v_k = _mm256_set1_ps(4.0f * dt), v_damp = _mm256_set1_ps(0.92f);

    int i = 0;
    for (; i <= count - 8; i += 8) {
        __m256 v_s = _mm256_loadu_ps(&seed[i]);
        __m256 v_angle = _mm256_fmadd_ps(v_s, _mm256_set1_ps(3.14159f * 2.0f), v_time_ang);

        __m256 v_cos = fast_cos_avx(v_angle);
        __m256 v_sin = fast_sin_avx(v_angle);

        // Calculate all 3 ring positions simultaneously!
        __m256 r0_x = _mm256_fmadd_ps(v_r, v_cos, v_cx), r0_y = _mm256_fmadd_ps(v_r, v_sin, v_cy), r0_z = v_cz;
        __m256 r1_x = r0_x, r1_y = v_cy, r1_z = _mm256_fmadd_ps(v_r, v_sin, v_cz);
        __m256 r2_x = v_cx, r2_y = _mm256_fmadd_ps(v_r, v_cos, v_cy), r2_z = r1_z;

        // Masking logic based on (i % 3)
        int rings[8] = { (i)%3, (i+1)%3, (i+2)%3, (i+3)%3, (i+4)%3, (i+5)%3, (i+6)%3, (i+7)%3 };
        __m256i v_ring = _mm256_loadu_si256((__m256i*)rings);

        __m256 m0 = _mm256_castsi256_ps(_mm256_cmpeq_epi32(v_ring, _mm256_setzero_si256()));
        __m256 m1 = _mm256_castsi256_ps(_mm256_cmpeq_epi32(v_ring, _mm256_set1_epi32(1)));

        __m256 v_tx = _mm256_blendv_ps(r2_x, _mm256_blendv_ps(r1_x, r0_x, m0), _mm256_or_ps(m0, m1));
        __m256 v_ty = _mm256_blendv_ps(r2_y, _mm256_blendv_ps(r1_y, r0_y, m0), _mm256_or_ps(m0, m1));
        __m256 v_tz = _mm256_blendv_ps(r2_z, _mm256_blendv_ps(r1_z, r0_z, m0), _mm256_or_ps(m0, m1));

        APPLY_SPRING_PHYSICS();
    }
    // ========================================================
    // SCALAR TAIL LOOP (For Safety - Mod 8 remainders)
    // ========================================================
    float k = 4.0f * dt;
    float damp = 0.92f;
    float time_ang = time * 2.5f;
    float r = 7000.0f;

    for (; i < count; i++) {
        float s = seed[i];
        float angle = (s * 3.14159f * 2.0f) + time_ang;

        float c = cosf(angle);
        float sa = sinf(angle);

        float tx, ty, tz;
        int ring = i % 3;

        // 1. Calculate Target Position based on Ring ID
        if (ring == 0) {
            // Ring 0: XY Plane
            tx = cx + r * c;
            ty = cy + r * sa;
            tz = cz;
        } else if (ring == 1) {
            // Ring 1: XZ Plane
            tx = cx + r * c;
            ty = cy;
            tz = cz + r * sa;
        } else {
            // Ring 2: YZ Plane
            tx = cx;
            ty = cy + r * c;
            tz = cz + r * sa;
        }

        // 2. SPRING PHYSICS
        float p_x = px[i], p_y = py[i], p_z = pz[i];
        float v_x = vx[i], v_y = vy[i], v_z = vz[i];

        v_x = (v_x + (tx - p_x) * k) * damp;
        v_y = (v_y + (ty - p_y) * k) * damp;
        v_z = (v_z + (tz - p_z) * k) * damp;

        px[i] = p_x + v_x * dt;
        py[i] = p_y + v_y * dt;
        pz[i] = p_z + v_z * dt;

        vx[i] = v_x;
        vy[i] = v_y;
        vz[i] = v_z;
    }
}
EXPORT void vmath_swarm_metal(int count, float* px, float* py, float* pz, float* vx, float* vy, float* vz, float* seed, float cx, float cy, float cz, float time, float dt, float noise_blend) {
    __m256 v_cx = _mm256_set1_ps(cx), v_cy = _mm256_set1_ps(cy), v_cz = _mm256_set1_ps(cz);
    __m256 v_time = _mm256_set1_ps(time);
    __m256 v_blend = _mm256_set1_ps(noise_blend);
    __m256 v_radius = _mm256_set1_ps(4000.0f);
    __m256 v_max_disp = _mm256_set1_ps(3000.0f); // Max noise distortion

    __m256 v_dt = _mm256_set1_ps(dt);
    __m256 v_k = _mm256_set1_ps(4.0f * dt); // Spring stiffness
    __m256 v_damp = _mm256_set1_ps(0.92f);  // Friction

    int i = 0;
    // BLAST 8 PARTICLES PER CYCLE
    for (; i <= count - 8; i += 8) {
        __m256 v_s = _mm256_loadu_ps(&seed[i]);

        // 1. FAST SPHERICAL MAPPING (Fibonacci-style distribution without acos)
        // Z goes from 1.0 to -1.0 based on seed
        __m256 v_sz = _mm256_fnmadd_ps(v_s, _mm256_set1_ps(2.0f), _mm256_set1_ps(1.0f));
        // Radius at this Z: r_xy = sqrt(1.0 - z*z)
        __m256 v_rxy = _mm256_sqrt_ps(_mm256_fnmadd_ps(v_sz, v_sz, _mm256_set1_ps(1.0f)));
        // Phi rotates wildly based on seed
        __m256 v_phi = _mm256_mul_ps(v_s, _mm256_set1_ps(10000.0f));

        __m256 v_sx = _mm256_mul_ps(v_rxy, fast_cos_avx(v_phi));
        __m256 v_sy = _mm256_mul_ps(v_rxy, fast_sin_avx(v_phi));

        // 2. EVALUATE 4D NOISE AT THE NORMALS
        __m256 v_noise = fast_trig_noise_avx(v_sx, v_sy, v_sz, v_time);

        // 3. APPLY DISPLACEMENT (Using FMA to blend seamlessly!)
        // displacement = noise * noise_blend * max_disp
        __m256 v_disp = _mm256_mul_ps(v_noise, _mm256_mul_ps(v_blend, v_max_disp));

        // Target Pos = Center + Normal * (Radius + Displacement)
        __m256 v_final_r = _mm256_add_ps(v_radius, v_disp);
        __m256 v_tx = _mm256_fmadd_ps(v_sx, v_final_r, v_cx);
        __m256 v_ty = _mm256_fmadd_ps(v_sy, v_final_r, v_cy);
        __m256 v_tz = _mm256_fmadd_ps(v_sz, v_final_r, v_cz);

        // 4. SPRING PHYSICS (Pull current pos toward Target Pos)
        __m256 v_px = _mm256_loadu_ps(&px[i]);
        __m256 v_py = _mm256_loadu_ps(&py[i]);
        __m256 v_pz = _mm256_loadu_ps(&pz[i]);

        __m256 v_vx = _mm256_loadu_ps(&vx[i]);
        __m256 v_vy = _mm256_loadu_ps(&vy[i]);
        __m256 v_vz = _mm256_loadu_ps(&vz[i]);

        // v += (target - p) * k * dt; v *= damp;
        v_vx = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_sub_ps(v_tx, v_px), v_k, v_vx), v_damp);
        v_vy = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_sub_ps(v_ty, v_py), v_k, v_vy), v_damp);
        v_vz = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_sub_ps(v_tz, v_pz), v_k, v_vz), v_damp);

        // p += v * dt;
        v_px = _mm256_fmadd_ps(v_vx, v_dt, v_px);
        v_py = _mm256_fmadd_ps(v_vy, v_dt, v_py);
        v_pz = _mm256_fmadd_ps(v_vz, v_dt, v_pz);

        _mm256_storeu_ps(&px[i], v_px);
        _mm256_storeu_ps(&py[i], v_py);
        _mm256_storeu_ps(&pz[i], v_pz);
        _mm256_storeu_ps(&vx[i], v_vx);
        _mm256_storeu_ps(&vy[i], v_vy);
        _mm256_storeu_ps(&vz[i], v_vz);
    }

    // ========================================================
    // SCALAR TAIL LOOP (For Safety - Mod 8 remainders)
    // ========================================================
    for (; i < count; i++) {
        float s = seed[i];

        // 1. FAST SPHERICAL MAPPING
        // _mm256_fnmadd_ps(v_s, 2.0, 1.0) equates to: -(s * 2.0) + 1.0
        float sz = 1.0f - (s * 2.0f);
        float rxy = sqrtf(1.0f - (sz * sz));
        float phi = s * 10000.0f;

        // Standard math is perfectly fine here since it processes 7 particles max!
        float sx = rxy * cosf(phi); 
        float sy = rxy * sinf(phi);

        // 2. EVALUATE 4D NOISE
        // If you wrote a fast_trig_noise_scalar function, call it here!
        // Otherwise, since this runs on <= 7 particles, a standard inline proxy is virtually free:
        float noise = sinf(sx * 10.0f + time) * cosf(sy * 10.0f + time) * sinf(sz * 10.0f + time); 

        // 3. APPLY DISPLACEMENT
        float disp = noise * noise_blend * 3000.0f;
        float final_r = 4000.0f + disp;

        float tx = cx + sx * final_r;
        float ty = cy + sy * final_r;
        float tz = cz + sz * final_r;

        // 4. SPRING PHYSICS
        float p_x = px[i], p_y = py[i], p_z = pz[i];
        float v_x = vx[i], v_y = vy[i], v_z = vz[i];

        float k = 4.0f * dt;
        float damp = 0.92f;

        // v += (target - p) * k * dt; v *= damp;
        v_x = (v_x + (tx - p_x) * k) * damp;
        v_y = (v_y + (ty - p_y) * k) * damp;
        v_z = (v_z + (tz - p_z) * k) * damp;

        // p += v * dt;
        px[i] = p_x + v_x * dt;
        py[i] = p_y + v_y * dt;
        pz[i] = p_z + v_z * dt;

        vx[i] = v_x;
        vy[i] = v_y;
        vz[i] = v_z;
    }
}
EXPORT void vmath_swarm_smales(int count, float* px, float* py, float* pz, float* vx, float* vy, float* vz, float* seed, float cx, float cy, float cz, float time, float dt, float blend) {
    __m256 v_cx = _mm256_set1_ps(cx), v_cy = _mm256_set1_ps(cy), v_cz = _mm256_set1_ps(cz);
    __m256 v_base_radius = _mm256_set1_ps(4000.0f);

    // THE DOD BLENDING MATH (Calculated once outside the loop!)
    // If blend=0: eversion=1.0, bulge=0.0
    // If blend=1: eversion=cos(t), bulge=sin(t)
    float t_scaled = time * 1.5f;
    float eversion_scalar = 1.0f + blend * (cosf(t_scaled) - 1.0f);
    float bulge_scalar = blend * sinf(t_scaled);

    __m256 v_eversion = _mm256_set1_ps(eversion_scalar);
    __m256 v_bulge = _mm256_set1_ps(bulge_scalar);

    __m256 v_1_2 = _mm256_set1_ps(1.2f);
    __m256 v_0_5 = _mm256_set1_ps(0.5f);
    __m256 v_4_0 = _mm256_set1_ps(4.0f);
    __m256 v_2_0 = _mm256_set1_ps(2.0f);
    __m256 v_3_0 = _mm256_set1_ps(3.0f);
    __m256 v_pi = _mm256_set1_ps(M_PI);
    __m256 v_phi_mul = _mm256_set1_ps(M_PI * 2.0f * 100.0f); // Wrap phi around 100 times

    __m256 v_dt = _mm256_set1_ps(dt);
    __m256 v_k = _mm256_set1_ps(4.0f * dt);
    __m256 v_damp = _mm256_set1_ps(0.92f);

    int i = 0;
    for (; i <= count - 8; i += 8) {
        __m256 v_s = _mm256_loadu_ps(&seed[i]);

        // 1. Map seed to Theta [0, PI] and Phi [0, 2PI * 100]
        __m256 v_theta = _mm256_mul_ps(v_s, v_pi);
        __m256 v_phi = _mm256_mul_ps(v_s, v_phi_mul);

        __m256 v_ny = fast_cos_avx(v_theta);
        __m256 v_sin_theta = fast_sin_avx(v_theta);

        __m256 v_nx = _mm256_mul_ps(v_sin_theta, fast_cos_avx(v_phi));
        __m256 v_nz = _mm256_mul_ps(v_sin_theta, fast_sin_avx(v_phi));

        // 2. PARADOX MATH
        __m256 v_waves = fast_cos_avx(_mm256_mul_ps(v_phi, v_4_0));
        __m256 v_twist = fast_sin_avx(_mm256_mul_ps(v_theta, v_2_0));

        __m256 v_r_corr = _mm256_mul_ps(v_base_radius,
                          _mm256_mul_ps(v_bulge,
                          _mm256_mul_ps(v_waves,
                          _mm256_mul_ps(v_twist, v_1_2))));

        __m256 v_r_main = _mm256_mul_ps(v_base_radius, v_eversion);

        // 3. APPLY DISPLACEMENT
        __m256 v_tx = _mm256_fmadd_ps(v_nx, _mm256_add_ps(v_r_main, v_r_corr), v_cx);
        __m256 v_tz = _mm256_fmadd_ps(v_nz, _mm256_add_ps(v_r_main, v_r_corr), v_cz);

        __m256 v_ty_offset = _mm256_mul_ps(fast_cos_avx(_mm256_mul_ps(v_theta, v_3_0)),
                             _mm256_mul_ps(v_base_radius,
                             _mm256_mul_ps(v_bulge, v_0_5)));

        __m256 v_ty = _mm256_add_ps(v_cy, _mm256_fmadd_ps(v_ny, v_r_main, v_ty_offset));

        // 4. SPRING PHYSICS
        __m256 v_px = _mm256_loadu_ps(&px[i]);
        __m256 v_py = _mm256_loadu_ps(&py[i]);
        __m256 v_pz = _mm256_loadu_ps(&pz[i]);

        __m256 v_vx = _mm256_loadu_ps(&vx[i]);
        __m256 v_vy = _mm256_loadu_ps(&vy[i]);
        __m256 v_vz = _mm256_loadu_ps(&vz[i]);

        v_vx = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_sub_ps(v_tx, v_px), v_k, v_vx), v_damp);
        v_vy = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_sub_ps(v_ty, v_py), v_k, v_vy), v_damp);
        v_vz = _mm256_mul_ps(_mm256_fmadd_ps(_mm256_sub_ps(v_tz, v_pz), v_k, v_vz), v_damp);

        v_px = _mm256_fmadd_ps(v_vx, v_dt, v_px);
        v_py = _mm256_fmadd_ps(v_vy, v_dt, v_py);
        v_pz = _mm256_fmadd_ps(v_vz, v_dt, v_pz);

        _mm256_storeu_ps(&px[i], v_px);
        _mm256_storeu_ps(&py[i], v_py);
        _mm256_storeu_ps(&pz[i], v_pz);
        _mm256_storeu_ps(&vx[i], v_vx);
        _mm256_storeu_ps(&vy[i], v_vy);
        _mm256_storeu_ps(&vz[i], v_vz);
    }
    // ========================================================
    // SCALAR TAIL LOOP (For Safety - Mod 8 remainders)
    // ========================================================
    for (; i < count; i++) {
        float s = seed[i];

        // 1. Map seed to Theta and Phi
        float theta = s * M_PI;
        float phi = s * (M_PI * 2.0f * 100.0f);

        float ny = cosf(theta);
        float sin_theta = sinf(theta);

        float nx = sin_theta * cosf(phi);
        float nz = sin_theta * sinf(phi);

        // 2. PARADOX MATH
        float waves = cosf(phi * 4.0f);
        float twist = sinf(theta * 2.0f);

        float r_corr = 4000.0f * bulge_scalar * waves * twist * 1.2f;
        float r_main = 4000.0f * eversion_scalar;

        // 3. APPLY DISPLACEMENT
        float tx = cx + nx * (r_main + r_corr);
        float tz = cz + nz * (r_main + r_corr);

        float ty_offset = cosf(theta * 3.0f) * 4000.0f * bulge_scalar * 0.5f;
        float ty = cy + (ny * r_main) + ty_offset;

        // 4. SPRING PHYSICS
        float p_x = px[i], p_y = py[i], p_z = pz[i];
        float v_x = vx[i], v_y = vy[i], v_z = vz[i];

        float k = 4.0f * dt;
        float damp = 0.92f;

        // v += (target - p) * k * dt; v *= damp;
        v_x = (v_x + (tx - p_x) * k) * damp;
        v_y = (v_y + (ty - p_y) * k) * damp;
        v_z = (v_z + (tz - p_z) * k) * damp;

        // p += v * dt;
        px[i] = p_x + v_x * dt;
        py[i] = p_y + v_y * dt;
        pz[i] = p_z + v_z * dt;

        vx[i] = v_x;
        vy[i] = v_y;
        vz[i] = v_z;
    }
}
EXPORT void vmath_swarm_apply_explosion(int count, float* px, float* py, float* pz, float* vx, float* vy, float* vz, float ex, float ey, float ez, float force, float radius) {
    __m256 v_ex = _mm256_set1_ps(ex), v_ey = _mm256_set1_ps(ey), v_ez = _mm256_set1_ps(ez);
    __m256 v_r2 = _mm256_set1_ps(radius * radius);
    __m256 v_1 = _mm256_set1_ps(1.0f);
    __m256 v_force = _mm256_set1_ps(force);
    __m256 v_inv_radius = _mm256_set1_ps(1.0f / radius);

    int i = 0; // <--- EXTRACTED SO IT SURVIVES FOR THE SCALAR LOOP!
    for (; i <= count - 8; i += 8) {
        __m256 dx = _mm256_sub_ps(_mm256_loadu_ps(&px[i]), v_ex);
        __m256 dy = _mm256_sub_ps(_mm256_loadu_ps(&py[i]), v_ey);
        __m256 dz = _mm256_sub_ps(_mm256_loadu_ps(&pz[i]), v_ez);

        __m256 dist2 = _mm256_fmadd_ps(dz, dz, _mm256_fmadd_ps(dy, dy, _mm256_mul_ps(dx, dx)));

        // Mask: 1.0f < dist2 < r2
        __m256 mask = _mm256_and_ps(_mm256_cmp_ps(dist2, v_r2, _CMP_LT_OQ), _mm256_cmp_ps(dist2, v_1, _CMP_GT_OQ));

        if (!_mm256_testz_ps(mask, mask)) {
            __m256 inv_dist = _mm256_rsqrt_ps(dist2); // Fast hardware inverse square root
            __m256 dist = _mm256_mul_ps(dist2, inv_dist);

            // f = force * (1.0f - dist * inv_radius)
            __m256 f = _mm256_mul_ps(v_force, _mm256_sub_ps(v_1, _mm256_mul_ps(dist, v_inv_radius)));
            __m256 f_inv_dist = _mm256_mul_ps(f, inv_dist); // (f / dist)

            __m256 v_vx = _mm256_loadu_ps(&vx[i]);
            __m256 v_vy = _mm256_loadu_ps(&vy[i]);
            __m256 v_vz = _mm256_loadu_ps(&vz[i]);

            v_vx = _mm256_blendv_ps(v_vx, _mm256_fmadd_ps(dx, f_inv_dist, v_vx), mask);
            v_vy = _mm256_blendv_ps(v_vy, _mm256_fmadd_ps(dy, f_inv_dist, v_vy), mask);
            v_vz = _mm256_blendv_ps(v_vz, _mm256_fmadd_ps(dz, f_inv_dist, v_vz), mask);

            _mm256_storeu_ps(&vx[i], v_vx);
            _mm256_storeu_ps(&vy[i], v_vy);
            _mm256_storeu_ps(&vz[i], v_vz);
        }
    }
    // ========================================================
    // SCALAR TAIL LOOP (For Safety - Mod 8 remainders)
    // ========================================================
    float inv_radius = 1.0f / radius;
    float r2 = radius * radius;

    for (; i < count; i++) {
        float dx = px[i] - ex;
        float dy = py[i] - ey;
        float dz = pz[i] - ez;

        float dist2 = dx * dx + dy * dy + dz * dz;

        // Apply only if it's within the blast radius and not exactly at the origin (to prevent divide-by-zero)
        if (dist2 > 1.0f && dist2 < r2) {
            float dist = sqrtf(dist2);

            // Linear falloff: 100% force at center, 0% at edge of radius
            float f = force * (1.0f - dist * inv_radius);

            // Divide by distance once so we can multiply by the raw dx/dy/dz vectors
            float f_inv_dist = f / dist;

            vx[i] += dx * f_inv_dist;
            vy[i] += dy * f_inv_dist;
            vz[i] += dz * f_inv_dist;
        }
    }
}
EXPORT void vmath_swarm_sort_depth(int count, float* px, float* py, float* pz, int* indices, int* temp_indices, float* distances, float* temp_distances, float cx, float cy, float cz) { 
    for(int j = 0; j < count; j++) indices[j] = j; 
}


// ========================================================
// CORE 2: PHYSICS WORKER (No Rasterizer!)
// ========================================================
THREAD_FUNC vmath_physics_worker(void* arg) {
    PhysicsThreadPayload* p = &g_physics_payload;
    while (1) {
        vmath_mutex_lock(&g_phys_mutex);
        while (g_phys_sig == 0) { vmath_cond_wait(&g_phys_cv_start, &g_phys_mutex); }
        if (g_phys_sig == 2) { vmath_mutex_unlock(&g_phys_mutex); break; }
        vmath_mutex_unlock(&g_phys_mutex);

        RenderMemory* mem = p->mem;
        float time = p->time; float dt = p->dt;
        int r = p->read_idx; int w = p->write_idx;

        for (int i = 0; i < p->command_count; i++) {
            int opcode = p->queue[i];
            int swarm_count = mem->Obj_VertCount[0] / 4; 
            switch (opcode) {
                case 2: vmath_swarm_update_velocities(swarm_count, mem->Swarm_PX[r], mem->Swarm_PY[r], mem->Swarm_PZ[r], mem->Swarm_VX[r], mem->Swarm_VY[r], mem->Swarm_VZ[r], mem->Swarm_PX[w], mem->Swarm_PY[w], mem->Swarm_PZ[w], mem->Swarm_VX[w], mem->Swarm_VY[w], mem->Swarm_VZ[w], -15000, 15000, -4000, 15000, -15000, 15000, dt, -8000.0f * mem->Swarm_GravityBlend); break;
                case 3: vmath_swarm_bundle(swarm_count, mem->Swarm_PX[w], mem->Swarm_PY[w], mem->Swarm_PZ[w], mem->Swarm_VX[w], mem->Swarm_VY[w], mem->Swarm_VZ[w], mem->Swarm_Seed, 0, 5000, 0, time, dt); break;
                case 4: vmath_swarm_galaxy(swarm_count, mem->Swarm_PX[w], mem->Swarm_PY[w], mem->Swarm_PZ[w], mem->Swarm_VX[w], mem->Swarm_VY[w], mem->Swarm_VZ[w], mem->Swarm_Seed, 0, 5000, 0, time, dt); break;
                case 5: vmath_swarm_tornado(swarm_count, mem->Swarm_PX[w], mem->Swarm_PY[w], mem->Swarm_PZ[w], mem->Swarm_VX[w], mem->Swarm_VY[w], mem->Swarm_VZ[w], mem->Swarm_Seed, 0, 5000, 0, time, dt); break;
                case 6: vmath_swarm_gyroscope(swarm_count, mem->Swarm_PX[w], mem->Swarm_PY[w], mem->Swarm_PZ[w], mem->Swarm_VX[w], mem->Swarm_VY[w], mem->Swarm_VZ[w], mem->Swarm_Seed, 0, 5000, 0, time, dt); break;
                case 7: vmath_swarm_metal(swarm_count, mem->Swarm_PX[w], mem->Swarm_PY[w], mem->Swarm_PZ[w], mem->Swarm_VX[w], mem->Swarm_VY[w], mem->Swarm_VZ[w], mem->Swarm_Seed, 0, 5000, 0, time, dt, mem->Swarm_MetalBlend); break;
                case 8: vmath_swarm_smales(swarm_count, mem->Swarm_PX[w], mem->Swarm_PY[w], mem->Swarm_PZ[w], mem->Swarm_VX[w], mem->Swarm_VY[w], mem->Swarm_VZ[w], mem->Swarm_Seed, 0, 5000, 0, time, dt, mem->Swarm_ParadoxBlend); break;
                case 12: vmath_swarm_apply_explosion(swarm_count, mem->Swarm_PX[w], mem->Swarm_PY[w], mem->Swarm_PZ[w], mem->Swarm_VX[w], mem->Swarm_VY[w], mem->Swarm_VZ[w], 0, 5000, 0, 5000000.0f * dt, 15000.0f); break;
                case 13: vmath_swarm_apply_explosion(swarm_count, mem->Swarm_PX[w], mem->Swarm_PY[w], mem->Swarm_PZ[w], mem->Swarm_VX[w], mem->Swarm_VY[w], mem->Swarm_VZ[w], 0, 5000, 0, -4000000.0f * dt, 20000.0f); break;
                case 14: vmath_swarm_sort_depth(swarm_count, mem->Swarm_PX[w], mem->Swarm_PY[w], mem->Swarm_PZ[w], mem->Swarm_Indices[w], mem->Swarm_TempIndices, mem->Swarm_Distances, mem->Swarm_TempDistances, 0, 0, 0); break;
            }
        }

        vmath_mutex_lock(&g_phys_mutex);
        g_phys_sig = 0; g_phys_done = 1;
        vmath_cond_broadcast(&g_phys_cv_done);
        vmath_mutex_unlock(&g_phys_mutex);
    }
    return THREAD_RETURN_VAL;
}

// ========================================================
// THE DUMB DISPATCHER (No raster dispatching!)
// ========================================================
EXPORT void vmath_execute_queue(int command_count, float time, float dt, int read_idx, int write_idx) {
    g_physics_payload.command_count = command_count;
    g_physics_payload.queue = g_queue; g_physics_payload.mem = g_mem; g_physics_payload.time = time; g_physics_payload.dt = dt;
    g_physics_payload.read_idx = read_idx; g_physics_payload.write_idx = write_idx;

    vmath_mutex_lock(&g_phys_mutex);
    g_phys_done = 0; g_phys_sig = 1;
    vmath_cond_broadcast(&g_phys_cv_start);
    vmath_mutex_unlock(&g_phys_mutex);

    for (int i = 0; i < command_count; i++) {
        int opcode = g_queue[i];
        int swarm_count = g_mem->Obj_VertCount[0] / 4;

        if (opcode == 9) { // SWARM_GEN_QUADS
            vmath_swarm_generate_quads(swarm_count, g_mem->Swarm_PX[read_idx], g_mem->Swarm_PY[read_idx], g_mem->Swarm_PZ[read_idx], g_mem->Vert_LX + g_mem->Obj_VertStart[0], g_mem->Vert_LY + g_mem->Obj_VertStart[0], g_mem->Vert_LZ + g_mem->Obj_VertStart[0], 120.0f, g_cam, g_half_w, g_half_h, g_mem->Swarm_Indices[read_idx]);
        } 
        else if (opcode == 11) { // RENDER_CULL
            int id = g_queue[++i];
            float cpx = g_cam->x, cpy = g_cam->y, cpz = g_cam->z;
            float cfw_x = g_cam->fwx, cfw_y = g_cam->fwy, cfw_z = g_cam->fwz;
            float crt_x = g_cam->rtx, crt_z = g_cam->rtz;
            float cup_x = g_cam->upx, cup_y = g_cam->upy, cup_z = g_cam->upz;
            float cam_fov = g_cam->fov;
            float ox = g_mem->Obj_X[id], oy = g_mem->Obj_Y[id], oz = g_mem->Obj_Z[id];
            float rx = g_mem->Obj_RTX[id], ry = g_mem->Obj_RTY[id], rz = g_mem->Obj_RTZ[id];
            float ux = g_mem->Obj_UPX[id], uy = g_mem->Obj_UPY[id], uz = g_mem->Obj_UPZ[id];
            float fx = g_mem->Obj_FWX[id], fy = g_mem->Obj_FWY[id], fz = g_mem->Obj_FWZ[id];
            int vStart = g_mem->Obj_VertStart[id], vCount = g_mem->Obj_VertCount[id];

            // REMOVED OLD CALLS
        }
    }

    vmath_mutex_lock(&g_phys_mutex);
    while (g_phys_done == 0) { vmath_cond_wait(&g_phys_cv_done, &g_phys_mutex); }
    vmath_mutex_unlock(&g_phys_mutex);
}

EXPORT void vmath_init_thread_pool() {
    vmath_mutex_init(&g_phys_mutex); vmath_cond_init(&g_phys_cv_start); vmath_cond_init(&g_phys_cv_done);
    g_physics_thread = vmath_thread_start(vmath_physics_worker, NULL);
}

EXPORT void vmath_shutdown_thread_pool() {
    vmath_mutex_lock(&g_phys_mutex); g_phys_sig = 2; vmath_cond_broadcast(&g_phys_cv_start); vmath_mutex_unlock(&g_phys_mutex);
    vmath_thread_join(g_physics_thread);
    vmath_mutex_destroy(&g_phys_mutex); vmath_cond_destroy(&g_phys_cv_start); vmath_cond_destroy(&g_phys_cv_done);
}
