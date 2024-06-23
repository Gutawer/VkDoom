#pragma once
#pragma once

#include <memory>

#include "r_defs.h"
#include "m_fixed.h"
#include "hwrenderer/scene/hw_clipper.h"
#include "r_utility.h"
#include "c_cvars.h"

class FWrapperTexture;
class DCanvas;
class FGameTexture;
class FTexture;
class player_t;
struct sector_t;

class SWSceneDrawer
{
	FTexture *PaletteTexture;
	std::unique_ptr<FGameTexture> FBTexture[2];
	int FBTextureIndex = 0;
	bool FBIsTruecolor = false;
	std::unique_ptr<DCanvas> Canvas;

public:
	SWSceneDrawer();
	~SWSceneDrawer();

	sector_t *RenderView(player_t *player);
};

