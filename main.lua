-- main.lua
local ffi = require("ffi")

print("[LUA] VibeEngine: Booting Vulkan + AVX2 Interop...")

Config = {
    window_title = "VibeEngine: Vulkan AVX2 FFI Pipeline",
    fullscreen = true
}

-- 1. Load the core modules (Set-it-and-forget-it OS routing)
local VibeMath = ffi.load(jit.os == "Windows" and "vibemath" or "./libvibemath.so")
local Memory = require("memory")
local Sequence = require("sequence")

local SwarmModule
local CameraModule
local global_time = 0
local read_buffer = 0
local write_buffer = 1

local time = 0
DrawCount = 1000000 -- Make sure your global draw count is set for Vulkan!

local CMD = {
    CLEAR = 1,
    SWARM_APPLY_BASE_PHYSICS = 2,
    SWARM_BUNDLE = 3,
    SWARM_GALAXY = 4,
    SWARM_TORNADO = 5,
    SWARM_GYROSCOPE = 6,
    SWARM_METAL = 7,
    SWARM_PARADOX = 8,
    SWARM_GEN_QUADS = 9,
    SPHERE_TICK = 10,
    RENDER_CULL = 11,
    SWARM_EXPLOSION_PUSH = 12,
    SWARM_EXPLOSION_PULL = 13,
    SWARM_SORT_DEPTH = 14
}

-- ========================================================
-- LOVE2D API MOCKING (So camera.lua works unmodified!)
-- ========================================================
love = {
    keyboard = {
        isDown = function(key)
            if key == "w" then return Engine.isKeyDown(87) end
            if key == "a" then return Engine.isKeyDown(65) end
            if key == "s" then return Engine.isKeyDown(83) end
            if key == "d" then return Engine.isKeyDown(68) end
            if key == "q" then return Engine.isKeyDown(81) end
            if key == "e" then return Engine.isKeyDown(69) end
            if key == "space" then return Engine.isKeyDown(32) end
            if key == "left" then return Engine.isKeyDown(263) end
            if key == "right" then return Engine.isKeyDown(262) end
            if key == "up" then return Engine.isKeyDown(265) end
            if key == "down" then return Engine.isKeyDown(264) end
            return false
        end
    },
    mouse = {
        getRelativeMode = function() return true end,
        -- [THE FIX] Bind to our new GLFW bridge!
        isDown = function(button) return Engine.isMouseDown(button) end
    }
}
-- ========================================================
-- DUMMY BUFFERS FOR THE CPU RASTERIZER
-- ========================================================
local CANVAS_W, CANVAS_H = 1920, 1080
local DummyScreen = ffi.new("uint32_t[?]", CANVAS_W * CANVAS_H)
local DummyZBuffer = ffi.new("float[?]", CANVAS_W * CANVAS_H)

-- ========================================================
-- ENGINE HOOKS
-- ========================================================
function love_load()
    print("[LUA] Running love_load...")
    Engine.setRelativeMode(true)

    -- [THE FIX] Restore the FOV so the CPU stops dividing by zero!
    MainCamera.fov = (CANVAS_W / 800) * 600
    -- ========================================================
    -- [THE FIX] THE VULKAN SOA OVERRIDE
    -- Vulkan is fully booted! Hijack the Arrays now!
    -- ========================================================
    print("[LUA] Hijacking CPU RAM with RTX 3050 Pointers...")
    Memory.Arrays.Vert_PX = ffi.cast("float*", Engine.getVRAM_X())
    Memory.Arrays.Vert_PY = ffi.cast("float*", Engine.getVRAM_Y())
    Memory.Arrays.Vert_PZ = ffi.cast("float*", Engine.getVRAM_Z())
    
    -- Lock them into the C Struct!
    Memory.RenderStruct.Vert_PX = Memory.Arrays.Vert_PX
    Memory.RenderStruct.Vert_PY = Memory.Arrays.Vert_PY
    Memory.RenderStruct.Vert_PZ = Memory.Arrays.Vert_PZ
    -- ========================================================

    -- NOW we bind the permanent memory architecture to C!
    VibeMath.vmath_bind_engine(Memory.RenderStruct, MainCamera, Memory.Arrays.CommandQueue)
    VibeMath.vmath_set_resolution(CANVAS_W, CANVAS_H, DummyScreen, DummyZBuffer)
    
    Sequence.LoadModule("camera", MainCamera)
    Sequence.LoadModule("swarm")
    SwarmModule = Sequence.Loaded["swarm"]
    CameraModule = Sequence.Loaded["camera"]

    Sequence.RunPhase("Init")

    -- Ignite the Permanent Quad-Core Engine!
    VibeMath.vmath_init_thread_pool()
