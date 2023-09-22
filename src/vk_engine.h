// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vector>
#include <deque>
#include <functional>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

struct DeletionQueue {
	std::deque<std::function<void()>> deletors;

	void push_func(std::function<void()>&& func) {
		deletors.push_back(func);
	}

	void flush() {
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
			(*it)();
		deletors.clear();
	}
};

class VulkanEngine {
public:

	bool _isInitialized{ false };
	int _frameNumber {0};

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkPhysicalDevice _chosenGPU;
	VkDevice _device;
	VkSurfaceKHR _surface;

	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;

	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	VkRenderPass _renderPass;
	std::vector<VkFramebuffer> _framebuffers;

	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;

	VkPipelineLayout _trianglePipelineLayout;
	VkPipeline _trianglePipeline;
	VkPipeline _redTrianglePipeline;

	DeletionQueue _deletionQueue;

	VmaAllocator _allocator;

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

	bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);

private:
	void init_vulkan();
	void init_swapchain();
	void init_commands();

	void init_default_renderpass();
	void init_framebuffers();

	void init_sync_structures();

	void init_pipelines();

};
