
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>

#include "VkBootstrap.h"

#include <iostream>
#include <fstream>

//we want to immediately abort when there is an error. In normal engines this would give an error message to the user, or perform a dump of state.
using namespace std;
#define VK_CHECK(x)                                                 \
	do                                                              \
	{                                                               \
		VkResult err = x;                                           \
		if (err)                                                    \
		{                                                           \
			std::cout <<"Detected Vulkan error: " << err << std::endl; \
			abort();                                                \
		}                                                           \
	} while (0)

int _selectedShader{ 0 };

void VulkanEngine::init()
{
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

	_window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);

	init_vulkan();
	init_swapchain();
	init_commands();

	init_default_renderpass();
	init_framebuffers();

	init_sync_structures();

	init_pipelines();

	//everything went fine
	_isInitialized = true;
}

void VulkanEngine::init_vulkan()
{
	vkb::InstanceBuilder builder;
	auto inst_ret = builder.set_app_name("Example Vulkan App")
		.request_validation_layers(true)
		.require_api_version(1, 1, 0)
		.use_default_debug_messenger()
		.build();

	vkb::Instance vkb_inst = inst_ret.value();

	_instance = vkb_inst.instance;
	_debug_messenger = vkb_inst.debug_messenger;

	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector.set_minimum_version(1, 1)
		.set_surface(_surface)
		.select()
		.value();

	_chosenGPU = physicalDevice.physical_device;

	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	vkb::Device vkbDevice = deviceBuilder.build().value();

	_device = vkbDevice.device;

	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
}

void VulkanEngine::init_swapchain() {

	vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU, _device, _surface };
	vkb::Swapchain vkbSwapchain = swapchainBuilder.use_default_format_selection()
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.build()
		.value();

	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();

	_swapchainImageFormat = vkbSwapchain.image_format;

}

void VulkanEngine::init_commands() {

	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_commandPool));


	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_commandPool);
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_mainCommandBuffer));
}

void VulkanEngine::init_default_renderpass() {
	VkAttachmentDescription color_attachment = {};
	color_attachment.format = _swapchainImageFormat;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attach_ref = {};
	color_attach_ref.attachment = 0;
	color_attach_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attach_ref;

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.pNext = nullptr;

	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;

	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;

	VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_renderPass));
}
void VulkanEngine::init_framebuffers() {
	VkFramebufferCreateInfo fb_info = {};
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.pNext = nullptr;

	fb_info.renderPass = _renderPass;
	fb_info.attachmentCount = 1;
	fb_info.width = _windowExtent.width;
	fb_info.height = _windowExtent.height;
	fb_info.layers = 1;

	//grab how many images we have in the swapchain
	const uint32_t swapchain_imagecount = _swapchainImages.size();
	_framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

	//create framebuffers for each of the swapchain image views
	for (int i = 0; i < swapchain_imagecount; i++) {

		fb_info.pAttachments = &_swapchainImageViews[i];
		VK_CHECK(vkCreateFramebuffer(_device, &fb_info, nullptr, &_framebuffers[i]));
	}
}

void VulkanEngine::init_sync_structures() {
	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;

	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_renderFence));

	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;
	semaphoreCreateInfo.flags = 0;

	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_presentSemaphore));
	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_renderSemaphore));

}


