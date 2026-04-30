-- modules/camera.lua
local ffi = require("ffi")
local max, min, cos, sin, tan = math.max, math.min, math.cos, math.sin, math.tan

-- Pre-allocate a 16-float array for the matrix so we don't trigger Garbage Collection every frame!
local viewProjMatrix = ffi.new("float[16]")

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
        local proj00 = f / aspect
        local proj11 = -f -- Inverted Y for Vulkan!
        local proj22 = zFar / (zNear - zFar)
        local proj23 = -1.0
        local proj32 = -(zFar * zNear) / (zFar - zNear)

        -- 2. Build View Matrix using existing Forward, Right, Up vectors
        local rx, ry, rz = MainCamera.rtx, MainCamera.rty, MainCamera.rtz
        local ux, uy, uz = MainCamera.upx, MainCamera.upy, MainCamera.upz
        local fx, fy, fz = MainCamera.fwx, MainCamera.fwy, MainCamera.fwz
        local cx, cy, cz = MainCamera.x, MainCamera.y, MainCamera.z

        -- Translation (Dot products of vectors and position)
        local tx = -(rx * cx + ry * cy + rz * cz)
        local ty = -(ux * cx + uy * cy + uz * cz)
        local tz =  (fx * cx + fy * cy + fz * cz) -- Forward is -Z in Vulkan view space

        -- 3. Multiply Proj * View directly into the 16-float array
        viewProjMatrix[0]  = proj00 * rx
        viewProjMatrix[1]  = proj11 * ux
        viewProjMatrix[2]  = proj22 * -fx + proj23 * tx
        viewProjMatrix[3]  = -fx
        
        viewProjMatrix[4]  = proj00 * ry
        viewProjMatrix[5]  = proj11 * uy
        viewProjMatrix[6]  = proj22 * -fy + proj23 * ty
        viewProjMatrix[7]  = -fy
        
        viewProjMatrix[8]  = proj00 * rz
        viewProjMatrix[9]  = proj11 * uz
        viewProjMatrix[10] = proj22 * -fz + proj23 * tz
        viewProjMatrix[11] = -fz
        
        viewProjMatrix[12] = proj00 * tx
        viewProjMatrix[13] = proj11 * ty
        viewProjMatrix[14] = proj22 * tz + proj23 * 1.0
        viewProjMatrix[15] = tz

        return viewProjMatrix
    end

    return CameraModule
end
