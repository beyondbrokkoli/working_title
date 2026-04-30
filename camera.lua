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
        -- [THE CINEMATIC SPAWN]
        -- Placed offset from the Sun vector to reveal perfect Lambertian shadows
        MainCamera.x, MainCamera.y, MainCamera.z = -150, 50, 150

        -- Point the lens roughly back at the origin where the donut spawns
        MainCamera.yaw, MainCamera.pitch = 2.35, -0.3

        UpdateBasis()
    end

    function CameraModule.UpdateVectors()
        UpdateBasis()
    end

    function CameraModule.Tick(dt)
        local s = 50.0 * dt -- Slowed down slightly for donut viewing!
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
        local fov = 1.0472 -- 60 degrees
        local zNear = 0.1
        local zFar = 10000.0

        -- 1. Build Perspective Matrix
        local f = 1.0 / tan(fov * 0.5)
        local p00 = f / aspect
        local p11 = -f -- Inverted Y for Vulkan!
        local p22 = zFar / (zNear - zFar)
        local p23 = (zFar * zNear) / (zNear - zFar) -- Fixed sign error here

        -- 2. Build View Matrix using existing Forward, Right, Up vectors
        local rx, ry, rz = MainCamera.rtx, MainCamera.rty, MainCamera.rtz
        local ux, uy, uz = MainCamera.upx, MainCamera.upy, MainCamera.upz
        local fx, fy, fz = MainCamera.fwx, MainCamera.fwy, MainCamera.fwz
        local cx, cy, cz = MainCamera.x, MainCamera.y, MainCamera.z

        -- Translation (Dot products of vectors and position)
        local tx = -(rx * cx + ry * cy + rz * cz)
        local ty = -(ux * cx + uy * cy + uz * cz)
        local tz =  (fx * cx + fy * cy + fz * cz)

        -- 3. Multiply Proj * View and return exactly 16 numbers
        -- Column 0
        local m0  = p00 * rx
        local m1  = p11 * ux
        local m2  = p22 * -fx
        local m3  = fx
        -- Column 1
        local m4  = p00 * ry
        local m5  = p11 * uy
        local m6  = p22 * -fy
        local m7  = fy
        -- Column 2
        local m8  = p00 * rz
        local m9  = p11 * uz
        local m10 = p22 * -fz
        local m11 = fz
        -- Column 3
        local m12 = p00 * tx
        local m13 = p11 * ty
        local m14 = p22 * tz + p23 -- Fixed column addition here
        local m15 = -tz

        return m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14, m15
    end

    return CameraModule
end
