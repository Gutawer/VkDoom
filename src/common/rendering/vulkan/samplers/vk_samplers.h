
#pragma once

#include <array>
#include <memory>

#include "zvulkan/vulkanobjects.h"
#include "gametexture.h"
#include "textures.h"

class VulkanRenderDevice;
enum class PPFilterMode;
enum class PPWrapMode;

class VkSamplerManager
{
public:
	VkSamplerManager(VulkanRenderDevice* fb);
	~VkSamplerManager();

	void ResetHWSamplers();

	VulkanSampler* Get(PPFilterMode filter, PPWrapMode wrap);

	inline VulkanSampler* Get(int no) const { return mSamplers[no].get(); }
	inline VulkanSampler* Get(MaterialLayerSampling filterOverride, int no) const
	{
		if (filterOverride == MaterialLayerSampling::Default)
			return Get(no);
		return mOverrideSamplers[int(filterOverride)].get();
	}

	std::unique_ptr<VulkanSampler> ShadowmapSampler;
	std::unique_ptr<VulkanSampler> LightmapSampler;

private:
	void CreateHWSamplers();
	void DeleteHWSamplers();
	void CreateShadowmapSampler();
	void CreateLightmapSampler();

	VulkanRenderDevice* fb = nullptr;
	std::array<std::unique_ptr<VulkanSampler>, NUMSAMPLERS> mSamplers;
	std::array<std::unique_ptr<VulkanSampler>, 2> mOverrideSamplers;
	std::array<std::unique_ptr<VulkanSampler>, 4> mPPSamplers;
};
