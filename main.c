#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <luajit-2.1/lua.h>
#include <luajit-2.1/lualib.h>
#include <luajit-2.1/lauxlib.h>

// THE 3 SOA VRAM POINTERS
void *g_ptrX = NULL, *g_ptrY = NULL, *g_ptrZ = NULL;
GLFWwindow* g_window = NULL;
lua_State* g_L = NULL; 
double g_last_mouse_x = 0.0, g_last_mouse_y = 0.0;
int g_first_mouse = 1;

static int l_isKeyDown(lua_State* L) { int key = luaL_checkinteger(L, 1); lua_pushboolean(L, glfwGetKey(g_window, key) == GLFW_PRESS); return 1; }
static int l_setRelativeMode(lua_State* L) { glfwSetInputMode(g_window, GLFW_CURSOR, lua_toboolean(L, 1) ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL); return 0; }
// Mimics love.mouse.isDown(button)
static int l_isMouseDown(lua_State* L) {
    int button = luaL_checkinteger(L, 1);

    // Love2D uses 1 for Left, 2 for Right. GLFW uses 0 for Left, 1 for Right.
    // We map Love2D's 1-based indexing to GLFW's 0-based indexing.
    int glfw_button = button - 1;

    int state = glfwGetMouseButton(g_window, glfw_button);
    lua_pushboolean(L, state == GLFW_PRESS);
    return 1;
}
// EXPORT THE 3 BUFFERS TO LUA
static int l_getVRAM_X(lua_State* L) { lua_pushlightuserdata(L, g_ptrX); return 1; }
static int l_getVRAM_Y(lua_State* L) { lua_pushlightuserdata(L, g_ptrY); return 1; }
static int l_getVRAM_Z(lua_State* L) { lua_pushlightuserdata(L, g_ptrZ); return 1; }

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    if (g_first_mouse) { g_last_mouse_x = xpos; g_last_mouse_y = ypos; g_first_mouse = 0; }
    double dx = xpos - g_last_mouse_x, dy = ypos - g_last_mouse_y;
    g_last_mouse_x = xpos; g_last_mouse_y = ypos;
    lua_getglobal(g_L, "love_mousemoved");
    if (lua_isfunction(g_L, -1)) {
        lua_pushnumber(g_L, xpos); lua_pushnumber(g_L, ypos); lua_pushnumber(g_L, dx); lua_pushnumber(g_L, dy);
        if (lua_pcall(g_L, 4, 0, 0) != LUA_OK) { printf("[LUA ERROR] %s\n", lua_tostring(g_L, -1)); }
    } else { lua_pop(g_L, 1); }
}

const char* validationLayers[] = { "VK_LAYER_KHRONOS_validation" };

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties; vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) return i;
    } exit(-1);
}

// HELPER TO ALLOCATE AND MAP A VRAM BUFFER
void createSoABuffer(VkDevice device, VkPhysicalDevice gpu, VkDeviceSize size, VkBuffer* buf, VkDeviceMemory* mem, void** ptr) {
    VkBufferCreateInfo info = {0}; info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; info.size = size; info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    vkCreateBuffer(device, &info, NULL, buf);
    VkMemoryRequirements reqs; vkGetBufferMemoryRequirements(device, *buf, &reqs);
    VkMemoryAllocateInfo alloc = {0}; alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; alloc.allocationSize = reqs.size; alloc.memoryTypeIndex = findMemoryType(gpu, reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device, &alloc, NULL, mem);
    vkBindBufferMemory(device, *buf, *mem, 0);
    vkMapMemory(device, *mem, 0, size, 0, ptr);
}

char* readShaderFile(const char* filename, size_t* outSize) {
    FILE* file = fopen(filename, "rb"); if (!file) { printf("FATAL: Failed to open %s\n", filename); exit(-1); }
    fseek(file, 0, SEEK_END); *outSize = ftell(file); fseek(file, 0, SEEK_SET);
    char* buffer = malloc(*outSize); fread(buffer, 1, *outSize, file); fclose(file);
    return buffer;
}

// ========================================================
// SHADER PUSH CONSTANTS
// ========================================================
// This must perfectly match the layout(push_constant) in render.vert
typedef struct {
    float viewProj[16]; // 64 bytes (A standard 4x4 Matrix)
} CameraPushConstants;