end
function love_update(dt)
    time = time + dt
    
    local ptrX = Engine.getVRAM_X()
    local ptrY = Engine.getVRAM_Y()
    local ptrZ = Engine.getVRAM_Z()

    -- THE ELECTRICAL SHORTCUT
    -- Bypassing physics, forcing the VRAM to become a 3D Donut
    -- 15.0 is the Major Radius (size of the ring)
    -- 5.0 is the Minor Radius (thickness of the tube)
    VibeMath.vmath_generate_torus(DrawCount, ptrX, ptrY, ptrZ, time, 15.0, 5.0)
end
function OLD_DEPRECATED_love_update(dt)
    dt = math.min(dt, 0.033)
    global_time = global_time + dt

    Sequence.RunPhase("Tick", dt)

    -- CONSTRUCT THE COMMAND QUEUE
    local q = Memory.Arrays.CommandQueue
    local q_len = 0
    local mem = Memory.RenderStruct

    q[q_len] = CMD.CLEAR; q_len = q_len + 1
    q[q_len] = CMD.SWARM_APPLY_BASE_PHYSICS; q_len = q_len + 1
    if love.mouse.isDown(1) then
        q[q_len] = CMD.SWARM_EXPLOSION_PUSH; q_len = q_len + 1
    end
    if love.mouse.isDown(2) then
        q[q_len] = CMD.SWARM_EXPLOSION_PULL; q_len = q_len + 1
    end
    local state = mem.Swarm_State
    if state == 1 then q[q_len] = CMD.SWARM_BUNDLE; q_len = q_len + 1
    elseif state == 2 then q[q_len] = CMD.SWARM_GALAXY; q_len = q_len + 1
    elseif state == 3 then q[q_len] = CMD.SWARM_TORNADO; q_len = q_len + 1
    elseif state == 4 then q[q_len] = CMD.SWARM_GYROSCOPE; q_len = q_len + 1
    elseif state == 5 then q[q_len] = CMD.SWARM_METAL; q_len = q_len + 1
    elseif state == 6 then q[q_len] = CMD.SWARM_PARADOX; q_len = q_len + 1
    end

    q[q_len] = CMD.SWARM_SORT_DEPTH; q_len = q_len + 1
    q[q_len] = CMD.SWARM_GEN_QUADS; q_len = q_len + 1

    -- Trigger the AVX2 Projection & CPU Rasterizer
    q[q_len] = CMD.RENDER_CULL; q_len = q_len + 1
    q[q_len] = 0;               q_len = q_len + 1

    read_buffer, write_buffer = write_buffer, read_buffer

    -- EXECUTE THE AVX2 BLACK MAGIC
    VibeMath.vmath_execute_queue(q_len, global_time, dt, read_buffer, write_buffer)

    -- Tell Vulkan EXACTLY how many vertices to draw from VRAM!
    -- Every particle spawns 4 vertices (a tetrahedron)
    DrawCount = mem.Obj_VertCount[0]
end

function love_mousemoved(x, y, dx, dy)
    Sequence.RunPhase("MouseMoved", x, y, dx, dy)
end
