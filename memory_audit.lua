--[[
  SYSTEM: VibeEngine Advanced Memory Audit
  MODULE: memory_audit.lua
  PURPOSE: Geometry validation, NaN detection, and Pointer alignment verification.
--]]

local ffi = require("ffi")
local Auditor = {}

---------------------------------------------------------
-- 1. UTILITIES (The Engine Room)
---------------------------------------------------------

-- Validates if a float is a real, usable number (Catches NaN and Inf)
local function is_valid_float(val)
    return v == v and val > -1e10 and val < 1e10
end

-- Extracts the raw memory address for debugging
local function get_address(cdata_ptr)
    return tonumber(ffi.cast("uintptr_t", cdata_ptr))
end

---------------------------------------------------------
-- 2. LOGIC (Validation & Friction Tracing)
---------------------------------------------------------

local function preflight_pointer_check(memory_arrays)
    print("\n[AUDIT]: Executing Pointer Preflight Check...")
    
    local critical_keys = {"Vert_LX", "Vert_LY", "Vert_LZ"}
    local passed = true

    for _, key in ipairs(critical_keys) do
        local ptr = memory_arrays[key]
        if not ptr then
            print(string.format("  [CRITICAL]: Missing Array %s!", key))
            passed = false
        else
            local addr = get_address(ptr)
            local alignment = addr % 64
            print(string.format("  |- %s: Address 0x%X (64-byte Alignment Offset: %d)", key, addr, alignment))
            
            if alignment ~= 0 then
                print(string.format("  [WARNING]: %s is NOT 64-byte aligned!", key))
            end
        end
    end
    return passed
end

local function audit_vertex_data(memory_arrays, check_count)
    print(string.format("\n[AUDIT]: Sampling first %d vertices...", check_count))
    
    local xs, ys, zs = memory_arrays.Vert_LX, memory_arrays.Vert_LY, memory_arrays.Vert_LZ
    local invalid_count = 0
    local all_zero = true

    for i = 0, check_count - 1 do
        local x, y, z = xs[i], ys[i], zs[i]
        
        -- Check if geometry actually exists
        if x ~= 0.0 or y ~= 0.0 or z ~= 0.0 then all_zero = false end
        
        -- NaN / Infinity check
        if not (is_valid_float(x) and is_valid_float(y) and is_valid_float(z)) then
            print(string.format("  [CORRUPTION]: Vertex %d contains invalid math -> (X:%.2f, Y:%.2f, Z:%.2f)", i, x, y, z))
            invalid_count = invalid_count + 1
        end
    end

    if all_zero then
        print("  [WARNING]: All sampled vertices are EXACTLY (0,0,0). Is the generator running?")
    elseif invalid_count == 0 then
        print("  |- Identity: Vertex math verified. No corruption detected.")
    end
end

---------------------------------------------------------
-- 3. WORKFLOW (Execution Bridge)
---------------------------------------------------------

function Auditor.RunPreflight(memory_module, draw_count)
    print("\n===========================================")
    print("      VIBE ENGINE: RENDER AUDIT START      ")
    print("===========================================")
    
    if not memory_module or not memory_module.Arrays then
        print("[FATAL]: Memory module invalid or uninitialized.")
        return false
    end

    local safe_pointers = preflight_pointer_check(memory_module.Arrays)
    
    if draw_count > 0 then
        -- We check a sample size of vertices (up to 12 per tetrahedron)
        local sample_size = math.min(draw_count * 12, 100) 
        audit_vertex_data(memory_module.Arrays, sample_size)
    else
        print("\n[LOG]: DrawCount is 0. Skipping vertex geometry audit.")
    end

    print("\n--- POST-AUDIT REVIEW ---")
    if safe_pointers then
        print("[SUCCESS]: Memory architecture is secure. Ready for Vulkan Dispatch.")
    else
        print("[HALT]: Architecture compromised. Check pointer alignment.")
    end
    print("===========================================\n")
end

return Auditor
