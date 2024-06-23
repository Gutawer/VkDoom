
#pragma once

#include <zvulkan/vulkanobjects.h>
#include <stdint.h>

#include "hwrenderer/postprocessing/hw_postprocess.h"
#include "zstring.h"

class VkPPRenderPassSetup;
class VkPPShader;
class VkPPTexture;
class VkTextureImage;
class VulkanRenderDevice;
class VulkanDescriptorSet;
class VulkanFramebuffer;

class VkPPRenderState : public PPRenderState
{
public:
	VkPPRenderState(VulkanRenderDevice* fb);

	void PushGroup(const FString &name) override;
	void PopGroup() override;

	void Draw() override;

private:
	void RenderScreenQuad(VkPPRenderPassSetup *passSetup, VulkanDescriptorSet *descriptorSet, VulkanFramebuffer *framebuffer, int framebufferWidth, int framebufferHeight, int x, int y, int width, int height, const void *pushConstants, uint32_t pushConstantsSize, bool stencilTest);

	VulkanRenderDevice* fb = nullptr;
};
