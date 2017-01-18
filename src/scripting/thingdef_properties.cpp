/*
** thingdef-properties.cpp
**
** Actor denitions - properties and flags handling
**
**---------------------------------------------------------------------------
** Copyright 2002-2007 Christoph Oelckers
** Copyright 2004-2007 Randy Heit
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. When not used as part of ZDoom or a ZDoom derivative, this code will be
**    covered by the terms of the GNU General Public License as published by
**    the Free Software Foundation; either version 2 of the License, or (at
**    your option) any later version.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "gi.h"
#include "d_player.h"
#include "info.h"
#include "tarray.h"
#include "w_wad.h"
#include "templates.h"
#include "r_defs.h"
#include "a_pickups.h"
#include "a_armor.h"
#include "s_sound.h"
#include "cmdlib.h"
#include "p_lnspec.h"
#include "decallib.h"
#include "m_random.h"
#include "i_system.h"
#include "p_local.h"
#include "p_effect.h"
#include "v_palette.h"
#include "doomerrors.h"
#include "a_artifacts.h"
#include "p_conversation.h"
#include "v_text.h"
#include "thingdef.h"
#include "a_sharedglobal.h"
#include "r_data/r_translate.h"
#include "a_morph.h"
#include "colormatcher.h"
#include "teaminfo.h"
#include "v_video.h"
#include "r_data/colormaps.h"
#include "a_weaponpiece.h"
#include "vmbuilder.h"
#include "a_ammo.h"
#include "a_keys.h"
#include "g_levellocals.h"

//==========================================================================
//
// Gets a class pointer and performs an error check for correct type
//
//==========================================================================
static PClassActor *FindClassTentative(const char *name, PClass *ancestor, bool optional = false)
{
	// "" and "none" mean 'no class'
	if (name == NULL || *name == 0 || !stricmp(name, "none"))
	{
		return NULL;
	}

	PClass *cls = ancestor->FindClassTentative(name);
	assert(cls != NULL);	// cls can not be NULL here
	if (!cls->IsDescendantOf(ancestor))
	{
		I_Error("%s does not inherit from %s\n", name, ancestor->TypeName.GetChars());
	}
	if (cls->Size == TentativeClass && optional)
	{
		cls->ObjectFlags |= OF_Transient;	// since this flag has no meaning in class types, let's use it for marking the type optional.
	}
	return static_cast<PClassActor *>(cls);
}
static AAmmo::MetaClass *FindClassTentativeAmmo(const char *name, bool optional = false)
{
	return static_cast<AAmmo::MetaClass *>(FindClassTentative(name, RUNTIME_CLASS(AAmmo), optional));
}
static AWeapon::MetaClass *FindClassTentativeWeapon(const char *name, bool optional = false)
{
	return static_cast<AWeapon::MetaClass *>(FindClassTentative(name, RUNTIME_CLASS(AWeapon), optional));
}
static APlayerPawn::MetaClass *FindClassTentativePlayerPawn(const char *name, bool optional = false)
{
	return static_cast<APlayerPawn::MetaClass *>(FindClassTentative(name, RUNTIME_CLASS(APlayerPawn), optional));
}

//==========================================================================
//
// Sets or clears a flag, taking field width into account.
//
//==========================================================================
void ModActorFlag(AActor *actor, FFlagDef *fd, bool set)
{
	// Little-Endian machines only need one case, because all field sizes
	// start at the same address. (Unless the machine has unaligned access
	// exceptions, in which case you'll need multiple cases for it too.)
#ifdef __BIG_ENDIAN__
	if (fd->fieldsize == 4)
#endif
	{
		DWORD *flagvar = (DWORD *)((char *)actor + fd->structoffset);
		if (set)
		{
			*flagvar |= fd->flagbit;
		}
		else
		{
			*flagvar &= ~fd->flagbit;
		}
	}
#ifdef __BIG_ENDIAN__
	else if (fd->fieldsize == 2)
	{
		WORD *flagvar = (WORD *)((char *)actor + fd->structoffset);
		if (set)
		{
			*flagvar |= fd->flagbit;
		}
		else
		{
			*flagvar &= ~fd->flagbit;
		}
	}
	else
	{
		assert(fd->fieldsize == 1);
		BYTE *flagvar = (BYTE *)((char *)actor + fd->structoffset);
		if (set)
		{
			*flagvar |= fd->flagbit;
		}
		else
		{
			*flagvar &= ~fd->flagbit;
		}
	}
#endif
}

//==========================================================================
//
// Finds a flag by name and sets or clears it
//
// Returns true if the flag was found for the actor; else returns false
//
//==========================================================================

bool ModActorFlag(AActor *actor, FString &flagname, bool set, bool printerror)
{
	bool found = false;

	if (actor != NULL)
	{
		const char *dot = strchr(flagname, '.');
		FFlagDef *fd;
		PClassActor *cls = actor->GetClass();

		if (dot != NULL)
		{
			FString part1(flagname.GetChars(), dot - flagname);
			fd = FindFlag(cls, part1, dot + 1);
		}
		else
		{
			fd = FindFlag(cls, flagname, NULL);
		}

		if (fd != NULL)
		{
			found = true;

			if (actor->CountsAsKill() && actor->health > 0) --level.total_monsters;
			if (actor->flags & MF_COUNTITEM) --level.total_items;
			if (actor->flags5 & MF5_COUNTSECRET) --level.total_secrets;

			if (fd->structoffset == -1)
			{
				HandleDeprecatedFlags(actor, cls, set, fd->flagbit);
			}
			else
			{
				ActorFlags *flagp = (ActorFlags*)(((char*)actor) + fd->structoffset);

				// If these 2 flags get changed we need to update the blockmap and sector links.
				bool linkchange = flagp == &actor->flags && (fd->flagbit == MF_NOBLOCKMAP || fd->flagbit == MF_NOSECTOR);

				FLinkContext ctx;
				if (linkchange) actor->UnlinkFromWorld(&ctx);
				ModActorFlag(actor, fd, set);
				if (linkchange) actor->LinkToWorld(&ctx);
			}

			if (actor->CountsAsKill() && actor->health > 0) ++level.total_monsters;
			if (actor->flags & MF_COUNTITEM) ++level.total_items;
			if (actor->flags5 & MF5_COUNTSECRET) ++level.total_secrets;
		}
		else if (printerror)
		{
			DPrintf(DMSG_ERROR, "ACS/DECORATE: '%s' is not a flag in '%s'\n", flagname.GetChars(), cls->TypeName.GetChars());
		}
	}

	return found;
}

//==========================================================================
//
// Returns whether an actor flag is true or not.
//
//==========================================================================

INTBOOL CheckActorFlag(const AActor *owner, FFlagDef *fd)
{
	if (fd->structoffset == -1)
	{
		return CheckDeprecatedFlags(owner, owner->GetClass(), fd->flagbit);
	}
	else
#ifdef __BIG_ENDIAN__
	if (fd->fieldsize == 4)
#endif
	{
		return fd->flagbit & *(DWORD *)(((char*)owner) + fd->structoffset);
	}
#ifdef __BIG_ENDIAN__
	else if (fd->fieldsize == 2)
	{
		return fd->flagbit & *(WORD *)(((char*)owner) + fd->structoffset);
	}
	else
	{
		assert(fd->fieldsize == 1);
		return fd->flagbit & *(BYTE *)(((char*)owner) + fd->structoffset);
	}
#endif
}

INTBOOL CheckActorFlag(const AActor *owner, const char *flagname, bool printerror)
{
	const char *dot = strchr (flagname, '.');
	FFlagDef *fd;
	const PClass *cls = owner->GetClass();

	if (dot != NULL)
	{
		FString part1(flagname, dot-flagname);
		fd = FindFlag (cls, part1, dot+1);
	}
	else
	{
		fd = FindFlag (cls, flagname, NULL);
	}

	if (fd != NULL)
	{
		return CheckActorFlag(owner, fd);
	}
	else
	{
		if (printerror) Printf("Unknown flag '%s' in '%s'\n", flagname, cls->TypeName.GetChars());
		return false;
	}
}

//===========================================================================
//
// HandleDeprecatedFlags
//
// Handles the deprecated flags and sets the respective properties
// to appropriate values. This is solely intended for backwards
// compatibility so mixing this with code that is aware of the real
// properties is not recommended
//
//===========================================================================
void HandleDeprecatedFlags(AActor *defaults, PClassActor *info, bool set, int index)
{
	switch (index)
	{
	case DEPF_FIREDAMAGE:
		defaults->DamageType = set? NAME_Fire : NAME_None;
		break;
	case DEPF_ICEDAMAGE:
		defaults->DamageType = set? NAME_Ice : NAME_None;
		break;
	case DEPF_LOWGRAVITY:
		defaults->Gravity = set ? 1. / 8 : 1.;
		break;
	case DEPF_SHORTMISSILERANGE:
		defaults->maxtargetrange = set? 896. : 0.;
		break;
	case DEPF_LONGMELEERANGE:
		defaults->meleethreshold = set? 196. : 0.;
		break;
	case DEPF_QUARTERGRAVITY:
		defaults->Gravity = set ? 1. / 4 : 1.;
		break;
	case DEPF_FIRERESIST:
		info->SetDamageFactor(NAME_Fire, set ? 0.5 : 1.);
		break;
	// the bounce flags will set the compatibility bounce modes to remain compatible
	case DEPF_HERETICBOUNCE:
		defaults->BounceFlags &= ~(BOUNCE_TypeMask|BOUNCE_UseSeeSound);
		if (set) defaults->BounceFlags |= BOUNCE_HereticCompat;
		break;
	case DEPF_HEXENBOUNCE:
		defaults->BounceFlags &= ~(BOUNCE_TypeMask|BOUNCE_UseSeeSound);
		if (set) defaults->BounceFlags |= BOUNCE_HexenCompat;
		break;
	case DEPF_DOOMBOUNCE:
		defaults->BounceFlags &= ~(BOUNCE_TypeMask|BOUNCE_UseSeeSound);
		if (set) defaults->BounceFlags |= BOUNCE_DoomCompat;
		break;
	case DEPF_PICKUPFLASH:
		if (set)
		{
			static_cast<AInventory*>(defaults)->PickupFlash = FindClassTentative("PickupFlash", RUNTIME_CLASS(AActor));
		}
		else
		{
			static_cast<AInventory*>(defaults)->PickupFlash = NULL;
		}
		break;
	case DEPF_INTERHUBSTRIP: // Old system was 0 or 1, so if the flag is cleared, assume 1.
		static_cast<AInventory*>(defaults)->InterHubAmount = set ? 0 : 1;
		break;
	case DEPF_NOTRAIL:
	{
		FString propname = "@property@powerspeed.notrail";
		FName name(propname, true);
		if (name != NAME_None)
		{
			auto propp = dyn_cast<PProperty>(info->Symbols.FindSymbol(name, true));
			if (propp != nullptr)
			{
				*((char*)defaults + propp->Variables[0]->Offset) = set ? 1 : 0;
			}
		}
		break;
	}


	default:
		break;	// silence GCC
	}
}

//===========================================================================
//
// CheckDeprecatedFlags
//
// Checks properties related to deprecated flags, and returns true only
// if the relevant properties are configured exactly as they would have
// been by setting the flag in HandleDeprecatedFlags.
//
//===========================================================================

bool CheckDeprecatedFlags(const AActor *actor, PClassActor *info, int index)
{
	// A deprecated flag is false if
	// a) it hasn't been added here
	// b) any property of the actor differs from what it would be after setting the flag using HandleDeprecatedFlags

	// Deprecated flags are normally replaced by something more flexible, which means a multitude of related configurations
	// will report "false".

	switch (index)
	{
	case DEPF_FIREDAMAGE:
		return actor->DamageType == NAME_Fire;
	case DEPF_ICEDAMAGE:
		return actor->DamageType == NAME_Ice;
	case DEPF_LOWGRAVITY:
		return actor->Gravity == 1./8;
	case DEPF_SHORTMISSILERANGE:
		return actor->maxtargetrange == 896.;
	case DEPF_LONGMELEERANGE:
		return actor->meleethreshold == 196.;
	case DEPF_QUARTERGRAVITY:
		return actor->Gravity == 1./4;
	case DEPF_FIRERESIST:
		if (info->DamageFactors)
		{
			double *df = info->DamageFactors->CheckKey(NAME_Fire);
			return df && (*df) == 0.5;
		}
		return false;

	case DEPF_HERETICBOUNCE:
		return (actor->BounceFlags & (BOUNCE_TypeMask|BOUNCE_UseSeeSound)) == BOUNCE_HereticCompat;

	case DEPF_HEXENBOUNCE:
		return (actor->BounceFlags & (BOUNCE_TypeMask|BOUNCE_UseSeeSound)) == BOUNCE_HexenCompat;
	
	case DEPF_DOOMBOUNCE:
		return (actor->BounceFlags & (BOUNCE_TypeMask|BOUNCE_UseSeeSound)) == BOUNCE_DoomCompat;

	case DEPF_PICKUPFLASH:
		return static_cast<const AInventory*>(actor)->PickupFlash == PClass::FindClass("PickupFlash");
		// A pure name lookup may or may not be more efficient, but I know no static identifier for PickupFlash.

	case DEPF_INTERHUBSTRIP:
		return !(static_cast<const AInventory*>(actor)->InterHubAmount);
	}

	return false; // Any entirely unknown flag is not set
}

//==========================================================================
//
// 
//
//==========================================================================
int MatchString (const char *in, const char **strings)
{
	int i;

	for (i = 0; *strings != NULL; i++)
	{
		if (!stricmp(in, *strings++))
		{
			return i;
		}
	}
	return -1;
}

//==========================================================================
//
// Get access to scripted pointers.
// They need a bit more work than other variables.
//
//==========================================================================

static bool PointerCheck(PType *symtype, PType *checktype)
{
	auto symptype = dyn_cast<PClassPointer>(symtype);
	auto checkptype = dyn_cast<PClassPointer>(checktype);
	return symptype != nullptr && checkptype != nullptr && symptype->ClassRestriction->IsDescendantOf(checkptype->ClassRestriction);
}

static void *ScriptVar(DObject *obj, PClass *cls, FName field, PType *type)
{
	auto sym = dyn_cast<PField>(cls->Symbols.FindSymbol(field, true));
	if (sym && (sym->Type == type || PointerCheck(sym->Type, type)))
	{
		return (((char*)obj) + sym->Offset);
	}
	I_Error("Variable %s of type %s not found in %s\n", field.GetChars(), type->DescriptiveName(), cls->TypeName.GetChars());
	return nullptr;
}

template<class T>
T &TypedScriptVar(DObject *obj, PClass *cls, FName field, PType *type)
{
	return *(T*)ScriptVar(obj, cls, field, type);
}

//==========================================================================
//
// Info Property handlers
//
//==========================================================================


//==========================================================================
//
//==========================================================================
DEFINE_INFO_PROPERTY(game, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	if (!stricmp(str, "Doom"))
	{
		info->GameFilter |= GAME_Doom;
	}
	else if (!stricmp(str, "Heretic"))
	{
		info->GameFilter |= GAME_Heretic;
	}
	else if (!stricmp(str, "Hexen"))
	{
		info->GameFilter |= GAME_Hexen;
	}
	else if (!stricmp(str, "Raven"))
	{
		info->GameFilter |= GAME_Raven;
	}
	else if (!stricmp(str, "Strife"))
	{
		info->GameFilter |= GAME_Strife;
	}
	else if (!stricmp(str, "Chex"))
	{
		info->GameFilter |= GAME_Chex;
	}
	else if (!stricmp(str, "Any"))
	{
		info->GameFilter = GAME_Any;
	}
	else
	{
		I_Error ("Unknown game type %s", str);
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_INFO_PROPERTY(spawnid, I, Actor)
{
	PROP_INT_PARM(id, 0);
	if (id<0 || id>65535)
	{
		I_Error ("SpawnID must be in the range [0,65535]");
	}
	else info->SpawnID=(WORD)id;
}

//==========================================================================
//
//==========================================================================
DEFINE_INFO_PROPERTY(conversationid, IiI, Actor)
{
	PROP_INT_PARM(convid, 0);
	PROP_INT_PARM(id1, 1);
	PROP_INT_PARM(id2, 2);

	if (convid <= 0 || convid > 65535) return;	// 0 is not usable because the dialogue scripts use it as 'no object'.
	else info->ConversationID=(WORD)convid;
}

//==========================================================================
//
// Property handlers
//
//==========================================================================

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(skip_super, 0, Actor)
{
	auto actorclass = RUNTIME_CLASS(AActor);
	if (info->Size != actorclass->Size)
	{
		bag.ScriptPosition.Message(MSG_OPTERROR,
			"'skip_super' is only allowed in subclasses of AActor with no additional fields and will be ignored in type %s.", info->TypeName.GetChars());
		return;
	}
	if (bag.StateSet)
	{
		bag.ScriptPosition.Message(MSG_OPTERROR,
			"'skip_super' must appear before any state definitions.");
		return;
	}

	memcpy ((void *)defaults, (void *)GetDefault<AActor>(), sizeof(AActor));
	ResetBaggage (&bag, RUNTIME_CLASS(AActor));
}

//==========================================================================
// for internal use only - please do not document!
//==========================================================================
DEFINE_PROPERTY(defaultstateusage, I, Actor)
{
	PROP_INT_PARM(use, 0);
	static_cast<PClassActor*>(bag.Info)->DefaultStateUsage = use;

}
//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(tag, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	defaults->SetTag(str);
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(health, I, Actor)
{
	PROP_INT_PARM(id, 0);
	defaults->health=id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(gibhealth, I, Actor)
{
	PROP_INT_PARM(id, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->GibHealth = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(woundhealth, I, Actor)
{
	PROP_INT_PARM(id, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->WoundHealth = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(reactiontime, I, Actor)
{
	PROP_INT_PARM(id, 0);
	defaults->reactiontime=id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(painchance, ZI, Actor)
{
	PROP_STRING_PARM(str, 0);
	PROP_INT_PARM(id, 1);
	if (str == NULL)
	{
		defaults->PainChance=id;
	}
	else
	{
		FName painType;
		if (!stricmp(str, "Normal")) painType = NAME_None;
		else painType=str;

		info->SetPainChance(painType, id);
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(painthreshold, I, Actor)
{
	PROP_INT_PARM(id, 0);

	defaults->PainThreshold = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(defthreshold, I, Actor)
{
	PROP_INT_PARM(id, 0);
	if (id < 0)
		I_Error("DefThreshold cannot be negative.");
	defaults->DefThreshold = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(threshold, I, Actor)
{
	PROP_INT_PARM(id, 0);
	if (id < 0)
		I_Error("Threshold cannot be negative.");
	defaults->threshold = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(damage, X, Actor)
{
	PROP_INT_PARM(dmgval, 0);
	PROP_EXP_PARM(id, 1);

	// Damage can either be a single number, in which case it is subject
	// to the original damage calculation rules. Or, it can be an expression
	// and will be calculated as-is, ignoring the original rules. For
	// compatibility reasons, expressions must be enclosed within
	// parentheses.

	defaults->DamageVal = dmgval;
	// Only DECORATE can get here with a valid expression.
	CreateDamageFunction(bag.Info, defaults, id, true, bag.Lumpnum);
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(damagemultiply, F, Actor)
{
	PROP_FLOAT_PARM(dmgm, 0);
	defaults->DamageMultiply = dmgm;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(projectilekickback, I, Actor)
{
	PROP_INT_PARM(id, 0);

	defaults->projectileKickback = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(speed, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->Speed = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(floatspeed, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->FloatSpeed = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(radius, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->radius = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(renderradius, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->renderradius = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(height, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->Height=id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(projectilepassheight, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->projectilepassheight=id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(mass, I, Actor)
{
	PROP_INT_PARM(id, 0);
	defaults->Mass=id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(xscale, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->Scale.X = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(yscale, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->Scale.Y = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(scale, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->Scale.X = defaults->Scale.Y = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(floatbobphase, I, Actor)
{
	PROP_INT_PARM(id, 0);
	if (id < -1 || id >= 64) I_Error ("FloatBobPhase must be in range [-1,63]");
	defaults->FloatBobPhase = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(args, Iiiii, Actor)
{
	for (int i = 0; i < PROP_PARM_COUNT; i++)
	{
		PROP_INT_PARM(id, i);
		defaults->args[i] = id;
	}
	defaults->flags2|=MF2_ARGSDEFINED;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(seesound, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	defaults->SeeSound = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(attacksound, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	defaults->AttackSound = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(bouncesound, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	defaults->BounceSound = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(wallbouncesound, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	defaults->WallBounceSound = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(painsound, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	defaults->PainSound = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(deathsound, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	defaults->DeathSound = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(activesound, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	defaults->ActiveSound = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(howlsound, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->HowlSound = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(crushpainsound, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	defaults->CrushPainSound = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(dropitem, S_i_i, Actor)
{
	PROP_STRING_PARM(type, 0);

	// create a linked list of dropitems
	if (!bag.DropItemSet)
	{
		bag.DropItemSet = true;
		bag.DropItemList = NULL;
	}

	DDropItem *di = new DDropItem;

	di->Name = type;
	di->Probability = 255;
	di->Amount = -1;

	if (PROP_PARM_COUNT > 1)
	{
		PROP_INT_PARM(prob, 1);
		di->Probability = prob;
		if (PROP_PARM_COUNT > 2)
		{
			PROP_INT_PARM(amt, 2);
			di->Amount = amt;
		}
	}
	di->Next = bag.DropItemList;
	bag.DropItemList = di;
	GC::WriteBarrier(di);
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(renderstyle, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	static const char * renderstyles[]={
		"NONE", "NORMAL", "FUZZY", "SOULTRANS", "OPTFUZZY", "STENCIL", 
		"TRANSLUCENT", "ADD", "SHADED", "SHADOW", "SUBTRACT", "ADDSTENCIL", "ADDSHADED", NULL };

	static const int renderstyle_values[]={
		STYLE_None, STYLE_Normal, STYLE_Fuzzy, STYLE_SoulTrans, STYLE_OptFuzzy,
			STYLE_TranslucentStencil, STYLE_Translucent, STYLE_Add, STYLE_Shaded,
			STYLE_Shadow, STYLE_Subtract, STYLE_AddStencil, STYLE_AddShaded};

	// make this work for old style decorations, too.
	if (!strnicmp(str, "style_", 6)) str+=6;

	int style = MatchString(str, renderstyles);
	if (style < 0) I_Error("Unknown render style '%s'", str);
	defaults->RenderStyle = LegacyRenderStyles[renderstyle_values[style]];
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(defaultalpha, 0, Actor)
{
	defaults->Alpha = gameinfo.gametype == GAME_Heretic ? HR_SHADOW : HX_SHADOW;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(alpha, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->Alpha = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(obituary, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->Obituary = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(hitobituary, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->HitObituary = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(donthurtshooter, 0, Actor)
{
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->DontHurtShooter = true;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(explosionradius, I, Actor)
{
	PROP_INT_PARM(id, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->ExplosionRadius = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(explosiondamage, I, Actor)
{
	PROP_INT_PARM(id, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->ExplosionDamage = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(deathheight, F, Actor)
{
	PROP_DOUBLE_PARM(h, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->DeathHeight = MAX(0., h);
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(burnheight, F, Actor)
{
	PROP_DOUBLE_PARM(h, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->BurnHeight = MAX(0., h);
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(maxtargetrange, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->maxtargetrange = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(meleethreshold, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->meleethreshold = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(meleedamage, I, Actor)
{
	PROP_INT_PARM(id, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->MeleeDamage = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(meleerange, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->meleerange = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(meleesound, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->MeleeSound = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(missiletype, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->MissileName = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(missileheight, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->MissileHeight = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(pushfactor, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->pushfactor = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(translation, L, Actor)
{
	PROP_INT_PARM(type, 0);

	if (type == 0)
	{
		PROP_INT_PARM(trans, 1);
		int max = 6;// (gameinfo.gametype == GAME_Strife || (info->GameFilter&GAME_Strife)) ? 6 : 2;
		if (trans < 0 || trans > max)
		{
			I_Error ("Translation must be in the range [0,%d]", max);
		}
		defaults->Translation = TRANSLATION(TRANSLATION_Standard, trans);
	}
	else 
	{
		FRemapTable CurrentTranslation;

		CurrentTranslation.MakeIdentity();
		for(int i = 1; i < PROP_PARM_COUNT; i++)
		{
			PROP_STRING_PARM(str, i);
			int tnum;
			if (i== 1 && PROP_PARM_COUNT == 2 && (tnum = R_FindCustomTranslation(str)) != -1)
			{
				defaults->Translation = tnum;
				return;
			}
			else
			{
				CurrentTranslation.AddToTranslation(str);
			}
		}
		defaults->Translation = CurrentTranslation.StoreTranslation (TRANSLATION_Decorate);
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(stencilcolor, C, Actor)
{
	PROP_COLOR_PARM(color, 0);

	defaults->fillcolor = color | (ColorMatcher.Pick (RPART(color), GPART(color), BPART(color)) << 24);
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(bloodcolor, C, Actor)
{
	PROP_COLOR_PARM(color, 0);

	PalEntry pe = color;
	pe.a = CreateBloodTranslation(pe);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->BloodColor = pe;
}


//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(bloodtype, Sss, Actor)
{
	PROP_STRING_PARM(str, 0)
	PROP_STRING_PARM(str1, 1)
	PROP_STRING_PARM(str2, 2)

	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	PClassActor *ainfo = static_cast<PClassActor *>(info);

	FName blood = str;
	// normal blood
	ainfo->BloodType = blood;

	if (PROP_PARM_COUNT > 1)
	{
		blood = str1;
	}
	// blood splatter
	ainfo->BloodType2 = blood;

	if (PROP_PARM_COUNT > 2)
	{
		blood = str2;
	}
	// axe blood
	ainfo->BloodType3 = blood;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(bouncetype, S, Actor)
{
	static const char *names[] = { "None", "Doom", "Heretic", "Hexen", "DoomCompat", "HereticCompat", "HexenCompat", "Grenade", "Classic", NULL };
	static const ActorBounceFlag flags[] = { BOUNCE_None,
		BOUNCE_Doom, BOUNCE_Heretic, BOUNCE_Hexen,
		BOUNCE_DoomCompat, BOUNCE_HereticCompat, BOUNCE_HexenCompat,
		BOUNCE_Grenade, BOUNCE_Classic, };
	PROP_STRING_PARM(id, 0);
	int match = MatchString(id, names);
	if (match < 0)
	{
		I_Error("Unknown bouncetype %s", id);
		match = 0;
	}
	defaults->BounceFlags &= ~(BOUNCE_TypeMask | BOUNCE_UseSeeSound);
	defaults->BounceFlags |= flags[match];
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(bouncefactor, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->bouncefactor = clamp<double>(id, 0, 1);
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(wallbouncefactor, F, Actor)
{
	PROP_DOUBLE_PARM(id, 0);
	defaults->wallbouncefactor = clamp<double>(id, 0, 1);
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(bouncecount, I, Actor)
{
	PROP_INT_PARM(id, 0);
	defaults->bouncecount = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(weaveindexXY, I, Actor)
{
	PROP_INT_PARM(id, 0);
	defaults->WeaveIndexXY = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(weaveindexZ, I, Actor)
{
	PROP_INT_PARM(id, 0);
	defaults->WeaveIndexZ = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(minmissilechance, I, Actor)
{
	PROP_INT_PARM(id, 0);
	defaults->MinMissileChance=id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(damagetype, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	if (!stricmp(str, "Normal")) defaults->DamageType = NAME_None;
	else defaults->DamageType=str;
}

//==========================================================================

//==========================================================================
DEFINE_PROPERTY(paintype, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	if (!stricmp(str, "Normal")) defaults->PainType = NAME_None;
	else defaults->PainType=str;
}

//==========================================================================

//==========================================================================
DEFINE_PROPERTY(deathtype, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	if (!stricmp(str, "Normal")) defaults->DeathType = NAME_None;
	else defaults->DeathType=str;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(damagefactor, ZF, Actor)
{
	PROP_STRING_PARM(str, 0);
	PROP_DOUBLE_PARM(id, 1);

	if (str == NULL)
	{
		defaults->DamageFactor = id;
	}
	else
	{
		FName dmgType;
		if (!stricmp(str, "Normal")) dmgType = NAME_None;
		else dmgType=str;

		info->SetDamageFactor(dmgType, id);
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(decal, S, Actor)
{
	PROP_STRING_PARM(str, 0);
	defaults->DecalGenerator = (FDecalBase *)intptr_t(int(FName(str)));
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(maxstepheight, F, Actor)
{
	PROP_DOUBLE_PARM(i, 0);
	defaults->MaxStepHeight = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(maxdropoffheight, F, Actor)
{
	PROP_DOUBLE_PARM(i, 0);
	defaults->MaxDropOffHeight = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(poisondamage, Iii, Actor)
{
	PROP_INT_PARM(poisondamage, 0);
	PROP_INT_PARM(poisonduration, 1);
	PROP_INT_PARM(poisonperiod, 2);

	defaults->PoisonDamage = poisondamage;
	if (PROP_PARM_COUNT == 1)
	{
		defaults->PoisonDuration = INT_MIN;
	}
	else
	{
		defaults->PoisonDuration = poisonduration;

		if (PROP_PARM_COUNT > 2)
			defaults->PoisonPeriod = poisonperiod;
		else
			defaults->PoisonPeriod = 0;
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(poisondamagetype, S, Actor)
{
	PROP_STRING_PARM(poisondamagetype, 0);

	defaults->PoisonDamageType = poisondamagetype;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(fastspeed, F, Actor)
{
	PROP_DOUBLE_PARM(i, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->FastSpeed = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(radiusdamagefactor, F, Actor)
{
	PROP_DOUBLE_PARM(i, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->RDFactor = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(cameraheight, F, Actor)
{
	PROP_DOUBLE_PARM(i, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassActor)));
	static_cast<PClassActor *>(info)->CameraHeight = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(vspeed, F, Actor)
{
	PROP_DOUBLE_PARM(i, 0);
	defaults->Vel.Z = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(gravity, F, Actor)
{
	PROP_DOUBLE_PARM(i, 0);

	if (i < 0) I_Error ("Gravity must not be negative.");
	defaults->Gravity = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(spriteangle, F, Actor)
{
	PROP_DOUBLE_PARM(i, 0);
	defaults->SpriteAngle = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(spriterotation, F, Actor)
{
	PROP_DOUBLE_PARM(i, 0);
	defaults->SpriteRotation = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(visibleangles, FF, Actor)
{
	PROP_DOUBLE_PARM(visstart, 0);
	PROP_DOUBLE_PARM(visend, 1);
	defaults->VisibleStartAngle = visstart;
	defaults->VisibleEndAngle = visend;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(visiblepitch, FF, Actor)
{
	PROP_DOUBLE_PARM(visstart, 0);
	PROP_DOUBLE_PARM(visend, 1);
	defaults->VisibleStartPitch = visstart;
	defaults->VisibleEndPitch = visend;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(friction, F, Actor)
{
	PROP_DOUBLE_PARM(i, 0);

	if (i < 0) I_Error ("Friction must not be negative.");
	defaults->Friction = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(species, S, Actor)
{
	PROP_STRING_PARM(n, 0);
	defaults->Species = n;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(clearflags, 0, Actor)
{
	defaults->flags = 0;
	defaults->flags2 &= MF2_ARGSDEFINED;	// this flag must not be cleared
	defaults->flags3 = 0;
	defaults->flags4 = 0;
	defaults->flags5 = 0;
	defaults->flags6 = 0;
	defaults->flags7 = 0;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(monster, 0, Actor)
{
	// sets the standard flags for a monster
	defaults->flags|=MF_SHOOTABLE|MF_COUNTKILL|MF_SOLID; 
	defaults->flags2|=MF2_PUSHWALL|MF2_MCROSS|MF2_PASSMOBJ;
	defaults->flags3|=MF3_ISMONSTER;
	defaults->flags4|=MF4_CANUSEWALLS;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(projectile, 0, Actor)
{
	// sets the standard flags for a projectile
	defaults->flags|=MF_NOBLOCKMAP|MF_NOGRAVITY|MF_DROPOFF|MF_MISSILE; 
	defaults->flags2|=MF2_IMPACT|MF2_PCROSS|MF2_NOTELEPORT;
	if (gameinfo.gametype&GAME_Raven) defaults->flags5|=MF5_BLOODSPLATTER;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(activation, N, Actor)
{
	// How the thing behaves when activated by death, USESPECIAL or BUMPSPECIAL
	PROP_INT_PARM(val, 0);
	defaults->activationtype = val;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(designatedteam, I, Actor)
{
	PROP_INT_PARM(val, 0);
	if(val < 0 || (val >= (signed) Teams.Size() && val != TEAM_NONE))
		I_Error("Invalid team designation.\n");
	defaults->DesignatedTeam = val;
}

//==========================================================================
// [BB]
//==========================================================================
DEFINE_PROPERTY(visibletoteam, I, Actor)
{
	PROP_INT_PARM(i, 0);
	defaults->VisibleToTeam=i+1;
}

//==========================================================================
// [BB]
//==========================================================================
DEFINE_PROPERTY(visibletoplayerclass, Ssssssssssssssssssss, Actor)
{
	info->VisibleToPlayerClass.Clear();
	for(int i = 0;i < PROP_PARM_COUNT;++i)
	{
		PROP_STRING_PARM(n, i);
		if (*n != 0)
			info->VisibleToPlayerClass.Push(FindClassTentativePlayerPawn(n));
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(accuracy, I, Actor)
{
	PROP_INT_PARM(i, 0);
	defaults->accuracy = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(stamina, I, Actor)
{
	PROP_INT_PARM(i, 0);
	defaults->stamina = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(telefogsourcetype, S, Actor)
{
	PROP_STRING_PARM(str, 0);

	defaults->TeleFogSourceType = FindClassTentative(str, RUNTIME_CLASS(AActor));
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(telefogdesttype, S, Actor)
{
	PROP_STRING_PARM(str, 0);

	defaults->TeleFogDestType = FindClassTentative(str, RUNTIME_CLASS(AActor));
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(ripperlevel, I, Actor)
{
	PROP_INT_PARM(id, 0);
	defaults->RipperLevel = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(riplevelmin, I, Actor)
{
	PROP_INT_PARM(id, 0);
	defaults->RipLevelMin = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(riplevelmax, I, Actor)
{
	PROP_INT_PARM(id, 0);
	defaults->RipLevelMax = id;
}

//==========================================================================
//
//==========================================================================
DEFINE_PROPERTY(distancecheck, S, Actor)
{
	PROP_STRING_PARM(cvar, 0);
	FBaseCVar *scratch;
	FBaseCVar *cv = FindCVar(cvar, &scratch);
	if (cv == NULL)
	{
		I_Error("CVar %s not defined", cvar);
	}
	else if (cv->GetRealType() == CVAR_Int)
	{
		static_cast<PClassActor*>(info)->distancecheck = static_cast<FIntCVar *>(cv);
	}
	else
	{
		I_Error("CVar %s must of type Int", cvar);
	}
}

//==========================================================================
//
// Special inventory properties
//
//==========================================================================

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(restrictedto, Ssssssssssssssssssss, Inventory)
{
	static_cast<PClassInventory*>(info)->RestrictedToPlayerClass.Clear();
	for(int i = 0;i < PROP_PARM_COUNT;++i)
	{
		PROP_STRING_PARM(n, i);
		if (*n != 0)
			static_cast<PClassInventory*>(info)->RestrictedToPlayerClass.Push(FindClassTentativePlayerPawn(n));
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(forbiddento, Ssssssssssssssssssss, Inventory)
{
	static_cast<PClassInventory*>(info)->ForbiddenToPlayerClass.Clear();
	for(int i = 0;i < PROP_PARM_COUNT;++i)
	{
		PROP_STRING_PARM(n, i);
		if (*n != 0)
			static_cast<PClassInventory*>(info)->ForbiddenToPlayerClass.Push(FindClassTentativePlayerPawn(n));
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(backpackamount, I, Ammo)
{
	PROP_INT_PARM(i, 0);
	defaults->BackpackAmount = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(backpackmaxamount, I, Ammo)
{
	PROP_INT_PARM(i, 0);
	defaults->BackpackMaxAmount = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(dropamount, I, Ammo)
{
	PROP_INT_PARM(i, 0);
	defaults->DropAmount = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(armor, maxsaveamount, I, BasicArmorBonus)
{
	PROP_INT_PARM(i, 0);
	defaults->MaxSaveAmount = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(armor, maxbonus, I, BasicArmorBonus)
{
	PROP_INT_PARM(i, 0);
	defaults->BonusCount = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(armor, maxbonusmax, I, BasicArmorBonus)
{
	PROP_INT_PARM(i, 0);
	defaults->BonusMax = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(saveamount, I, Armor)
{
	PROP_INT_PARM(i, 0);

	// Special case here because this property has to work for 2 unrelated classes
	if (info->IsDescendantOf(RUNTIME_CLASS(ABasicArmorPickup)))
	{
		((ABasicArmorPickup*)defaults)->SaveAmount=i;
	}
	else if (info->IsDescendantOf(RUNTIME_CLASS(ABasicArmorBonus)))
	{
		((ABasicArmorBonus*)defaults)->SaveAmount=i;
	}
	else
	{
		I_Error("\"Armor.SaveAmount\" requires an actor of type \"Armor\"");
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(savepercent, F, Armor)
{
	PROP_DOUBLE_PARM(i, 0);

	i = clamp(i, 0., 100.)/100.;
	// Special case here because this property has to work for 2 unrelated classes
	if (info->IsDescendantOf(RUNTIME_CLASS(ABasicArmorPickup)))
	{
		((ABasicArmorPickup*)defaults)->SavePercent = i;
	}
	else if (info->IsDescendantOf(RUNTIME_CLASS(ABasicArmorBonus)))
	{
		((ABasicArmorBonus*)defaults)->SavePercent = i;
	}
	else
	{
		I_Error("\"Armor.SavePercent\" requires an actor of type \"Armor\"\n");
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(maxabsorb, I, Armor)
{
	PROP_INT_PARM(i, 0);

	// Special case here because this property has to work for 2 unrelated classes
	if (info->IsDescendantOf(RUNTIME_CLASS(ABasicArmorPickup)))
	{
		((ABasicArmorPickup*)defaults)->MaxAbsorb = i;
	}
	else if (info->IsDescendantOf(RUNTIME_CLASS(ABasicArmorBonus)))
	{
		((ABasicArmorBonus*)defaults)->MaxAbsorb = i;
	}
	else
	{
		I_Error("\"Armor.MaxAbsorb\" requires an actor of type \"Armor\"\n");
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(maxfullabsorb, I, Armor)
{
	PROP_INT_PARM(i, 0);

	// Special case here because this property has to work for 2 unrelated classes
	if (info->IsDescendantOf(RUNTIME_CLASS(ABasicArmorPickup)))
	{
		((ABasicArmorPickup*)defaults)->MaxFullAbsorb = i;
	}
	else if (info->IsDescendantOf(RUNTIME_CLASS(ABasicArmorBonus)))
	{
		((ABasicArmorBonus*)defaults)->MaxFullAbsorb = i;
	}
	else
	{
		I_Error("\"Armor.MaxFullAbsorb\" requires an actor of type \"Armor\"\n");
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(amount, I, Inventory)
{
	PROP_INT_PARM(i, 0);
	defaults->Amount = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(icon, S, Inventory)
{
	PROP_STRING_PARM(i, 0);

	if (i == NULL || i[0] == '\0')
	{
		defaults->Icon.SetNull();
	}
	else
	{
		defaults->Icon = TexMan.CheckForTexture(i, FTexture::TEX_MiscPatch);
		if (!defaults->Icon.isValid())
		{
			// Don't print warnings if the item is for another game or if this is a shareware IWAD. 
			// Strife's teaser doesn't contain all the icon graphics of the full game.
			if ((info->GameFilter == GAME_Any || info->GameFilter & gameinfo.gametype) &&
				!(gameinfo.flags&GI_SHAREWARE) && Wads.GetLumpFile(bag.Lumpnum) != 0)
			{
				bag.ScriptPosition.Message(MSG_WARNING,
					"Icon '%s' for '%s' not found\n", i, info->TypeName.GetChars());
			}
		}
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(interhubamount, I, Inventory)
{
	PROP_INT_PARM(i, 0);
	defaults->InterHubAmount = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(maxamount, I, Inventory)
{
	PROP_INT_PARM(i, 0);
	defaults->MaxAmount = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(defmaxamount, 0, Inventory)
{
	defaults->MaxAmount = gameinfo.definventorymaxamount;
}


//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(pickupflash, S, Inventory)
{
	PROP_STRING_PARM(str, 0);
	defaults->PickupFlash = FindClassTentative(str, RUNTIME_CLASS(AActor));
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(pickupmessage, T, Inventory)
{
	PROP_STRING_PARM(str, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassInventory)));
	static_cast<PClassInventory *>(info)->PickupMessage = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(pickupsound, S, Inventory)
{
	PROP_STRING_PARM(str, 0);
	defaults->PickupSound = str;
}

//==========================================================================
// Dummy for Skulltag compatibility...
//==========================================================================
DEFINE_CLASS_PROPERTY(pickupannouncerentry, S, Inventory)
{
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(respawntics, I, Inventory)
{
	PROP_INT_PARM(i, 0);
	defaults->RespawnTics = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(usesound, S, Inventory)
{
	PROP_STRING_PARM(str, 0);
	defaults->UseSound = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(givequest, I, Inventory)
{
	PROP_INT_PARM(i, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassInventory)));
	static_cast<PClassInventory *>(info)->GiveQuest = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(ammogive, I, Weapon)
{
	PROP_INT_PARM(i, 0);
	defaults->AmmoGive1 = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(ammogive1, I, Weapon)
{
	PROP_INT_PARM(i, 0);
	defaults->AmmoGive1 = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(ammogive2, I, Weapon)
{
	PROP_INT_PARM(i, 0);
	defaults->AmmoGive2 = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(ammotype, S, Weapon)
{
	PROP_STRING_PARM(str, 0);
	if (!stricmp(str, "none") || *str == 0) defaults->AmmoType1 = NULL;
	else defaults->AmmoType1 = FindClassTentativeAmmo(str);
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(ammotype1, S, Weapon)
{
	PROP_STRING_PARM(str, 0);
	if (!stricmp(str, "none") || *str == 0) defaults->AmmoType1 = NULL;
	else defaults->AmmoType1 = FindClassTentativeAmmo(str);
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(ammotype2, S, Weapon)
{
	PROP_STRING_PARM(str, 0);
	if (!stricmp(str, "none") || *str == 0) defaults->AmmoType1 = NULL;
	else defaults->AmmoType2 = FindClassTentativeAmmo(str);
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(ammouse, I, Weapon)
{
	PROP_INT_PARM(i, 0);
	defaults->AmmoUse1 = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(ammouse1, I, Weapon)
{
	PROP_INT_PARM(i, 0);
	defaults->AmmoUse1 = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(ammouse2, I, Weapon)
{
	PROP_INT_PARM(i, 0);
	defaults->AmmoUse2 = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(kickback, I, Weapon)
{
	PROP_INT_PARM(i, 0);
	defaults->Kickback = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(defaultkickback, 0, Weapon)
{
	defaults->Kickback = gameinfo.defKickback;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(readysound, S, Weapon)
{
	PROP_STRING_PARM(str, 0);
	defaults->ReadySound = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(selectionorder, I, Weapon)
{
	PROP_INT_PARM(i, 0);
	defaults->SelectionOrder = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(minselectionammo1, I, Weapon)
{
	PROP_INT_PARM(i, 0);
	defaults->MinSelAmmo1 = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(minselectionammo2, I, Weapon)
{
	PROP_INT_PARM(i, 0);
	defaults->MinSelAmmo2 = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(sisterweapon, S, Weapon)
{
	PROP_STRING_PARM(str, 0);
	defaults->SisterWeaponType = FindClassTentativeWeapon(str);
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(upsound, S, Weapon)
{
	PROP_STRING_PARM(str, 0);
	defaults->UpSound = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(yadjust, F, Weapon)
{
	PROP_FLOAT_PARM(i, 0);
	defaults->YAdjust = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(bobstyle, S, Weapon)
{
	static const char *names[] = { "Normal", "Inverse", "Alpha", "InverseAlpha", "Smooth", "InverseSmooth", NULL };
	static const int styles[] = { AWeapon::BobNormal,
		AWeapon::BobInverse, AWeapon::BobAlpha, AWeapon::BobInverseAlpha,
		AWeapon::BobSmooth, AWeapon::BobInverseSmooth, };
	PROP_STRING_PARM(id, 0);
	int match = MatchString(id, names);
	if (match < 0)
	{
		I_Error("Unknown bobstyle %s", id);
		match = 0;
	}
	defaults->BobStyle = styles[match];
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(bobspeed, F, Weapon)
{
	PROP_FLOAT_PARM(i, 0);
	defaults->BobSpeed = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(bobrangex, F, Weapon)
{
	PROP_FLOAT_PARM(i, 0);
	defaults->BobRangeX = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(bobrangey, F, Weapon)
{
	PROP_FLOAT_PARM(i, 0);
	defaults->BobRangeY = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(slotnumber, I, Weapon)
{
	PROP_INT_PARM(i, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassWeapon)));
	static_cast<PClassWeapon *>(info)->SlotNumber = i;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(slotpriority, F, Weapon)
{
	PROP_DOUBLE_PARM(i, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassWeapon)));
	static_cast<PClassWeapon *>(info)->SlotPriority = int(i*65536);
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(preferredskin, S, Weapon)
{
	PROP_STRING_PARM(str, 0);
	// NoOp - only for Skulltag compatibility
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(number, I, WeaponPiece)
{
	PROP_INT_PARM(i, 0);
	defaults->PieceValue = 1 << (i-1);
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(weapon, S, WeaponPiece)
{
	PROP_STRING_PARM(str, 0);
	defaults->WeaponClass = FindClassTentativeWeapon(str);
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(powerup, color, C_f, Inventory)
{
	static const char *specialcolormapnames[] = {
		"INVERSEMAP", "GOLDMAP", "REDMAP", "GREENMAP", "BLUEMAP", NULL };

	int alpha;
	PalEntry *pBlendColor;
	bool isgiver = info->IsDescendantOf(PClass::FindActor(NAME_PowerupGiver));

	if (info->IsDescendantOf(PClass::FindActor(NAME_Powerup)) || isgiver)
	{
		pBlendColor = &defaults->ColorVar(NAME_BlendColor);
	}
	else
	{
		I_Error("\"powerup.color\" requires an actor of type \"Powerup\"\n");
		return;
	}

	PROP_INT_PARM(mode, 0);
	PROP_INT_PARM(color, 1);

	if (mode == 1)
	{
		PROP_STRING_PARM(name, 1);

		// We must check the old special colormap names for compatibility
		int v = MatchString(name, specialcolormapnames);
		if (v >= 0)
		{
			*pBlendColor = MakeSpecialColormap(v);
			return;
		}
		else if (!stricmp(name, "none") && isgiver)
		{
			*pBlendColor = MakeSpecialColormap(65535);
			return;
		}
		color = V_GetColor(NULL, name, &bag.ScriptPosition);
	}
	if (PROP_PARM_COUNT > 2)
	{
		PROP_DOUBLE_PARM(falpha, 2);
		alpha=int(falpha*255);
	}
	else alpha = 255/3;

	alpha=clamp<int>(alpha, 0, 255);
	if (alpha != 0) *pBlendColor = MAKEARGB(alpha, 0, 0, 0) | color;
	else *pBlendColor = 0;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(powerup, colormap, FFFfff, Inventory)
{
	PalEntry BlendColor;

	if (!info->IsDescendantOf(PClass::FindActor(NAME_Powerup)) && !info->IsDescendantOf(PClass::FindActor(NAME_PowerupGiver)))
	{
		I_Error("\"powerup.colormap\" requires an actor of type \"Powerup\"\n");
		return;
	}

	if (PROP_PARM_COUNT == 3)
	{
		PROP_FLOAT_PARM(r, 0);
		PROP_FLOAT_PARM(g, 1);
		PROP_FLOAT_PARM(b, 2);
		BlendColor = MakeSpecialColormap(AddSpecialColormap(0, 0, 0, r, g, b));
	}
	else if (PROP_PARM_COUNT == 6)
	{
		PROP_FLOAT_PARM(r1, 0);
		PROP_FLOAT_PARM(g1, 1);
		PROP_FLOAT_PARM(b1, 2);
		PROP_FLOAT_PARM(r2, 3);
		PROP_FLOAT_PARM(g2, 4);
		PROP_FLOAT_PARM(b2, 5);
		BlendColor = MakeSpecialColormap(AddSpecialColormap(r1, g1, b1, r2, g2, b2));
	}
	else
	{
		I_Error("\"power.colormap\" must have either 3 or 6 parameters\n");
	}
	defaults->ColorVar(NAME_BlendColor) = BlendColor;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(powerup, duration, I, Inventory)
{
	if (!info->IsDescendantOf(PClass::FindActor(NAME_Powerup)) && !info->IsDescendantOf(PClass::FindActor(NAME_PowerupGiver)))
	{
		I_Error("\"powerup.duration\" requires an actor of type \"Powerup\"\n");
		return;
	}

	PROP_INT_PARM(i, 0);
	defaults->IntVar(NAME_EffectTics) = (i >= 0) ? i : -i * TICRATE;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(powerup, strength, F, Inventory)
{
	if (!info->IsDescendantOf(PClass::FindActor(NAME_Powerup)) && !info->IsDescendantOf(PClass::FindActor(NAME_PowerupGiver)))
	{
		I_Error("\"powerup.strength\" requires an actor of type \"Powerup\"\n");
		return;
	}
	PROP_DOUBLE_PARM(f, 0);
	defaults->FloatVar(NAME_Strength) = f;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(powerup, mode, S, Inventory)
{
	PROP_STRING_PARM(str, 0);

	if (!info->IsDescendantOf(PClass::FindActor(NAME_Powerup)) && !info->IsDescendantOf(PClass::FindActor(NAME_PowerupGiver)))
	{
		I_Error("\"powerup.mode\" requires an actor of type \"Powerup\"\n");
		return;
	}
	defaults->NameVar(NAME_Mode) = (FName)str;
}

//==========================================================================
//
//==========================================================================
DEFINE_SCRIPTED_PROPERTY_PREFIX(powerup, type, S, PowerupGiver)
{
	PROP_STRING_PARM(str, 0);

	// Yuck! What was I thinking when I decided to prepend "Power" to the name? 
	// Now it's too late to change it...
	PClassActor *cls = PClass::FindActor(str);
	auto pow = PClass::FindActor(NAME_Powerup);
	if (cls == nullptr || !cls->IsDescendantOf(pow))
	{
		if (bag.fromDecorate)
		{
			FString st;
			st.Format("%s%s", strnicmp(str, "power", 5) ? "Power" : "", str);
			cls = FindClassTentative(st, pow);
		}
		else
		{
			I_Error("Unknown powerup type %s", str);
		}
	}
	TypedScriptVar<PClassActor*>(defaults, info, NAME_PowerupType, NewClassPointer(RUNTIME_CLASS(AActor))) = cls;
}

//==========================================================================
//
// [GRB] Special player properties
//
//==========================================================================

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, displayname, S, PlayerPawn)
{
	PROP_STRING_PARM(str, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassPlayerPawn)));
	static_cast<PClassPlayerPawn *>(info)->DisplayName = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, soundclass, S, PlayerPawn)
{
	PROP_STRING_PARM(str, 0);

	FString tmp = str;
	tmp.ReplaceChars (' ', '_');
	assert(info->IsKindOf(RUNTIME_CLASS(PClassPlayerPawn)));
	static_cast<PClassPlayerPawn *>(info)->SoundClass = tmp;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, face, S, PlayerPawn)
{
	PROP_STRING_PARM(str, 0);
	FString tmp = str;

	tmp.ToUpper();
	bool valid = (tmp.Len() == 3 &&
		(((tmp[0] >= 'A') && (tmp[0] <= 'Z')) || ((tmp[0] >= '0') && (tmp[0] <= '9'))) &&
		(((tmp[1] >= 'A') && (tmp[1] <= 'Z')) || ((tmp[1] >= '0') && (tmp[1] <= '9'))) &&
		(((tmp[2] >= 'A') && (tmp[2] <= 'Z')) || ((tmp[2] >= '0') && (tmp[2] <= '9')))
		);
	if (!valid)
	{
		bag.ScriptPosition.Message(MSG_OPTERROR,
			"Invalid face '%s' for '%s';\nSTF replacement codes must be 3 alphanumeric characters.\n",
			tmp.GetChars(), info->TypeName.GetChars ());
	}
	
	assert(info->IsKindOf(RUNTIME_CLASS(PClassPlayerPawn)));
	static_cast<PClassPlayerPawn *>(info)->Face = tmp;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, colorrange, I_I, PlayerPawn)
{
	PROP_INT_PARM(start, 0);
	PROP_INT_PARM(end, 1);

	if (start > end)
		swapvalues (start, end);

	assert(info->IsKindOf(RUNTIME_CLASS(PClassPlayerPawn)));
	static_cast<PClassPlayerPawn *>(info)->ColorRangeStart = start;
	static_cast<PClassPlayerPawn *>(info)->ColorRangeEnd = end;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, colorset, ISIIIiiiiiiiiiiiiiiiiiiiiiiii, PlayerPawn)
{
	PROP_INT_PARM(setnum, 0);
	PROP_STRING_PARM(setname, 1);
	PROP_INT_PARM(rangestart, 2);
	PROP_INT_PARM(rangeend, 3);
	PROP_INT_PARM(representative_color, 4);

	FPlayerColorSet color;
	color.Name = setname;
	color.Lump = -1;
	color.FirstColor = rangestart;
	color.LastColor = rangeend;
	color.RepresentativeColor = representative_color;
	color.NumExtraRanges = 0;

	if (PROP_PARM_COUNT > 5)
	{
		int count = PROP_PARM_COUNT - 5;
		int start = 5;

		while (count >= 4)
		{
			PROP_INT_PARM(range_start, start+0);
			PROP_INT_PARM(range_end, start+1);
			PROP_INT_PARM(first_color, start+2);
			PROP_INT_PARM(last_color, start+3);
			int extra = color.NumExtraRanges++;
			assert (extra < (int)countof(color.Extra));

			color.Extra[extra].RangeStart = range_start;
			color.Extra[extra].RangeEnd = range_end;
			color.Extra[extra].FirstColor = first_color;
			color.Extra[extra].LastColor = last_color;
			count -= 4;
			start += 4;
		}
		if (count != 0)
		{
			bag.ScriptPosition.Message(MSG_OPTERROR, "Extra ranges require 4 parameters each.\n");
		}
	}

	if (setnum < 0)
	{
		bag.ScriptPosition.Message(MSG_OPTERROR, "Color set number must not be negative.\n");
	}
	else
	{
		assert(info->IsKindOf(RUNTIME_CLASS(PClassPlayerPawn)));
		static_cast<PClassPlayerPawn *>(info)->ColorSets.Insert(setnum, color);
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, colorsetfile, ISSI, PlayerPawn)
{
	PROP_INT_PARM(setnum, 0);
	PROP_STRING_PARM(setname, 1);
	PROP_STRING_PARM(rangefile, 2);
	PROP_INT_PARM(representative_color, 3);

	FPlayerColorSet color;
	color.Name = setname;
	color.Lump = Wads.CheckNumForName(rangefile);
	color.RepresentativeColor = representative_color;
	color.NumExtraRanges = 0;

	if (setnum < 0)
	{
		bag.ScriptPosition.Message(MSG_OPTERROR, "Color set number must not be negative.\n");
	}
	else if (color.Lump >= 0)
	{
		assert(info->IsKindOf(RUNTIME_CLASS(PClassPlayerPawn)));
		static_cast<PClassPlayerPawn *>(info)->ColorSets.Insert(setnum, color);
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, clearcolorset, I, PlayerPawn)
{
	PROP_INT_PARM(setnum, 0);

	if (setnum < 0)
	{
		bag.ScriptPosition.Message(MSG_OPTERROR, "Color set number must not be negative.\n");
	}
	else
	{
		assert(info->IsKindOf(RUNTIME_CLASS(PClassPlayerPawn)));
		static_cast<PClassPlayerPawn *>(info)->ColorSets.Remove(setnum);
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, attackzoffset, F, PlayerPawn)
{
	PROP_DOUBLE_PARM(z, 0);
	defaults->AttackZOffset = z;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, jumpz, F, PlayerPawn)
{
	PROP_DOUBLE_PARM(z, 0);
	defaults->JumpZ = z;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, GruntSpeed, F, PlayerPawn)
{
	PROP_DOUBLE_PARM(z, 0);
	defaults->GruntSpeed = z;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, FallingScreamSpeed, FF, PlayerPawn)
{
	PROP_DOUBLE_PARM(minz, 0);
	PROP_DOUBLE_PARM(maxz, 1);
	defaults->FallingScreamMinSpeed = minz;
	defaults->FallingScreamMaxSpeed = maxz;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, spawnclass, L, PlayerPawn)
{
	PROP_INT_PARM(type, 0);

	if (type == 0)
	{
		PROP_INT_PARM(val, 1);
		if (val > 0) defaults->SpawnMask |= 1<<(val-1);
	}
	else 
	{
		for(int i=1; i<PROP_PARM_COUNT; i++)
		{
			PROP_STRING_PARM(str, i);

			if (!stricmp(str, "Any"))
				defaults->SpawnMask = 0;
			else if (!stricmp(str, "Fighter"))
				defaults->SpawnMask |= 1;
			else if (!stricmp(str, "Cleric"))
				defaults->SpawnMask |= 2;
			else if (!stricmp(str, "Mage"))
				defaults->SpawnMask |= 4;

		}
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, viewheight, F, PlayerPawn)
{
	PROP_DOUBLE_PARM(z, 0);
	defaults->ViewHeight = z;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, userange, F, PlayerPawn)
{
	PROP_DOUBLE_PARM(z, 0);
	defaults->UseRange = z;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, aircapacity, F, PlayerPawn)
{
	PROP_DOUBLE_PARM(z, 0);
	defaults->AirCapacity = z;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, forwardmove, F_f, PlayerPawn)
{
	PROP_DOUBLE_PARM(m, 0);
	defaults->ForwardMove1 = defaults->ForwardMove2 = m;
	if (PROP_PARM_COUNT > 1)
	{
		PROP_DOUBLE_PARM(m2, 1);
		defaults->ForwardMove2 = m2;
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, sidemove, F_f, PlayerPawn)
{
	PROP_DOUBLE_PARM(m, 0);
	defaults->SideMove1 = defaults->SideMove2 = m;
	if (PROP_PARM_COUNT > 1)
	{
		PROP_DOUBLE_PARM(m2, 1);
		defaults->SideMove2 = m2;
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, maxhealth, I, PlayerPawn)
{
	PROP_INT_PARM(z, 0);
	defaults->MaxHealth = z;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, mugshotmaxhealth, I, PlayerPawn)
{
	PROP_INT_PARM(z, 0);
	defaults->MugShotMaxHealth = z;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, runhealth, I, PlayerPawn)
{
	PROP_INT_PARM(z, 0);
	defaults->RunHealth = z;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, morphweapon, S, PlayerPawn)
{
	PROP_STRING_PARM(z, 0);
	defaults->MorphWeapon = FName(z);
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, flechettetype, S, PlayerPawn)
{
	PROP_STRING_PARM(str, 0);
	defaults->FlechetteType = FindClassTentative(str, PClass::FindActor("ArtiPoisonBag"));
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, scoreicon, S, PlayerPawn)
{
	PROP_STRING_PARM(z, 0);
	defaults->ScoreIcon = TexMan.CheckForTexture(z, FTexture::TEX_MiscPatch);
	if (!defaults->ScoreIcon.isValid())
	{
		bag.ScriptPosition.Message(MSG_WARNING,
			"Icon '%s' for '%s' not found\n", z, info->TypeName.GetChars ());
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, crouchsprite, S, PlayerPawn)
{
	PROP_STRING_PARM(z, 0);
	if (strlen(z) == 4)
	{
		defaults->crouchsprite = GetSpriteIndex (z);
	}
	else if (*z == 0)
	{
		defaults->crouchsprite = 0;
	}
	else
	{
		I_Error("Sprite name must have exactly 4 characters");
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, damagescreencolor, Cfs, PlayerPawn)
{
	PROP_COLOR_PARM(c, 0);

	PalEntry color = c;

	if (PROP_PARM_COUNT < 3)		// Because colors count as 2 parms
	{
		color.a = 255;
		defaults->DamageFade = color;
	}
	else if (PROP_PARM_COUNT < 4)
	{
		PROP_DOUBLE_PARM(a, 2);

		color.a = BYTE(255 * clamp<double>(a, 0.f, 1.f));
		defaults->DamageFade = color;
	}
	else
	{
		PROP_DOUBLE_PARM(a, 2);
		PROP_STRING_PARM(type, 3);

		color.a = BYTE(255 * clamp<double>(a, 0.f, 1.f));
		assert(info->IsKindOf(RUNTIME_CLASS(PClassPlayerPawn)));
		static_cast<PClassPlayerPawn *>(info)->PainFlashes.Insert(type, color);
	}
}

//==========================================================================
//
// [GRB] Store start items in drop item list
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, startitem, S_i, PlayerPawn)
{
	PROP_STRING_PARM(str, 0);

	// create a linked list of startitems
	if (!bag.DropItemSet)
	{
		bag.DropItemSet = true;
		bag.DropItemList = NULL;
	}

	DDropItem *di = new DDropItem;

	di->Name = str;
	di->Probability = 255;
	di->Amount = 1;
	if (PROP_PARM_COUNT > 1)
	{
		PROP_INT_PARM(amt, 1);
		di->Amount = amt;
	}
	di->Next = bag.DropItemList;
	bag.DropItemList = di;
	GC::WriteBarrier(di);
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, invulnerabilitymode, S, PlayerPawn)
{
	PROP_STRING_PARM(str, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassPlayerPawn)));
	static_cast<PClassPlayerPawn *>(info)->InvulMode = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, healradiustype, S, PlayerPawn)
{
	PROP_STRING_PARM(str, 0);
	assert(info->IsKindOf(RUNTIME_CLASS(PClassPlayerPawn)));
	static_cast<PClassPlayerPawn *>(info)->HealingRadiusType = str;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, hexenarmor, FFFFF, PlayerPawn)
{
	assert(info->IsKindOf(RUNTIME_CLASS(PClassPlayerPawn)));
	for (int i = 0; i < 5; i++)
	{
		PROP_DOUBLE_PARM(val, i);
		static_cast<PClassPlayerPawn *>(info)->HexenArmor[i] = val;
	}
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, portrait, S, PlayerPawn)
{
	assert(info->IsKindOf(RUNTIME_CLASS(PClassPlayerPawn)));
	PROP_STRING_PARM(val, 0);
	static_cast<PClassPlayerPawn *>(info)->Portrait = val;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, weaponslot, ISsssssssssssssssssssssssssssssssssssssssssss, PlayerPawn)
{
	PROP_INT_PARM(slot, 0);

	assert(info->IsKindOf(RUNTIME_CLASS(PClassPlayerPawn)));
	if (slot < 0 || slot > 9)
	{
		I_Error("Slot must be between 0 and 9.");
	}
	else
	{
		FString weapons;

		for(int i = 1; i < PROP_PARM_COUNT; ++i)
		{
			PROP_STRING_PARM(str, i);
			weapons << ' ' << str;
		}
		static_cast<PClassPlayerPawn *>(info)->Slot[slot] = &weapons[1];
	}
}

//==========================================================================
//
// [SP] Player.Viewbob
//
//==========================================================================
DEFINE_CLASS_PROPERTY_PREFIX(player, viewbob, F, PlayerPawn)
{
	PROP_DOUBLE_PARM(z, 0);
	// [SP] Hard limits. This is to prevent terrywads from making players sick.
	//   Remember - this messes with a user option who probably has it set a
	//   certain way for a reason. I think a 1.5 limit is pretty generous, but
	//   it may be safe to increase it. I really need opinions from people who
	//   could be affected by this.
	if (z < 0.0 || z > 1.5)
	{
		I_Error("ViewBob must be between 0.0 and 1.5.");
	}
	defaults->ViewBob = z;
}

//==========================================================================
// (non-fatal with non-existent types only in DECORATE)
//==========================================================================
DEFINE_CLASS_PROPERTY(playerclass, S, MorphProjectile)
{
	PROP_STRING_PARM(str, 0);
	defaults->PlayerClass = FindClassTentativePlayerPawn(str, bag.fromDecorate);
}

//==========================================================================
// (non-fatal with non-existent types only in DECORATE)
//==========================================================================
DEFINE_CLASS_PROPERTY(monsterclass, S, MorphProjectile)
{
	PROP_STRING_PARM(str, 0);
	defaults->MonsterClass = FindClassTentative(str, RUNTIME_CLASS(AActor), bag.fromDecorate);
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(duration, I, MorphProjectile)
{
	PROP_INT_PARM(i, 0);
	defaults->Duration = i >= 0 ? i : -i*TICRATE;
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(morphstyle, M, MorphProjectile)
{
	PROP_INT_PARM(i, 0);
	defaults->MorphStyle = i;
}

//==========================================================================
// (non-fatal with non-existent types only in DECORATE)
//==========================================================================
DEFINE_CLASS_PROPERTY(morphflash, S, MorphProjectile)
{
	PROP_STRING_PARM(str, 0);
	defaults->MorphFlash = FindClassTentative(str, RUNTIME_CLASS(AActor), bag.fromDecorate);
}

//==========================================================================
//
//==========================================================================
DEFINE_CLASS_PROPERTY(unmorphflash, S, MorphProjectile)
{
	PROP_STRING_PARM(str, 0);
	defaults->UnMorphFlash = FindClassTentative(str, RUNTIME_CLASS(AActor), bag.fromDecorate);
}

//==========================================================================
// (non-fatal with non-existent types only in DECORATE)
//==========================================================================
DEFINE_SCRIPTED_PROPERTY(playerclass, S, PowerMorph)
{
	PROP_STRING_PARM(str, 0);
	TypedScriptVar<PClassActor*>(defaults, bag.Info, NAME_PlayerClass, NewClassPointer(RUNTIME_CLASS(APlayerPawn))) = FindClassTentativePlayerPawn(str, bag.fromDecorate);
}

//==========================================================================
//
//==========================================================================
DEFINE_SCRIPTED_PROPERTY(morphstyle, M, PowerMorph)
{
	PROP_INT_PARM(i, 0);
	TypedScriptVar<int>(defaults, bag.Info, NAME_MorphStyle, TypeSInt32) = i;
}

//==========================================================================
// (non-fatal with non-existent types only in DECORATE)
//==========================================================================
DEFINE_SCRIPTED_PROPERTY(morphflash, S, PowerMorph)
{
	PROP_STRING_PARM(str, 0);
	TypedScriptVar<PClassActor*>(defaults, bag.Info, NAME_MorphFlash, NewClassPointer(RUNTIME_CLASS(AActor))) = FindClassTentative(str, RUNTIME_CLASS(AActor), bag.fromDecorate);
}

//==========================================================================
// (non-fatal with non-existent types only in DECORATE)
//==========================================================================
DEFINE_SCRIPTED_PROPERTY(unmorphflash, S, PowerMorph)
{
	PROP_STRING_PARM(str, 0);
	TypedScriptVar<PClassActor*>(defaults, bag.Info, NAME_UnMorphFlash, NewClassPointer(RUNTIME_CLASS(AActor))) = FindClassTentative(str, RUNTIME_CLASS(AActor), bag.fromDecorate);
}


