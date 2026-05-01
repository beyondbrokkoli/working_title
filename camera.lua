-- modules/camera.lua
local ffi = require("ffi")
local max, min, cos, sin, tan = math.max, math.min, math.cos, math.sin, math.tan

return function(MainCamera)
    local CameraModule = {}

    local function UpdateBasis()
        local cy, sy = cos(MainCamera.yaw), sin(MainCamera.yaw)
        local cp, sp = cos(MainCamera.pitch), sin(MainCamera.pitch)
        MainCamera.fwx, MainCamera.fwy, MainCamera.fwz = sy * cp, sp, cy * cp
        MainCamera.rtx, MainCamera.rty, MainCamera.rtz = cy, 0, -sy
        MainCamera.upx = MainCamera.fwy * MainCamera.rtz
        MainCamera.upy = MainCamera.fwz * MainCamera.rtx - MainCamera.fwx * MainCamera.rtz
        MainCamera.upz = -MainCamera.fwy * MainCamera.rtx
    end

    function CameraModule.Init()
        -- [THE GOD MODE SPAWN]
        -- The Swarm lives at Y = 5000 and expands outward by 10,000+ units.
        -- We spawn far back on the Z-axis, slightly elevated, looking straight at it.
        MainCamera.x, MainCamera.y, MainCamera.z = 0, 7000, 25000

        -- Point the lens straight ahead, with a slight downward tilt
        MainCamera.yaw = -math.pi / 2  -- (Adjust to +math.pi/2 if it looks backward!)
        MainCamera.pitch = -0.1

        UpdateBasis()
    end

    function CameraModule.UpdateVectors()
        UpdateBasis()
    end

    function CameraModule.Tick(dt)
        local s = 200.0 * dt -- Slowed down slightly for donut viewing!
        if love.keyboard.isDown("w") then MainCamera.x, MainCamera.y, MainCamera.z = MainCamera.x + MainCamera.fwx * s, MainCamera.y + MainCamera.fwy * s, MainCamera.z + MainCamera.fwz * s end
        if love.keyboard.isDown("s") then MainCamera.x, MainCamera.y, MainCamera.z = MainCamera.x - MainCamera.fwx * s, MainCamera.y - MainCamera.fwy * s, MainCamera.z - MainCamera.fwz * s end
        if love.keyboard.isDown("a") then MainCamera.x, MainCamera.z = MainCamera.x - MainCamera.rtx * s, MainCamera.z - MainCamera.rtz * s end
        if love.keyboard.isDown("d") then MainCamera.x, MainCamera.z = MainCamera.x + MainCamera.rtx * s, MainCamera.z + MainCamera.rtz * s end
        if love.keyboard.isDown("e") then MainCamera.y = MainCamera.y - s end
        if love.keyboard.isDown("q") then MainCamera.y = MainCamera.y + s end
        
        local rotSpeed = 2.5 * dt
        if love.keyboard.isDown("left") then MainCamera.yaw = MainCamera.yaw - rotSpeed end
        if love.keyboard.isDown("right") then MainCamera.yaw = MainCamera.yaw + rotSpeed end
        if love.keyboard.isDown("up") then MainCamera.pitch = MainCamera.pitch - rotSpeed end
        if love.keyboard.isDown("down") then MainCamera.pitch = MainCamera.pitch + rotSpeed end

        MainCamera.pitch = math.max(-1.56, math.min(1.56, MainCamera.pitch))
        UpdateBasis()
    end

    function CameraModule.MouseMoved(x, y, dx, dy)
        if love.mouse.getRelativeMode() then
            MainCamera.yaw = MainCamera.yaw + (dx * 0.002)
            MainCamera.pitch = MainCamera.pitch - (dy * 0.002) -- Flipped for standard mouselook
            MainCamera.pitch = max(-1.56, min(1.56, MainCamera.pitch))
            UpdateBasis()
        end
    end

    -- ========================================================
    -- VULKAN CAMERA MATRIX BUILDER
    -- ========================================================
    function CameraModule.GetViewProjectionMatrix(aspect)
        -- 1. Extract basis vectors (calculated from yaw/pitch)
        local cx, cy, cz = MainCamera.x, MainCamera.y, MainCamera.z
        local fx, fy, fz = MainCamera.fwx, MainCamera.fwy, MainCamera.fwz
        local ux, uy, uz = MainCamera.upx, MainCamera.upy, MainCamera.upz
        local rx, ry, rz = MainCamera.rtx, MainCamera.rty, MainCamera.rtz

        -- 2. View Matrix (Translates and rotates the world around the camera)
        -- Dot products to calculate camera translation offsets
        local tx = -(rx*cx + ry*cy + rz*cz)
        local ty = -(ux*cx + uy*cy + uz*cz)
        local tz =  (fx*cx + fy*cy + fz*cz)

        local view = {
            rx, ux, -fx, 0,
            ry, uy, -fy, 0,
            rz, uz, -fz, 0,
            tx, ty,  tz, 1
        }

        -- 3. Projection Matrix (Vulkan Y-Down, Z 0 to 1)
        local fov = math.rad(60)
        local f = 1.0 / math.tan(fov * 0.5)
        local zNear = 1.0
        local zFar = 100000.0

        local proj = {
            f / aspect, 0,  0, 0,
            0, f, 0, 0, -- NOT FLIPPING
            0,  0, zFar / (zNear - zFar), -1,
            0,  0, -(zFar * zNear) / (zFar - zNear), 0
        }

        -- 4. Matrix Multiplication: out = Proj * View
        local out = {}
        for r = 0, 3 do
            for c = 0, 3 do
                local sum = 0
                for k = 0, 3 do
                    sum = sum + view[r*4 + k + 1] * proj[k*4 + c + 1]
                end
                out[r*4 + c + 1] = sum
            end
        end

        -- Unpack the 16 floats directly onto the Lua stack for main.c to grab!
        return unpack(out)
    end

    return CameraModule
end