int main() {
    lua_State* L = luaL_newstate(); g_L = L; luaL_openlibs(L);
    lua_newtable(L);
    lua_pushcfunction(L, l_isKeyDown); lua_setfield(L, -2, "isKeyDown");
    lua_pushcfunction(L, l_setRelativeMode); lua_setfield(L, -2, "setRelativeMode");
    lua_pushcfunction(L, l_getVRAM_X); lua_setfield(L, -2, "getVRAM_X");
    lua_pushcfunction(L, l_getVRAM_Y); lua_setfield(L, -2, "getVRAM_Y");
    lua_pushcfunction(L, l_getVRAM_Z); lua_setfield(L, -2, "getVRAM_Z");
    lua_pushcfunction(L, l_isMouseDown); lua_setfield(L, -2, "isMouseDown");
    lua_setglobal(L, "Engine");

    if (luaL_dofile(L, "main.lua") != LUA_OK) { printf("FATAL: %s\n", lua_tostring(L, -1)); return -1; }

    lua_getglobal(L, "Config");
    lua_getfield(L, -1, "window_title"); const char* lua_window_title = lua_tostring(L, -1); lua_pop(L, 2);

    glfwInit(); glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor(); const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
    g_window = glfwCreateWindow(mode->width, mode->height, lua_window_title, primaryMonitor, NULL);
    glfwSetCursorPosCallback(g_window, cursor_position_callback);

    VkApplicationInfo appInfo = {0}; appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; appInfo.apiVersion = VK_API_VERSION_1_3;
    uint32_t glfwExtCount = 0; const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    VkInstanceCreateInfo createInfo = {0}; createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; createInfo.pApplicationInfo = &appInfo; createInfo.enabledExtensionCount = glfwExtCount; createInfo.ppEnabledExtensionNames = glfwExts; createInfo.enabledLayerCount = 1; createInfo.ppEnabledLayerNames = validationLayers;
    VkInstance instance; vkCreateInstance(&createInfo, NULL, &instance);

    uint32_t deviceCount = 0; vkEnumeratePhysicalDevices(instance, &deviceCount, NULL);
    VkPhysicalDevice* devices = malloc(deviceCount * sizeof(VkPhysicalDevice)); vkEnumeratePhysicalDevices(instance, &deviceCount, devices);
    VkPhysicalDevice chosenGPU = devices[0]; free(devices);

    uint32_t queueFamilyCount = 0; vkGetPhysicalDeviceQueueFamilyProperties(chosenGPU, &queueFamilyCount, NULL);
    VkQueueFamilyProperties* queueFamilies = malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties)); vkGetPhysicalDeviceQueueFamilyProperties(chosenGPU, &queueFamilyCount, queueFamilies);
    uint32_t qIndex = 0; for (uint32_t i = 0; i < queueFamilyCount; i++) { if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { qIndex = i; break; } } free(queueFamilies);

    float queuePriority = 1.0f; VkDeviceQueueCreateInfo queueCreateInfo = {0}; queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; queueCreateInfo.queueFamilyIndex = qIndex; queueCreateInfo.queueCount = 1; queueCreateInfo.pQueuePriorities = &queuePriority;
    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering = {0}; dynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES; dynamicRendering.dynamicRendering = VK_TRUE;
    VkDeviceCreateInfo deviceCreateInfo = {0}; deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; deviceCreateInfo.pNext = &dynamicRendering; deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo; deviceCreateInfo.queueCreateInfoCount = 1; deviceCreateInfo.enabledExtensionCount = 1; deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;
    VkDevice device; vkCreateDevice(chosenGPU, &deviceCreateInfo, NULL, &device);
    VkQueue graphicsQueue; vkGetDeviceQueue(device, qIndex, 0, &graphicsQueue);

    // ========================================================
    // ALLOCATE 3 SEPARATE SOA BUFFERS
    // ========================================================
    uint32_t maxVerts = 4000000; // MUST Match memory.lua!
    VkDeviceSize bufSize = maxVerts * sizeof(float);
    VkBuffer bufX, bufY, bufZ;
    VkDeviceMemory memX, memY, memZ;
    createSoABuffer(device, chosenGPU, bufSize, &bufX, &memX, &g_ptrX);
    createSoABuffer(device, chosenGPU, bufSize, &bufY, &memY, &g_ptrY);
    createSoABuffer(device, chosenGPU, bufSize, &bufZ, &memZ, &g_ptrZ);

    // ========================================================
    // DESCRIPTORS (3 BINDINGS NOW!)
    // ========================================================
    VkDescriptorSetLayoutBinding bindings[3] = {{0}};
    for(int i=0; i<3; i++) {
        bindings[i].binding = i; bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; bindings[i].descriptorCount = 1; bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo = {0}; layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; layoutInfo.bindingCount = 3; layoutInfo.pBindings = bindings;
    VkDescriptorSetLayout descriptorSetLayout; vkCreateDescriptorSetLayout(device, &layoutInfo, NULL, &descriptorSetLayout);

    VkDescriptorPoolSize poolSize = {0}; poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; poolSize.descriptorCount = 3;
    VkDescriptorPoolCreateInfo poolInfo = {0}; poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; poolInfo.poolSizeCount = 1; poolInfo.pPoolSizes = &poolSize; poolInfo.maxSets = 1;
    VkDescriptorPool descriptorPool; vkCreateDescriptorPool(device, &poolInfo, NULL, &descriptorPool);

    VkDescriptorSetAllocateInfo allocSetInfo = {0}; allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; allocSetInfo.descriptorPool = descriptorPool; allocSetInfo.descriptorSetCount = 1; allocSetInfo.pSetLayouts = &descriptorSetLayout;
    VkDescriptorSet descriptorSet; vkAllocateDescriptorSets(device, &allocSetInfo, &descriptorSet);

    VkBuffer buffers[3] = {bufX, bufY, bufZ};
    VkDescriptorBufferInfo dBufferInfos[3] = {{0}};
    VkWriteDescriptorSet descriptorWrites[3] = {{0}};
    for(int i=0; i<3; i++) {
        dBufferInfos[i].buffer = buffers[i]; dBufferInfos[i].offset = 0; dBufferInfos[i].range = VK_WHOLE_SIZE;
        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; descriptorWrites[i].dstSet = descriptorSet; descriptorWrites[i].dstBinding = i; descriptorWrites[i].dstArrayElement = 0; descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; descriptorWrites[i].descriptorCount = 1; descriptorWrites[i].pBufferInfo = &dBufferInfos[i];
    }
    vkUpdateDescriptorSets(device, 3, descriptorWrites, 0, NULL);

    VkSurfaceKHR surface; glfwCreateWindowSurface(instance, g_window, NULL, &surface);
    VkSurfaceCapabilitiesKHR surfaceCaps; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(chosenGPU, surface, &surfaceCaps);
    VkExtent2D swapchainExtent = surfaceCaps.currentExtent;
    VkSwapchainCreateInfoKHR swapchainInfo = {0}; swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR; swapchainInfo.surface = surface; swapchainInfo.minImageCount = surfaceCaps.minImageCount + 1; swapchainInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB; swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; swapchainInfo.imageExtent = swapchainExtent; swapchainInfo.imageArrayLayers = 1; swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; swapchainInfo.preTransform = surfaceCaps.currentTransform; swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; swapchainInfo.clipped = VK_TRUE;
    VkSwapchainKHR swapchain; vkCreateSwapchainKHR(device, &swapchainInfo, NULL, &swapchain);
    uint32_t imageCount; vkGetSwapchainImagesKHR(device, swapchain, &imageCount, NULL);
    VkImage* swapchainImages = malloc(imageCount * sizeof(VkImage)); vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages);
    VkImageView* swapchainImageViews = malloc(imageCount * sizeof(VkImageView));
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo = {0}; viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; viewInfo.image = swapchainImages[i]; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = VK_FORMAT_B8G8R8A8_SRGB; viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; viewInfo.subresourceRange.levelCount = 1; viewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &viewInfo, NULL, &swapchainImageViews[i]);
    }
    // ========================================================
    // CREATE DEPTH BUFFER (For 3D Tetrahedrons)
    // ========================================================
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT; // Standard 32-bit float depth
    VkImage depthImage;
    VkDeviceMemory depthMemory;
    VkImageView depthImageView;

    VkImageCreateInfo dImgInfo = {0};
    dImgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    dImgInfo.imageType = VK_IMAGE_TYPE_2D;
    dImgInfo.extent.width = swapchainExtent.width;
    dImgInfo.extent.height = swapchainExtent.height;
    dImgInfo.extent.depth = 1;
    dImgInfo.mipLevels = 1;
    dImgInfo.arrayLayers = 1;
    dImgInfo.format = depthFormat;
    dImgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    dImgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    dImgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    dImgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    vkCreateImage(device, &dImgInfo, NULL, &depthImage);

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, depthImage, &memReqs);
    // Note: In a real scenario, you'd search for VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    // Assuming you have a helper for this, or just hardcode the memory type index for now.
    VkMemoryAllocateInfo dAllocInfo = {0};
    dAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    dAllocInfo.allocationSize = memReqs.size;

    // Quick inline memory type search for DEVICE_LOCAL (VRAM)
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(chosenGPU, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memReqs.memoryTypeBits & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            dAllocInfo.memoryTypeIndex = i; break;
        }
    }
    vkAllocateMemory(device, &dAllocInfo, NULL, &depthMemory);
    vkBindImageMemory(device, depthImage, depthMemory, 0);

    VkImageViewCreateInfo dViewInfo = {0};
    dViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    dViewInfo.image = depthImage;
    dViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    dViewInfo.format = depthFormat;
    dViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    dViewInfo.subresourceRange.levelCount = 1;
    dViewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(device, &dViewInfo, NULL, &depthImageView);
    size_t vertSize, fragSize; char* vertCode = readShaderFile("render_vert.spv", &vertSize); char* fragCode = readShaderFile("render_frag.spv", &fragSize);
    VkShaderModuleCreateInfo vertInfo = {0}; vertInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; vertInfo.codeSize = vertSize; vertInfo.pCode = (uint32_t*)vertCode;
    VkShaderModuleCreateInfo fragInfo = {0}; fragInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; fragInfo.codeSize = fragSize; fragInfo.pCode = (uint32_t*)fragCode;
    VkShaderModule vertModule, fragModule; vkCreateShaderModule(device, &vertInfo, NULL, &vertModule); vkCreateShaderModule(device, &fragInfo, NULL, &fragModule); free(vertCode); free(fragCode);