void VulkanEngine::init_pipelines() {
	VkShaderModule redTriangleFragShader;
	if (!load_shader_module("../../shaders/triangle.frag.glsl.spv", &redTriangleFragShader))
	{
		std::cout << "Error when building the triangle fragment shader module" << std::endl;
	}
	else {
		std::cout << "Triangle fragment shader successfully loaded" << std::endl;
	}

	VkShaderModule redTriangleVertexShader;
	if (!load_shader_module("../../shaders/triangle.vert.glsl.spv", &redTriangleVertexShader))
	{
		std::cout << "Error when building the triangle vertex shader module" << std::endl;

	}
	else {
		std::cout << "Triangle vertex shader successfully loaded" << std::endl;
	}

	VkShaderModule triangleFragShader;
	if (!load_shader_module("../../shaders/triangle-colored.frag.glsl.spv", &triangleFragShader))
	{
		std::cout << "Error when building the triangle fragment shader module" << std::endl;
	}
	else {
		std::cout << "Triangle fragment shader successfully loaded" << std::endl;
	}

	VkShaderModule triangleVertexShader;
	if (!load_shader_module("../../shaders/triangle-colored.vert.glsl.spv", &triangleVertexShader))
	{
		std::cout << "Error when building the triangle vertex shader module" << std::endl;

	}
	else {
		std::cout << "Triangle vertex shader successfully loaded" << std::endl;
	}


	VkPipelineLayoutCreateInfo pipeline_create_info = vkinit::pipeline_layout_create_info();
	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_create_info, nullptr, &_trianglePipelineLayout));

	PipelineBuilder pipelineBuilder;
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, triangleVertexShader)
	);

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader)
	);

	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	pipelineBuilder._viewport.x = 0.f;
	pipelineBuilder._viewport.y = 0.f;
	pipelineBuilder._viewport.width = (float)_windowExtent.width;
	pipelineBuilder._viewport.height = (float)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.f;
	pipelineBuilder._viewport.maxDepth = 1.f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();

	pipelineBuilder._pipelineLayout = _trianglePipelineLayout;

	_trianglePipeline = pipelineBuilder.build_pipeline(_device, _renderPass);


	pipelineBuilder._shaderStages.clear();

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, redTriangleVertexShader)
	);

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, redTriangleFragShader)
	);
	_redTrianglePipeline = pipelineBuilder.build_pipeline(_device, _renderPass);
}

void VulkanEngine::cleanup()
{
	if (_isInitialized) {

		vkDestroySwapchainKHR(_device, _swapchain, nullptr);

		vkDestroyRenderPass(_device, _renderPass, nullptr);
		vkDestroyCommandPool(_device, _commandPool, nullptr);


		for (int i = 0; i < _swapchainImageViews.size(); i++)
		{
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);

		}
		vkDestroyDevice(_device, nullptr);
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::draw()
{
	//wait until the GPU has finished rendering the last frame. Timeout of 1 second
	VK_CHECK(vkWaitForFences(_device, 1, &_renderFence, true, 1000000000));
	VK_CHECK(vkResetFences(_device, 1, &_renderFence));

	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, _presentSemaphore, nullptr, &swapchainImageIndex));

	VkCommandBuffer cmd = _mainCommandBuffer;

	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;

	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.f));
	clearValue.color = { {0.f, 0.f, flash, 1.f} };

	VkRenderPassBeginInfo rpInfo = {};
	rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpInfo.pNext = nullptr;

	rpInfo.renderPass = _renderPass;
	rpInfo.renderArea.offset.x = 0;
	rpInfo.renderArea.offset.y = 0;
	rpInfo.renderArea.extent = _windowExtent;
	rpInfo.framebuffer = _framebuffers[swapchainImageIndex];

	rpInfo.clearValueCount = 1;
	rpInfo.pClearValues = &clearValue;
	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _selectedShader == 0 ? _trianglePipeline : _redTrianglePipeline);

	vkCmdDraw(cmd, 3, 1.0, 0, 0);

	vkCmdEndRenderPass(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	submit.pWaitDstStageMask = &waitStage;

	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &_presentSemaphore;

	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &_renderSemaphore;

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	//submit command buffer to the queue and execute it.
	// _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _renderFence));

	// this will put the image we just rendered into the visible window.
// we want to wait on the _renderSemaphore for that,
// as it's necessary that drawing commands have finished before the image is displayed to the user
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &_renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	//increase the number of frames drawn
	_frameNumber++;

	//https://vkguide.dev/docs/chapter_2

}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;

	//main loop
	while (!bQuit)
	{
		//Handle events on queue
		while (SDL_PollEvent(&e) != 0)
		{
			if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
				bQuit = true;

			if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_SPACE)
			{
				_selectedShader = (_selectedShader + 1) % 2;

				std::cout << _selectedShader << std::endl;
			}

			//close the window when user alt-f4s or clicks the X button			
			if (e.type == SDL_QUIT) bQuit = true;


		}

		draw();
	}
}

bool VulkanEngine::load_shader_module(const char* filePath, VkShaderModule* outShaderModule) {
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open()) return false;

	size_t fileSize = (size_t)file.tellg();
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	file.seekg(0);
	file.read((char*)buffer.data(), fileSize);
	file.close();

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
		return false;

	*outShaderModule = shaderModule;

	return true;
}
