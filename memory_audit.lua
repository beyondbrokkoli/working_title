-- memory_audit.lua
local ffi = require("ffi")
local Auditor = {}

ffi.cdef[[
    typedef struct { float x, y, z; } VertexAoS;
]]

function Auditor.RunPreflight(memory_module, draw_count)
    print("\n===========================================")
    print("      VIBE ENGINE: VRAM SILICON AUDIT      ")
    print("===========================================")

    local raw_vbo = Engine.get_gpu_vbo()
    if raw_vbo == nil then
        print("[FATAL]: Vulkan VBO pointer is NULL. Handshake failed.")
        return false
    end

    -- Cast the raw pointer to our specific C-struct array
    local gpu_vertices = ffi.cast("VertexAoS*", raw_vbo)
    
    print(string.format("[AUDIT]: Vulkan VBO Mapped at Address: %s", tostring(raw_vbo)))
    
    if draw_count > 0 then
        -- 1 Particle = 4 Vertices. Let's check the first 2 particles (8 vertices).
        local check_count = math.min(draw_count * 4, 8) 
        print(string.format("[AUDIT]: Sampling first %d VRAM vertices...\n", check_count))

        for i = 0, check_count - 1 do
            local v = gpu_vertices[i]
            print(string.format("  |- VRAM Vertex %d: (X: %7.1f, Y: %7.1f, Z: %7.1f)", i, v.x, v.y, v.z))
        end
    else
        print("\n[LOG]: DrawCount is 0. Skipping vertex sample.")
    end

    print("===========================================\n")
    return true
end

return Auditor