// ========================================================
    // 1. SHADER STAGES
    // ========================================================
    VkPipelineShaderStageCreateInfo shaderStages[2] = {{0}};
    shaderStages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; 
    shaderStages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT; 
    shaderStages[0].module = vertModule; 
    shaderStages[0].pName  = "main";

    shaderStages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; 
    shaderStages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT; 
    shaderStages[1].module = fragModule; 
    shaderStages[1].pName  = "main";

    // ========================================================
    // 2. FIXED FUNCTION STATE (The "Dumb" Hardware Configuration)
    // ========================================================
    // Empty Vertex Input - Our SoA SSBOs handle this natively!
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {0}; 
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    
    // Topology: Now set to Triangles!
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {0}; 
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; 
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState = {0}; 
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; 
    viewportState.viewportCount = 1; 
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = {0}; 
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; 
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL; 
    rasterizer.lineWidth   = 1.0f; 
    rasterizer.cullMode    = VK_CULL_MODE_NONE; // Culling happens automatically if we want, but let's keep it off while debugging 3D

    VkPipelineMultisampleStateCreateInfo multisampling = {0}; 
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; 
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // --- NEW: DEPTH STENCIL STATE ---
    // This tells the silicon to actively test and write to our new Z-Buffer
    VkPipelineDepthStencilStateCreateInfo depthStencil = {0};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;  // Check if new pixel is closer than old pixel
    depthStencil.depthWriteEnable = VK_TRUE;  // If closer, write the new depth to the buffer
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS; // "Less" means closer to the camera
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable     = VK_FALSE;

    // ========================================================
    // 3. COLOR BLENDING
    // ========================================================
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {0}; 
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; 
    colorBlendAttachment.blendEnable         = VK_TRUE; 
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; 
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE; 
    colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD; 
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; 
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; 
    colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending = {0}; 
    colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; 
    colorBlending.attachmentCount = 1; 
    colorBlending.pAttachments    = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }; 
    VkPipelineDynamicStateCreateInfo dynamicStateInfo = {0}; 
    dynamicStateInfo.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; 
    dynamicStateInfo.dynamicStateCount = 2; 
    dynamicStateInfo.pDynamicStates    = dynamicStates;

    // ========================================================
    // 4. PIPELINE LAYOUT & PUSH CONSTANTS (The Bridge)
    // ========================================================
    // UPDATED: Now sized for our 64-byte Camera Matrix instead of just screen resolution
    VkPushConstantRange gfxPushRange = {0}; 
    gfxPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; 
    gfxPushRange.offset     = 0; 
    gfxPushRange.size       = sizeof(CameraPushConstants); // <--- CHANGED!

    VkPipelineLayoutCreateInfo gfxLayoutInfo = {0}; 
    gfxLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; 
    gfxLayoutInfo.setLayoutCount         = 1; 
    gfxLayoutInfo.pSetLayouts            = &descriptorSetLayout; // Binds your 3 SSBOs
    gfxLayoutInfo.pushConstantRangeCount = 1; 
    gfxLayoutInfo.pPushConstantRanges    = &gfxPushRange;
    
    VkPipelineLayout graphicsPipelineLayout; 
    vkCreatePipelineLayout(device, &gfxLayoutInfo, NULL, &graphicsPipelineLayout);

    VkPipelineRenderingCreateInfo renderingCreateInfo = {0}; renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO; renderingCreateInfo.colorAttachmentCount = 1; renderingCreateInfo.pColorAttachmentFormats = &swapchainInfo.imageFormat;
    VkGraphicsPipelineCreateInfo gfxPipelineInfo = {0}; gfxPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; gfxPipelineInfo.pNext = &renderingCreateInfo; gfxPipelineInfo.stageCount = 2; gfxPipelineInfo.pStages = shaderStages; gfxPipelineInfo.pVertexInputState = &vertexInputInfo; gfxPipelineInfo.pInputAssemblyState = &inputAssembly; gfxPipelineInfo.pViewportState = &viewportState; gfxPipelineInfo.pRasterizationState = &rasterizer; gfxPipelineInfo.pMultisampleState = &multisampling; gfxPipelineInfo.pColorBlendState = &colorBlending; gfxPipelineInfo.pDynamicState = &dynamicStateInfo; gfxPipelineInfo.layout = graphicsPipelineLayout;
    VkPipeline graphicsPipeline; vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gfxPipelineInfo, NULL, &graphicsPipeline);

    VkCommandPoolCreateInfo cmdPoolInfo = {0}; cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; cmdPoolInfo.queueFamilyIndex = qIndex; cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool commandPool; vkCreateCommandPool(device, &cmdPoolInfo, NULL, &commandPool);
    VkCommandBufferAllocateInfo cmdAllocInfo = {0}; cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; cmdAllocInfo.commandPool = commandPool; cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer; vkAllocateCommandBuffers(device, &cmdAllocInfo, &commandBuffer);

    VkSemaphore imageAvailableSemaphore; VkSemaphoreCreateInfo semaInfo = {0}; semaInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO; vkCreateSemaphore(device, &semaInfo, NULL, &imageAvailableSemaphore);

    lua_getglobal(L, "love_load");
    if (lua_isfunction(L, -1)) { if (lua_pcall(L, 0, 0, 0) != LUA_OK) { printf("[LUA ERROR] %s\n", lua_tostring(L, -1)); } } lua_pop(L, 1);

    while (!glfwWindowShouldClose(g_window)) {
        glfwPollEvents();
        if (glfwGetKey(g_window, GLFW_KEY_ESCAPE) == GLFW_PRESS) { glfwSetWindowShouldClose(g_window, GLFW_TRUE); }

        lua_getglobal(L, "love_update");
        if (lua_isfunction(L, -1)) { lua_pushnumber(L, 0.016); if (lua_pcall(L, 1, 0, 0) != LUA_OK) { printf("[LUA ERROR] %s\n", lua_tostring(L, -1)); } } else { lua_pop(L, 1); }

        // --- READ DRAW COUNT FROM LUA ---
        int draw_count = 0;
        lua_getglobal(L, "DrawCount");
        if (lua_isnumber(L, -1)) { draw_count = lua_tointeger(L, -1); }
        lua_pop(L, 1);

        // ========================================================
        // 1. FRAME ACQUISITION & COMMAND BUFFER SETUP
        // ========================================================
        // Ask the swapchain for the next available image to draw into
        uint32_t imageIndex;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

        vkResetCommandBuffer(commandBuffer, 0);

        VkCommandBufferBeginInfo beginInfo = {0};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        // ========================================================
        // 2. IMAGE LAYOUT TRANSITION (The Pipeline Barrier)
        // ========================================================
        // GPUs expect images to be in specific memory layouts for specific tasks.
        // Here, we transition the swapchain image from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL.
        VkImageMemoryBarrier imgBarrier = {0};
        imgBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imgBarrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        imgBarrier.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imgBarrier.image               = swapchainImages[imageIndex];

        imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imgBarrier.subresourceRange.levelCount = 1;
        imgBarrier.subresourceRange.layerCount = 1;

        imgBarrier.srcAccessMask = 0;
        imgBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // Execute the barrier. The pipeline will stall at the output stage until the transition is done.
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, NULL, 0, NULL, 1, &imgBarrier
        );

        // ========================================================
        // 3. DYNAMIC RENDERING SETUP (Color + Depth)
        // ========================================================
        // Tell Vulkan WHERE to draw the colors, and what background color to clear with
        VkRenderingAttachmentInfo colorAttachment = {0};
        colorAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.imageView   = swapchainImageViews[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;  // Clear screen at start of render
        colorAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE; // Keep the pixels for presentation

        // VibeEngine Dark Purple/Blue background
        colorAttachment.clearValue.color.float32[0] = 0.02f; // R
        colorAttachment.clearValue.color.float32[1] = 0.02f; // G
        colorAttachment.clearValue.color.float32[2] = 0.05f; // B
        colorAttachment.clearValue.color.float32[3] = 1.0f;  // A

        // --- NEW: DEPTH ATTACHMENT ---
        // Tell Vulkan WHERE to do Z-buffer testing
        VkRenderingAttachmentInfo depthAttachment = {0};
        depthAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView   = depthImageView; // Created during setup
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear the depth buffer to 1.0 (furthest)
        depthAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE; // We don't read depth after rendering, so let the GPU discard it to save bandwidth!
        depthAttachment.clearValue.depthStencil.depth = 1.0f;

        // Bind the attachments to the Rendering Info
        VkRenderingInfo renderInfo = {0};
        renderInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderInfo.renderArea.extent    = swapchainExtent;
        renderInfo.layerCount           = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments    = &colorAttachment;
        renderInfo.pDepthAttachment     = &depthAttachment; // <-- Linked here!

        vkCmdBeginRendering(commandBuffer, &renderInfo);

        // ========================================================
        // 4. VIEWPORT, SCISSOR & BINDINGS
        // ========================================================
        VkViewport viewport = {0};
        viewport.x        = 0.0f;
        viewport.y        = 0.0f;
        viewport.width    = (float)swapchainExtent.width;
        viewport.height   = (float)swapchainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor = {0};
        scissor.offset = (VkOffset2D){0, 0};
        scissor.extent = swapchainExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        // Bind the Pipeline (The shaders and hardware state)
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        // Bind the Descriptor Sets (Your 3 SoA SSBOs for X, Y, Z)
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipelineLayout, 0, 1, &descriptorSet, 0, NULL);

        // ========================================================
        // 5. PUSH CONSTANTS & THE INSTANCED DRAW CALL
        // ========================================================
        // Push the 64-byte Camera Matrix to the Vertex Shader
        // (Assuming you filled 'cam_pc' from Lua/C math right before this)
        // vkCmdPushConstants(
            // commandBuffer,
            // graphicsPipelineLayout,
            // VK_SHADER_STAGE_VERTEX_BIT,
            // 0,
            // sizeof(CameraPushConstants),
            // &cam_pc
        // );
        // ========================================================
        // 5. PUSH CONSTANTS & THE INSTANCED DRAW CALL
        // ========================================================
        
        // Instantiate the struct
        CameraPushConstants cam_pc = {0};
        
        // Temporary Identity Matrix (Scale 1, No Rotation, No Translation)
        // You will eventually overwrite this with the matrix from your AVX2/Lua camera!
        cam_pc.viewProj[0]  = 1.0f;
        cam_pc.viewProj[5]  = 1.0f;
        cam_pc.viewProj[10] = 1.0f;
        cam_pc.viewProj[15] = 1.0f;

        // Push the 64-byte Camera Matrix to the Vertex Shader
        vkCmdPushConstants(
            commandBuffer, 
            graphicsPipelineLayout, 
            VK_SHADER_STAGE_VERTEX_BIT, 
            0, 
            sizeof(CameraPushConstants), 
            &cam_pc
        );

        if (draw_count > 0) {
            // PARAMETERS: (commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance)
            // 12 vertices = 4 triangles (a complete tetrahedron)
            // draw_count = number of instances (particles)
            vkCmdDraw(commandBuffer, 12, draw_count, 0, 0);
        }

        vkCmdEndRendering(commandBuffer);

        imgBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; imgBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; imgBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; imgBarrier.dstAccessMask = 0;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &imgBarrier);
        vkEndCommandBuffer(commandBuffer);

        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSubmitInfo submitInfo = {0}; submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; submitInfo.waitSemaphoreCount = 1; submitInfo.pWaitSemaphores = &imageAvailableSemaphore; submitInfo.pWaitDstStageMask = waitStages; submitInfo.commandBufferCount = 1; submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);

        VkPresentInfoKHR presentInfo = {0}; presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR; presentInfo.swapchainCount = 1; presentInfo.pSwapchains = &swapchain; presentInfo.pImageIndices = &imageIndex;
        vkQueuePresentKHR(graphicsQueue, &presentInfo);
        vkQueueWaitIdle(graphicsQueue);
    }
    vkDeviceWaitIdle(device);

    // 1. Sync Objects
    vkDestroySemaphore(device, imageAvailableSemaphore, NULL);

    // 2. Pipelines & Layouts
    vkDestroyPipeline(device, graphicsPipeline, NULL);
    vkDestroyPipelineLayout(device, graphicsPipelineLayout, NULL);

    // 3. Shaders (Note: You can actually do this right after vkCreateGraphicsPipelines!)
    vkDestroyShaderModule(device, vertModule, NULL);
    vkDestroyShaderModule(device, fragModule, NULL);

    // 4. Descriptors
    vkDestroyDescriptorPool(device, descriptorPool, NULL);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, NULL);

    // 5. Command Pool (This automatically frees the command buffers inside it)
    vkDestroyCommandPool(device, commandPool, NULL);

    // ========================================================
    // 6. DESTROY DEPTH BUFFER (Before Swapchain!)
    // ========================================================
    vkDestroyImageView(device, depthImageView, NULL);
    vkDestroyImage(device, depthImage, NULL);
    vkFreeMemory(device, depthMemory, NULL);

    // 7. Swapchain & Views
    for (uint32_t i = 0; i < imageCount; i++) {
        vkDestroyImageView(device, swapchainImageViews[i], NULL);
    }
    free(swapchainImageViews);
    free(swapchainImages);
    vkDestroySwapchainKHR(device, swapchain, NULL);

    // 8. FREE 3 VRAM BUFFERS (Your SoA Pipeline)
    vkUnmapMemory(device, memX); vkDestroyBuffer(device, bufX, NULL); vkFreeMemory(device, memX, NULL);
    vkUnmapMemory(device, memY); vkDestroyBuffer(device, bufY, NULL); vkFreeMemory(device, memY, NULL);
    vkUnmapMemory(device, memZ); vkDestroyBuffer(device, bufZ, NULL); vkFreeMemory(device, memZ, NULL);

    // 9. Core Vulkan Context
    vkDestroyDevice(device, NULL);
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);

    // 10. OS & Scripting
    glfwDestroyWindow(g_window);
    glfwTerminate();
    lua_close(L);

    return 0;
}
