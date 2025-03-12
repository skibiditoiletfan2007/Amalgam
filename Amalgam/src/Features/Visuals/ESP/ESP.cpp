#include "ESP.h"

#include "../../Players/PlayerUtils.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"

MAKE_SIGNATURE(CTFPlayerSharedUtils_GetEconItemViewByLoadoutSlot, "client.dll", "48 89 6C 24 ? 56 41 54 41 55 41 56 41 57 48 83 EC", 0x0);
MAKE_SIGNATURE(CEconItemView_GetItemName, "client.dll", "40 53 48 83 EC ? 48 8B D9 C6 81 ? ? ? ? ? E8 ? ? ? ? 48 8B 8B", 0x0);

void CESP::Store(CTFPlayer* pLocal)
{
	m_mPlayerCache.clear();
	m_mBuildingCache.clear();
	m_mWorldCache.clear();

	if (!Vars::ESP::Draw.Value || !pLocal)
		return;

	StorePlayers(pLocal);
	StoreBuildings(pLocal);
	StoreProjectiles(pLocal);
	StoreObjective(pLocal);
	StoreWorld();
}

void CESP::StorePlayers(CTFPlayer* pLocal)
{
	if (!(Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::Players) || !Vars::ESP::Player.Value)
		return;

	auto pResource = H::Entities.GetPR();
	for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ALL))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		int iIndex = pPlayer->entindex();

		if (pLocal->m_iObserverMode() == OBS_MODE_FIRSTPERSON ? pLocal->m_hObserverTarget().Get() == pPlayer : iIndex == I::EngineClient->GetLocalPlayer())
		{
			if (!(Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Local) || !I::Input->CAM_IsThirdPerson())
				continue;
		}
		else
		{
			if (!pPlayer->IsAlive() || pPlayer->IsAGhost())
				continue;

			if (pPlayer->IsDormant())
			{
				if (!H::Entities.GetDormancy(iIndex) || !Vars::ESP::DormantAlpha.Value
					|| Vars::ESP::DormantPriority.Value && !F::PlayerUtils.IsPrioritized(iIndex))
					continue;
			}

			if (!(Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Prioritized && F::PlayerUtils.IsPrioritized(iIndex))
				&& !(Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Friends && H::Entities.IsFriend(iIndex))
				&& !(Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Party && H::Entities.InParty(iIndex))
				&& pPlayer->m_iTeamNum() == pLocal->m_iTeamNum() ? !(Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Team) : !(Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Enemy))
				continue;
		}

		int iClassNum = pPlayer->m_iClass();
		auto pWeapon = pPlayer->m_hActiveWeapon().Get()->As<CTFWeaponBase>();

		PlayerCache& tCache = m_mPlayerCache[pEntity];
		tCache.m_flAlpha = (pPlayer->IsDormant() ? Vars::ESP::DormantAlpha.Value : Vars::ESP::ActiveAlpha.Value) / 255.f;
		tCache.m_tColor = H::Color.GetTeamColor(pLocal->m_iTeamNum(), pPlayer->m_iTeamNum(), Vars::Colors::Relative.Value);
		tCache.m_bBox = Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Box;
		tCache.m_bBones = Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Bones;

		if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Distance && pPlayer != pLocal)
		{
			Vec3 vDelta = pPlayer->m_vecOrigin() - pLocal->m_vecOrigin();
			tCache.m_vText.push_back({ ESPTextEnum::Bottom, std::format("[{:.0f}M]", vDelta.Length2D() / 41), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
		}

		PlayerInfo_t pi{};
		if (I::EngineClient->GetPlayerInfo(iIndex, &pi))
		{
			if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Name)
				tCache.m_vText.push_back({ ESPTextEnum::Top, F::PlayerUtils.GetPlayerName(iIndex, pi.name), H::Color.GetEntityNameColor(pLocal, pPlayer, Vars::Colors::Relative.Value), Vars::Menu::Theme::Background.Value });

			//std::vector<PriorityLabel_t> vTags = {};
			if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Priority)
			{
				if (auto pTag = F::PlayerUtils.GetSignificantTag(pi.friendsID, 1)) // 50 alpha as white outline tends to be more apparent
					tCache.m_vText.push_back({ ESPTextEnum::Top, pTag->Name, pTag->Color, H::Draw.IsColorDark(pTag->Color) ? Color_t(255, 255, 255, 50) : Color_t(0, 0, 0, 255) });
			}

			if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Labels)
			{
				std::vector<PriorityLabel_t*> vTags = {};
				for (auto& iID : F::PlayerUtils.m_mPlayerTags[pi.friendsID])
				{
					auto pTag = F::PlayerUtils.GetTag(iID);
					if (pTag && pTag->Label)
						vTags.push_back(pTag);
				}
				if (H::Entities.IsFriend(iIndex))
				{
					auto pTag = &F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(FRIEND_TAG)];
					if (pTag->Label)
						vTags.push_back(pTag);
				}
				if (H::Entities.InParty(iIndex))
				{
					auto pTag = &F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(PARTY_TAG)];
					if (pTag->Label)
						vTags.push_back(pTag);
				}

				if (vTags.size())
				{
					std::sort(vTags.begin(), vTags.end(), [&](const auto a, const auto b) -> bool
						{
							// sort by priority if unequal
							if (a->Priority != b->Priority)
								return a->Priority > b->Priority;

							return a->Name < b->Name;
						});

					for (auto& pTag : vTags) // 50 alpha as white outline tends to be more apparent
						tCache.m_vText.push_back({ ESPTextEnum::Top, pTag->Name, pTag->Color, H::Draw.IsColorDark(pTag->Color) ? Color_t(255, 255, 255, 50) : Color_t(0, 0, 0, 255) });
				}
			}
		}

		float flHealth = pPlayer->m_iHealth(), flMaxHealth = pPlayer->GetMaxHealth();
		if (tCache.m_bHealthBar = Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::HealthBar)
		{
			if (flHealth > flMaxHealth)
			{
				float flMaxOverheal = floorf(flMaxHealth / 10.f) * 5;
				tCache.m_flHealth = 1.f + std::clamp((flHealth - flMaxHealth) / flMaxOverheal, 0.f, 1.f);
			}
			else
				tCache.m_flHealth = std::clamp(flHealth / flMaxHealth, 0.f, 1.f);
		}

		// Health Text Display (Show normal health when not overhealed, otherwise show overheal)
		if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::HealthText)
		{
			int overhealAmount = flHealth - flMaxHealth;
			if (overhealAmount > 0)
			{
				// Show only the overheal amount if the player is overhealed
				tCache.m_vText.push_back({ ESPTextEnum::Health, std::format("+{}", overhealAmount), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
			}
			else
			{
				// Show normal health if not overhealed
				tCache.m_vText.push_back({ ESPTextEnum::Health, std::format("{}", flHealth), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
			}
		}

		if (iClassNum == TF_CLASS_MEDIC)
		{
			auto pMediGun = pPlayer->GetWeaponFromSlot(SLOT_SECONDARY);
			if (pMediGun && pMediGun->GetClassID() == ETFClassID::CWeaponMedigun)
			{
				tCache.m_flUber = std::clamp(pMediGun->As<CWeaponMedigun>()->m_flChargeLevel(), 0.f, 1.f);
				tCache.m_bUberBar = Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::UberBar;
				if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::UberText)
					tCache.m_vText.push_back({ ESPTextEnum::Uber, std::format("{:.0f}%%", tCache.m_flUber * 100.f), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
			}
		}

		if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::ClassIcon)
			tCache.m_iClassIcon = iClassNum;
		if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::ClassText)
			tCache.m_vText.push_back({ ESPTextEnum::Right, GetPlayerClass(iClassNum), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

		if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::WeaponIcon && pWeapon)
			tCache.m_pWeaponIcon = pWeapon->GetWeaponIcon();
		if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::WeaponText && pWeapon)
		{
			int iWeaponSlot = pWeapon->GetSlot();
			switch (pPlayer->m_iClass())
			{
			case TF_CLASS_SPY:
			{
				switch (iWeaponSlot)
				{
				case 0: iWeaponSlot = 1; break;
				case 1: iWeaponSlot = 4; break;
				case 3: iWeaponSlot = 5; break;
				}
				break;
			}
			case TF_CLASS_ENGINEER:
			{
				switch (iWeaponSlot)
				{
				case 3: iWeaponSlot = 5; break;
				case 4: iWeaponSlot = 6; break;
				}
				break;
			}
			}

			if (void* pCurItemData = S::CTFPlayerSharedUtils_GetEconItemViewByLoadoutSlot.Call<void*>(pPlayer, iWeaponSlot, nullptr))
				tCache.m_vText.push_back({ ESPTextEnum::Bottom, SDK::ConvertWideToUTF8(S::CEconItemView_GetItemName.Call<const wchar_t*>(pCurItemData)), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
		}

		if (Vars::Debug::Info.Value && !pPlayer->IsDormant() && pPlayer->entindex() != I::EngineClient->GetLocalPlayer())
		{
			int iAverage = TIME_TO_TICKS(F::MoveSim.GetPredictedDelta(pPlayer));
			int iCurrent = H::Entities.GetChoke(pPlayer->entindex());
			tCache.m_vText.push_back({ ESPTextEnum::Right, std::format("LAG {}, {}", iAverage, iCurrent), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
		}

		{
			if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::LagCompensation && !pPlayer->IsDormant() && pPlayer != pLocal)
			{
				if (H::Entities.GetLagCompensation(pPlayer->entindex()))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "LAGCOMP", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
			}

			if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Ping && pResource && pPlayer != pLocal)
			{
				auto pNetChan = I::EngineClient->GetNetChannelInfo();
				if (pNetChan && !pNetChan->IsLoopback())
				{
					int iPing = pResource->GetPing(pPlayer->entindex());
					if (iPing && (iPing >= 200 || iPing <= 5))
						tCache.m_vText.push_back({ ESPTextEnum::Right, std::format("{}MS", iPing), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				}
			}

			if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::KDR && pResource && pPlayer != pLocal)
			{
				int iKills = pResource->GetKills(pPlayer->entindex()), iDeaths = pResource->GetDeaths(pPlayer->entindex());
				if (iKills >= 20)
				{
					int iKDR = iKills / std::max(iDeaths, 1);
					if (iKDR >= 10)
						tCache.m_vText.push_back({ ESPTextEnum::Right, std::format("HIGH KD [{} / {}]", iKills, iDeaths), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				}
			}

			// Buffs
			if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Buffs)
			{
				bool bCrits = false, bMiniCrits = false;
				if (pPlayer->IsCritBoosted())
					pWeapon&& pWeapon->GetWeaponID() == TF_WEAPON_PARTICLE_CANNON ? bMiniCrits = true : bCrits = true;
				if (pPlayer->IsMiniCritBoosted())
					pWeapon&& pWeapon->m_iItemDefinitionIndex() == Sniper_t_TheBushwacka ? bCrits = true : bMiniCrits = true;
				if (pWeapon && pWeapon->m_iItemDefinitionIndex() == Soldier_t_TheMarketGardener && pPlayer->InCond(TF_COND_BLASTJUMPING))
					bCrits = true, bMiniCrits = false;

				if (bCrits)
					tCache.m_vText.push_back({ ESPTextEnum::Right, "CRITS", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				else if (bMiniCrits)
					tCache.m_vText.push_back({ ESPTextEnum::Right, "MINI-CRITS", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				if (pPlayer->InCond(TF_COND_RADIUSHEAL) ||
					pPlayer->InCond(TF_COND_HEALTH_BUFF) ||
					pPlayer->InCond(TF_COND_RADIUSHEAL_ON_DAMAGE) ||
					pPlayer->InCond(TF_COND_MEGAHEAL) ||
					pPlayer->InCond(TF_COND_HALLOWEEN_QUICK_HEAL) ||
					pPlayer->InCond(TF_COND_HALLOWEEN_HELL_HEAL) ||
					pPlayer->InCond(TF_COND_KING_BUFFED))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "HP+", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				else if (pPlayer->InCond(TF_COND_HEALTH_OVERHEALED))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "HP", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				if (pPlayer->InCond(TF_COND_INVULNERABLE) ||
					pPlayer->InCond(TF_COND_INVULNERABLE_HIDE_UNLESS_DAMAGED) ||
					pPlayer->InCond(TF_COND_INVULNERABLE_USER_BUFF) ||
					pPlayer->InCond(TF_COND_INVULNERABLE_CARD_EFFECT))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "UBER", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				else if (pPlayer->InCond(TF_COND_PHASE))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "BONK", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				/* vaccinator effects */
				if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BULLET_RESIST) || pPlayer->InCond(TF_COND_BULLET_IMMUNE))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "BULLET+", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				else if (pPlayer->InCond(TF_COND_MEDIGUN_SMALL_BULLET_RESIST))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "BULLET", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_BLAST_RESIST) || pPlayer->InCond(TF_COND_BLAST_IMMUNE))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "BLAST+", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				else if (pPlayer->InCond(TF_COND_MEDIGUN_SMALL_BLAST_RESIST))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "BLAST", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_MEDIGUN_UBER_FIRE_RESIST) || pPlayer->InCond(TF_COND_FIRE_IMMUNE))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "FIRE+", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				else if (pPlayer->InCond(TF_COND_MEDIGUN_SMALL_FIRE_RESIST))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "FIRE", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				if (pPlayer->InCond(TF_COND_OFFENSEBUFF))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "BANNER", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_DEFENSEBUFF))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "BATTALIONS", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_REGENONDAMAGEBUFF))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "CONCH", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				//if (pPlayer->InCond(TF_COND_BLASTJUMPING))
					//tCache.m_vText.push_back({ ESPTextEnum::Right, "BLASTJUMP", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				if (pPlayer->InCond(TF_COND_RUNE_STRENGTH))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "STRENGHT", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_RUNE_HASTE))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "HASTE", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_RUNE_REGEN))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "REGEN", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_RUNE_RESIST))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "RESISTANCE", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_RUNE_VAMPIRE))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "VAMPIRE", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_RUNE_REFLECT))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "REFLECT", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_RUNE_PRECISION))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "PRECISION", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_RUNE_AGILITY))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "AGILITY", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_RUNE_KNOCKOUT))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "KNOCKOUT", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_RUNE_KING))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "KING", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_RUNE_PLAGUE))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "PLAGUE", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_RUNE_SUPERNOVA))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "SUPERNOVA", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (pPlayer->InCond(TF_COND_POWERUPMODE_DOMINANT))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "DOMINANT", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
			}

			// Debuffs
			if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Debuffs)
			{
				if (pPlayer->InCond(TF_COND_MARKEDFORDEATH) || pPlayer->InCond(TF_COND_MARKEDFORDEATH_SILENT))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "MARKED", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				if (pPlayer->InCond(TF_COND_URINE))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "JARATE", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				if (pPlayer->InCond(TF_COND_MAD_MILK))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "MILK", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				if (pPlayer->InCond(TF_COND_STUNNED))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "STUN", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				if (pPlayer->InCond(TF_COND_BURNING))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "BURN", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				if (pPlayer->InCond(TF_COND_BLEEDING))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "BLEED", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
			}

			// Misc
			if (Vars::ESP::Player.Value & Vars::ESP::PlayerEnum::Misc)
			{
				if (Vars::Visuals::Removals::Taunts.Value && pPlayer->InCond(TF_COND_TAUNTING))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "TAUNT", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				if (pPlayer->m_bFeignDeathReady())
					tCache.m_vText.push_back({ ESPTextEnum::Right, "DR", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				if (pPlayer->InCond(TF_COND_AIMING) || pPlayer->InCond(TF_COND_ZOOMED))
				{
					switch (pWeapon ? pWeapon->GetWeaponID() : -1)
					{
					case TF_WEAPON_MINIGUN:
						tCache.m_vText.push_back({ ESPTextEnum::Right, "REV", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
						break;
					case TF_WEAPON_SNIPERRIFLE:
					case TF_WEAPON_SNIPERRIFLE_CLASSIC:
					case TF_WEAPON_SNIPERRIFLE_DECAP:
					{
						if (iIndex == I::EngineClient->GetLocalPlayer())
						{
							tCache.m_vText.push_back({ ESPTextEnum::Right, std::format("CHARGING {:.0f}%%", Math::RemapValClamped(pWeapon->As<CTFSniperRifle>()->m_flChargedDamage(), 0.f, 150.f, 0.f, 100.f)), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
							break;
						}
						else
						{
							CSniperDot* pPlayerDot = nullptr;
							for (auto pDot : H::Entities.GetGroup(EGroupType::MISC_DOTS))
							{
								if (pDot->m_hOwnerEntity().Get() == pEntity)
								{
									pPlayerDot = pDot->As<CSniperDot>();
									break;
								}
							}
							if (pPlayerDot)
							{
								float flChargeTime = std::max(SDK::AttribHookValue(3.f, "mult_sniper_charge_per_sec", pWeapon), 1.5f);
								tCache.m_vText.push_back({ ESPTextEnum::Right, std::format("CHARGING {:.0f}%%", Math::RemapValClamped(TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick) - pPlayerDot->m_flChargeStartTime() - 0.3f, 0.f, flChargeTime, 0.f, 100.f)), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
								break;
							}
						}
						tCache.m_vText.push_back({ ESPTextEnum::Right, "CHARGING", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
						break;
					}
					case TF_WEAPON_COMPOUND_BOW:
						if (iIndex == I::EngineClient->GetLocalPlayer())
						{
							tCache.m_vText.push_back({ ESPTextEnum::Right, std::format("CHARGING {:.0f}%%", Math::RemapValClamped(TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick) - pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime(), 0.f, 1.f, 0.f, 100.f)), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
							break;
						}
						tCache.m_vText.push_back({ ESPTextEnum::Right, "CHARGING", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
						break;
					default:
						tCache.m_vText.push_back({ ESPTextEnum::Right, "CHARGING", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
					}
				}

				if (pPlayer->InCond(TF_COND_SHIELD_CHARGE))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "CHARGE", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				if (pPlayer->InCond(TF_COND_STEALTHED) || pPlayer->InCond(TF_COND_STEALTHED_BLINK) || pPlayer->InCond(TF_COND_STEALTHED_USER_BUFF) || pPlayer->InCond(TF_COND_STEALTHED_USER_BUFF_FADING))
					tCache.m_vText.push_back({ ESPTextEnum::Right, std::format("INVIS {:.0f}%%", pPlayer->GetInvisPercentage()), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

				if (pPlayer->InCond(TF_COND_DISGUISING) || pPlayer->InCond(TF_COND_DISGUISED))
					tCache.m_vText.push_back({ ESPTextEnum::Right, "DISGUISE", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
			}
		}
	}
}

void CESP::StoreBuildings(CTFPlayer* pLocal)
{
	if (!(Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::Buildings) || !Vars::ESP::Building.Value)
		return;

	for (auto pEntity : H::Entities.GetGroup(EGroupType::BUILDINGS_ALL))
	{
		auto pBuilding = pEntity->As<CBaseObject>();
		auto pOwner = pBuilding->m_hBuilder().Get();
		int iIndex = pOwner ? pOwner->entindex() : 0;

		if (pOwner)
		{
			if (iIndex == I::EngineClient->GetLocalPlayer())
			{
				if (!(Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Local))
					continue;
			}
			else
			{
				if (!(Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Prioritized && F::PlayerUtils.IsPrioritized(iIndex))
					&& !(Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Friends && H::Entities.IsFriend(iIndex))
					&& !(Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Party && H::Entities.InParty(iIndex))
					&& pOwner->m_iTeamNum() == pLocal->m_iTeamNum() ? !(Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Team) : !(Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Enemy))
					continue;
			}
		}
		else if (pEntity->m_iTeamNum() == pLocal->m_iTeamNum() ? !(Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Team) : !(Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Enemy))
			continue;

		bool bIsMini = pBuilding->m_bMiniBuilding();

		BuildingCache& tCache = m_mBuildingCache[pEntity];
		tCache.m_flAlpha = Vars::ESP::ActiveAlpha.Value / 255.f;
		tCache.m_tColor = H::Color.GetTeamColor(pLocal->m_iTeamNum(), (pOwner ? pOwner : pEntity)->m_iTeamNum(), Vars::Colors::Relative.Value);
		tCache.m_bBox = Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Box;

		if (Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Distance)
		{
			Vec3 vDelta = pEntity->m_vecOrigin() - pLocal->m_vecOrigin();
			tCache.m_vText.push_back({ ESPTextEnum::Bottom, std::format("[{:.0f}M]", vDelta.Length2D() / 41), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
		}

		if (Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Name)
		{
			const char* szName = "Building";
			switch (pEntity->GetClassID())
			{
			case ETFClassID::CObjectSentrygun: szName = bIsMini ? "Mini-Sentry" : "Sentry"; break;
			case ETFClassID::CObjectDispenser: szName = "Dispenser"; break;
			case ETFClassID::CObjectTeleporter: szName = pBuilding->m_iObjectMode() ? "Teleporter Exit" : "Teleporter Entrance";
			}
			tCache.m_vText.push_back({ ESPTextEnum::Top, szName, H::Color.GetEntityNameColor(pLocal, (pOwner ? pOwner : pEntity), Vars::Colors::Relative.Value), Vars::Menu::Theme::Background.Value });
		}

		float flHealth = pBuilding->m_iHealth(), flMaxHealth = pBuilding->m_iMaxHealth();
		if (tCache.m_bHealthBar = Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::HealthBar)
			tCache.m_flHealth = std::clamp(flHealth / flMaxHealth, 0.f, 1.f);
		if (Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::HealthText)
			tCache.m_vText.push_back({ ESPTextEnum::Health, std::format("{}", flHealth), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

		if (Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Owner && !pBuilding->m_bWasMapPlaced() && pOwner)
		{
			PlayerInfo_t pi{};
			if (I::EngineClient->GetPlayerInfo(iIndex, &pi))
				tCache.m_vText.push_back({ ESPTextEnum::Top, F::PlayerUtils.GetPlayerName(iIndex, pi.name), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
		}

		if (Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Level && !bIsMini)
			tCache.m_vText.push_back({ ESPTextEnum::Right, std::format("{} / {}", pBuilding->m_iUpgradeLevel(), bIsMini ? 1 : 3), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

		if (Vars::ESP::Building.Value & Vars::ESP::BuildingEnum::Flags)
		{
			float flConstructed = pBuilding->m_flPercentageConstructed();
			if (flConstructed < 1.f)
				tCache.m_vText.push_back({ ESPTextEnum::Right, std::format("{:.0f}%%", flConstructed * 100.f), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

			if (pBuilding->IsSentrygun() && pBuilding->As<CObjectSentrygun>()->m_bPlayerControlled())
				tCache.m_vText.push_back({ ESPTextEnum::Right, "WRANGLED", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

			if (pBuilding->m_bHasSapper())
				tCache.m_vText.push_back({ ESPTextEnum::Right, "SAPPED", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
			else if (pBuilding->m_bDisabled())
				tCache.m_vText.push_back({ ESPTextEnum::Right, "DISABLED", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });

			if (pBuilding->IsSentrygun() && !pBuilding->m_bBuilding())
			{
				int iShells, iMaxShells, iRockets, iMaxRockets; pBuilding->As<CObjectSentrygun>()->GetAmmoCount(iShells, iMaxShells, iRockets, iMaxRockets);
				if (!iShells)
					tCache.m_vText.push_back({ ESPTextEnum::Right, "NO AMMO", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
				if (!bIsMini && !iRockets)
					tCache.m_vText.push_back({ ESPTextEnum::Right, "NO ROCKETS", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
			}
		}
	}
}

void CESP::StoreProjectiles(CTFPlayer* pLocal)
{
	if (!(Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::Projectiles) || !Vars::ESP::Projectile.Value)
		return;

	for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_PROJECTILES))
	{
		CBaseEntity* pOwner = nullptr;
		switch (pEntity->GetClassID())
		{
		case ETFClassID::CBaseProjectile:
		case ETFClassID::CBaseGrenade:
		case ETFClassID::CTFWeaponBaseGrenadeProj:
		case ETFClassID::CTFWeaponBaseMerasmusGrenade:
		case ETFClassID::CTFGrenadePipebombProjectile:
		case ETFClassID::CTFStunBall:
		case ETFClassID::CTFBall_Ornament:
		case ETFClassID::CTFProjectile_Jar:
		case ETFClassID::CTFProjectile_Cleaver:
		case ETFClassID::CTFProjectile_JarGas:
		case ETFClassID::CTFProjectile_JarMilk:
		case ETFClassID::CTFProjectile_SpellBats:
		case ETFClassID::CTFProjectile_SpellKartBats:
		case ETFClassID::CTFProjectile_SpellMeteorShower:
		case ETFClassID::CTFProjectile_SpellMirv:
		case ETFClassID::CTFProjectile_SpellPumpkin:
		case ETFClassID::CTFProjectile_SpellSpawnBoss:
		case ETFClassID::CTFProjectile_SpellSpawnHorde:
		case ETFClassID::CTFProjectile_SpellSpawnZombie:
		case ETFClassID::CTFProjectile_SpellTransposeTeleport:
		case ETFClassID::CTFProjectile_Throwable:
		case ETFClassID::CTFProjectile_ThrowableBreadMonster:
		case ETFClassID::CTFProjectile_ThrowableBrick:
		case ETFClassID::CTFProjectile_ThrowableRepel:
		{
			pOwner = pEntity->As<CTFWeaponBaseGrenadeProj>()->m_hThrower().Get();
			break;
		}
		case ETFClassID::CTFBaseRocket:
		case ETFClassID::CTFFlameRocket:
		case ETFClassID::CTFProjectile_Arrow:
		case ETFClassID::CTFProjectile_GrapplingHook:
		case ETFClassID::CTFProjectile_HealingBolt:
		case ETFClassID::CTFProjectile_Rocket:
		case ETFClassID::CTFProjectile_BallOfFire:
		case ETFClassID::CTFProjectile_MechanicalArmOrb:
		case ETFClassID::CTFProjectile_SentryRocket:
		case ETFClassID::CTFProjectile_SpellFireball:
		case ETFClassID::CTFProjectile_SpellLightningOrb:
		case ETFClassID::CTFProjectile_SpellKartOrb:
		case ETFClassID::CTFProjectile_EnergyBall:
		case ETFClassID::CTFProjectile_Flare:
		{
			auto pWeapon = pEntity->As<CTFBaseRocket>()->m_hLauncher().Get();
			pOwner = pWeapon ? pWeapon->As<CTFWeaponBase>()->m_hOwner().Get() : nullptr;
			break;
		}
		case ETFClassID::CTFBaseProjectile:
		case ETFClassID::CTFProjectile_EnergyRing:
			//case ETFClassID::CTFProjectile_Syringe:
		{
			auto pWeapon = pEntity->As<CTFBaseProjectile>()->m_hLauncher().Get();
			pOwner = pWeapon ? pWeapon->As<CTFWeaponBase>()->m_hOwner().Get() : nullptr;
			break;
		}
		}

		if (pOwner)
		{
			int iIndex = pOwner->entindex();

			if (iIndex == I::EngineClient->GetLocalPlayer())
			{
				if (!(Vars::ESP::Projectile.Value & Vars::ESP::ProjectileEnum::Local))
					continue;
			}
			else
			{
				if (!(Vars::ESP::Projectile.Value & Vars::ESP::ProjectileEnum::Prioritized && F::PlayerUtils.IsPrioritized(iIndex))
					&& !(Vars::ESP::Projectile.Value & Vars::ESP::ProjectileEnum::Friends && H::Entities.IsFriend(iIndex))
					&& !(Vars::ESP::Projectile.Value & Vars::ESP::ProjectileEnum::Party && H::Entities.InParty(iIndex))
					&& pOwner->m_iTeamNum() == pLocal->m_iTeamNum() ? !(Vars::ESP::Projectile.Value & Vars::ESP::ProjectileEnum::Team) : !(Vars::ESP::Projectile.Value & Vars::ESP::ProjectileEnum::Enemy))
					continue;
			}
		}
		else if (pEntity->m_iTeamNum() == pLocal->m_iTeamNum() ? !(Vars::ESP::Projectile.Value & Vars::ESP::ProjectileEnum::Team) : !(Vars::ESP::Projectile.Value & Vars::ESP::ProjectileEnum::Enemy))
			continue;

		WorldCache& tCache = m_mWorldCache[pEntity];
		tCache.m_flAlpha = Vars::ESP::ActiveAlpha.Value / 255.f;
		tCache.m_tColor = H::Color.GetTeamColor(pLocal->m_iTeamNum(), (pOwner ? pOwner : pEntity)->m_iTeamNum(), Vars::Colors::Relative.Value);
		tCache.m_bBox = Vars::ESP::Projectile.Value & Vars::ESP::ProjectileEnum::Box;

		if (Vars::ESP::Projectile.Value & Vars::ESP::ProjectileEnum::Distance)
		{
			Vec3 vDelta = pEntity->m_vecOrigin() - pLocal->m_vecOrigin();
			tCache.m_vText.push_back({ ESPTextEnum::Bottom, std::format("[{:.0f}M]", vDelta.Length2D() / 41), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
		}

		if (Vars::ESP::Projectile.Value & Vars::ESP::ProjectileEnum::Name)
		{
			const char* szName = "Projectile";
			switch (pEntity->GetClassID())
			{
				//case ETFClassID::CBaseProjectile:
				//case ETFClassID::CBaseGrenade:
				//case ETFClassID::CTFWeaponBaseGrenadeProj:
			case ETFClassID::CTFWeaponBaseMerasmusGrenade: szName = "Bomb"; break;
			case ETFClassID::CTFGrenadePipebombProjectile: szName = pEntity->As<CTFGrenadePipebombProjectile>()->HasStickyEffects() ? "Sticky" : "Pipe"; break;
			case ETFClassID::CTFStunBall: szName = "Baseball"; break;
			case ETFClassID::CTFBall_Ornament: szName = "Bauble"; break;
			case ETFClassID::CTFProjectile_Jar: szName = "Jarate"; break;
			case ETFClassID::CTFProjectile_Cleaver: szName = "Cleaver"; break;
			case ETFClassID::CTFProjectile_JarGas: szName = "Gas"; break;
			case ETFClassID::CTFProjectile_JarMilk:
			case ETFClassID::CTFProjectile_ThrowableBreadMonster: szName = "Milk"; break;
			case ETFClassID::CTFProjectile_SpellBats:
			case ETFClassID::CTFProjectile_SpellKartBats: szName = "Bats"; break;
			case ETFClassID::CTFProjectile_SpellMeteorShower: szName = "Meteor shower"; break;
			case ETFClassID::CTFProjectile_SpellMirv:
			case ETFClassID::CTFProjectile_SpellPumpkin: szName = "Pumpkin"; break;
			case ETFClassID::CTFProjectile_SpellSpawnBoss: szName = "Monoculus"; break;
			case ETFClassID::CTFProjectile_SpellSpawnHorde:
			case ETFClassID::CTFProjectile_SpellSpawnZombie: szName = "Skeleton"; break;
			case ETFClassID::CTFProjectile_SpellTransposeTeleport: szName = "Teleport"; break;
				//case ETFClassID::CTFProjectile_Throwable:
				//case ETFClassID::CTFProjectile_ThrowableBrick:
				//case ETFClassID::CTFProjectile_ThrowableRepel: szName = "Throwable"; break;
			case ETFClassID::CTFProjectile_Arrow: szName = "Arrow"; break;
			case ETFClassID::CTFProjectile_GrapplingHook: szName = "Grapple"; break;
			case ETFClassID::CTFProjectile_HealingBolt: szName = "Heal"; break;
				//case ETFClassID::CTFBaseRocket:
				//case ETFClassID::CTFFlameRocket:
			case ETFClassID::CTFProjectile_Rocket:
			case ETFClassID::CTFProjectile_EnergyBall:
			case ETFClassID::CTFProjectile_SentryRocket: szName = "Rocket"; break;
			case ETFClassID::CTFProjectile_BallOfFire: szName = "Fire"; break;
			case ETFClassID::CTFProjectile_MechanicalArmOrb: szName = "Short circuit"; break;
			case ETFClassID::CTFProjectile_SpellFireball: szName = "Fireball"; break;
			case ETFClassID::CTFProjectile_SpellLightningOrb: szName = "Lightning"; break;
			case ETFClassID::CTFProjectile_SpellKartOrb: szName = "Fist"; break;
			case ETFClassID::CTFProjectile_Flare: szName = "Flare"; break;
				//case ETFClassID::CTFBaseProjectile:
			case ETFClassID::CTFProjectile_EnergyRing: szName = "Energy"; break;
				//case ETFClassID::CTFProjectile_Syringe: szName = "Syringe";
			}
			tCache.m_vText.push_back({ ESPTextEnum::Top, szName, H::Color.GetEntityNameColor(pLocal, (pOwner ? pOwner : pEntity), Vars::Colors::Relative.Value), Vars::Menu::Theme::Background.Value });
		}

		if (Vars::ESP::Projectile.Value & Vars::ESP::ProjectileEnum::Flags)
		{
			switch (pEntity->GetClassID())
			{
			case ETFClassID::CTFWeaponBaseGrenadeProj:
			case ETFClassID::CTFWeaponBaseMerasmusGrenade:
			case ETFClassID::CTFGrenadePipebombProjectile:
			case ETFClassID::CTFStunBall:
			case ETFClassID::CTFBall_Ornament:
			case ETFClassID::CTFProjectile_Jar:
			case ETFClassID::CTFProjectile_Cleaver:
			case ETFClassID::CTFProjectile_JarGas:
			case ETFClassID::CTFProjectile_JarMilk:
			case ETFClassID::CTFProjectile_SpellBats:
			case ETFClassID::CTFProjectile_SpellKartBats:
			case ETFClassID::CTFProjectile_SpellMeteorShower:
			case ETFClassID::CTFProjectile_SpellMirv:
			case ETFClassID::CTFProjectile_SpellPumpkin:
			case ETFClassID::CTFProjectile_SpellSpawnBoss:
			case ETFClassID::CTFProjectile_SpellSpawnHorde:
			case ETFClassID::CTFProjectile_SpellSpawnZombie:
			case ETFClassID::CTFProjectile_SpellTransposeTeleport:
			case ETFClassID::CTFProjectile_Throwable:
			case ETFClassID::CTFProjectile_ThrowableBreadMonster:
			case ETFClassID::CTFProjectile_ThrowableBrick:
			case ETFClassID::CTFProjectile_ThrowableRepel:
				if (pEntity->As<CTFWeaponBaseGrenadeProj>()->m_bCritical())
					tCache.m_vText.push_back({ ESPTextEnum::Right, "CRIT", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value });
				break;
			case ETFClassID::CTFProjectile_Arrow:
			case ETFClassID::CTFProjectile_GrapplingHook:
			case ETFClassID::CTFProjectile_HealingBolt:
				if (pEntity->As<CTFProjectile_Arrow>()->m_bCritical())
					tCache.m_vText.push_back({ ESPTextEnum::Right, "CRIT", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value });
				break;
			case ETFClassID::CTFProjectile_Rocket:
			case ETFClassID::CTFProjectile_BallOfFire:
			case ETFClassID::CTFProjectile_MechanicalArmOrb:
			case ETFClassID::CTFProjectile_SentryRocket:
			case ETFClassID::CTFProjectile_SpellFireball:
			case ETFClassID::CTFProjectile_SpellLightningOrb:
			case ETFClassID::CTFProjectile_SpellKartOrb:
				if (pEntity->As<CTFProjectile_Rocket>()->m_bCritical())
					tCache.m_vText.push_back({ ESPTextEnum::Right, "CRIT", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value });
				break;
			case ETFClassID::CTFProjectile_EnergyBall:
				if (pEntity->As<CTFProjectile_EnergyBall>()->m_bChargedShot())
					tCache.m_vText.push_back({ ESPTextEnum::Right, "CHARGE", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value });
				break;
			case ETFClassID::CTFProjectile_Flare:
				if (pEntity->As<CTFProjectile_Flare>()->m_bCritical())
					tCache.m_vText.push_back({ ESPTextEnum::Right, "CRIT", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value });
				break;
			}
		}
	}
}

void CESP::StoreObjective(CTFPlayer* pLocal)
{
	if (!(Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::Objective) || !Vars::ESP::Objective.Value)
		return;

	for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_OBJECTIVE))
	{
		if (pEntity->m_iTeamNum() == pLocal->m_iTeamNum() ? !(Vars::ESP::Objective.Value & Vars::ESP::ObjectiveEnum::Team) : !(Vars::ESP::Objective.Value & Vars::ESP::ObjectiveEnum::Enemy))
			continue;

		WorldCache& tCache = m_mWorldCache[pEntity];
		tCache.m_flAlpha = Vars::ESP::ActiveAlpha.Value / 255.f;
		tCache.m_tColor = H::Color.GetTeamColor(pLocal->m_iTeamNum(), pEntity->m_iTeamNum(), Vars::Colors::Relative.Value);
		tCache.m_bBox = Vars::ESP::Objective.Value & Vars::ESP::ObjectiveEnum::Box;

		if (Vars::ESP::Objective.Value & Vars::ESP::ObjectiveEnum::Distance)
		{
			Vec3 vDelta = pEntity->m_vecOrigin() - pLocal->m_vecOrigin();
			tCache.m_vText.push_back({ ESPTextEnum::Bottom, std::format("[{:.0f}M]", vDelta.Length2D() / 41), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
		}

		switch (pEntity->GetClassID())
		{
		case ETFClassID::CCaptureFlag:
		{
			auto pIntel = pEntity->As<CCaptureFlag>();

			if (Vars::ESP::Objective.Value & Vars::ESP::ObjectiveEnum::Name)
				tCache.m_vText.push_back({ ESPTextEnum::Top, "Intel", H::Color.GetEntityNameColor(pLocal, pEntity, Vars::Colors::Relative.Value), Vars::Menu::Theme::Background.Value });

			if (Vars::ESP::Objective.Value & Vars::ESP::ObjectiveEnum::Flags)
			{
				switch (pIntel->m_nFlagStatus())
				{
				case TF_FLAGINFO_HOME:
					tCache.m_vText.push_back({ ESPTextEnum::Right, "HOME", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
					break;
				case TF_FLAGINFO_DROPPED:
					tCache.m_vText.push_back({ ESPTextEnum::Right, "DROPPED", Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
					break;
				default:
					tCache.m_vText.push_back({ ESPTextEnum::Right, "STOLEN", Vars::Colors::IndicatorTextBad.Value, Vars::Menu::Theme::Background.Value });
				}
			}

			if (Vars::ESP::Objective.Value & Vars::ESP::ObjectiveEnum::IntelReturnTime && pIntel->m_nFlagStatus() == TF_FLAGINFO_DROPPED)
			{
				float flReturnTime = std::max(pIntel->m_flResetTime() - TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick), 0.f);
				tCache.m_vText.push_back({ ESPTextEnum::Right, std::format("RETURN {:.1f}s", pIntel->m_flResetTime() - TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick)).c_str(), Vars::Menu::Theme::Active.Value, Vars::Menu::Theme::Background.Value });
			}

			break;
		}
		}
	}
}

void CESP::StoreWorld()
{
	if (Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::NPCs)
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_NPC))
		{
			WorldCache& tCache = m_mWorldCache[pEntity];

			const char* szName = "NPC";
			switch (pEntity->GetClassID())
			{
			case ETFClassID::CEyeballBoss: szName = "Monoculus"; break;
			case ETFClassID::CHeadlessHatman: szName = "Horseless Headless Horsemann"; break;
			case ETFClassID::CMerasmus: szName = "Merasmus"; break;
			case ETFClassID::CTFBaseBoss: szName = "Boss"; break;
			case ETFClassID::CTFTankBoss: szName = "Tank"; break;
			case ETFClassID::CZombie: szName = "Skeleton"; break;
			}

			tCache.m_vText.push_back({ ESPTextEnum::Top, szName, Vars::Colors::NPC.Value, Vars::Menu::Theme::Background.Value });
		}
	}

	if (Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::Health)
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::PICKUPS_HEALTH))
		{
			WorldCache& tCache = m_mWorldCache[pEntity];

			tCache.m_vText.push_back({ ESPTextEnum::Top, "Health", Vars::Colors::Health.Value, Vars::Menu::Theme::Background.Value });
		}
	}

	if (Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::Ammo)
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::PICKUPS_AMMO))
		{
			WorldCache& tCache = m_mWorldCache[pEntity];

			tCache.m_vText.push_back({ ESPTextEnum::Top, "Ammo", Vars::Colors::Ammo.Value, Vars::Menu::Theme::Background.Value });
		}
	}

	if (Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::Money)
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::PICKUPS_MONEY))
		{
			WorldCache& tCache = m_mWorldCache[pEntity];

			tCache.m_vText.push_back({ ESPTextEnum::Top, "Money", Vars::Colors::Money.Value, Vars::Menu::Theme::Background.Value });
		}
	}

	if (Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::Powerups)
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::PICKUPS_POWERUP))
		{
			WorldCache& tCache = m_mWorldCache[pEntity];

			const char* szName = "Powerup";
			switch (FNV1A::Hash32(I::ModelInfoClient->GetModelName(pEntity->GetModel())))
			{
			case FNV1A::Hash32Const("models/pickups/pickup_powerup_agility.mdl"): szName = "Agility"; break;
			case FNV1A::Hash32Const("models/pickups/pickup_powerup_crit.mdl"): szName = "Revenge"; break;
			case FNV1A::Hash32Const("models/pickups/pickup_powerup_defense.mdl"): szName = "Resistance"; break;
			case FNV1A::Hash32Const("models/pickups/pickup_powerup_haste.mdl"): szName = "Haste"; break;
			case FNV1A::Hash32Const("models/pickups/pickup_powerup_king.mdl"): szName = "King"; break;
			case FNV1A::Hash32Const("models/pickups/pickup_powerup_knockout.mdl"): szName = "Knockout"; break;
			case FNV1A::Hash32Const("models/pickups/pickup_powerup_plague.mdl"): szName = "Plague"; break;
			case FNV1A::Hash32Const("models/pickups/pickup_powerup_precision.mdl"): szName = "Precision"; break;
			case FNV1A::Hash32Const("models/pickups/pickup_powerup_reflect.mdl"): szName = "Reflect"; break;
			case FNV1A::Hash32Const("models/pickups/pickup_powerup_regen.mdl"): szName = "Regeneration"; break;
				//case FNV1A::Hash32Const("models/pickups/pickup_powerup_resistance.mdl"): szName = "11"; break;
			case FNV1A::Hash32Const("models/pickups/pickup_powerup_strength.mdl"): szName = "Strength"; break;
				//case FNV1A::Hash32Const("models/pickups/pickup_powerup_strength_arm.mdl"): szName = "13"; break;
			case FNV1A::Hash32Const("models/pickups/pickup_powerup_supernova.mdl"): szName = "Supernova"; break;
				//case FNV1A::Hash32Const("models/pickups/pickup_powerup_thorns.mdl"): szName = "15"; break;
				//case FNV1A::Hash32Const("models/pickups/pickup_powerup_uber.mdl"): szName = "16"; break;
			case FNV1A::Hash32Const("models/pickups/pickup_powerup_vampire.mdl"): szName = "Vampire";
			}
			tCache.m_vText.push_back({ ESPTextEnum::Top, szName, Vars::Colors::Powerup.Value, Vars::Menu::Theme::Background.Value });
		}
	}

	if (Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::Bombs)
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_BOMBS))
		{
			WorldCache& tCache = m_mWorldCache[pEntity];

			tCache.m_vText.push_back({ ESPTextEnum::Top, pEntity->GetClassID() == ETFClassID::CTFPumpkinBomb ? "Pumpkin Bomb" : "Bomb", Vars::Colors::Halloween.Value, Vars::Menu::Theme::Background.Value });
		}
	}

	if (Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::Spellbook)
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::PICKUPS_SPELLBOOK))
		{
			WorldCache& tCache = m_mWorldCache[pEntity];

			tCache.m_vText.push_back({ ESPTextEnum::Top, "Spellbook", Vars::Colors::Halloween.Value, Vars::Menu::Theme::Background.Value });
		}
	}

	if (Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::Gargoyle)
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_GARGOYLE))
		{
			WorldCache& tCache = m_mWorldCache[pEntity];

			tCache.m_vText.push_back({ ESPTextEnum::Top, "Gargoyle", Vars::Colors::Halloween.Value, Vars::Menu::Theme::Background.Value });
		}
	}
}

void CESP::Draw()
{
	if (!Vars::ESP::Draw.Value)
		return;

	DrawWorld();
	DrawBuildings();
	DrawPlayers();
}

void CESP::DrawPlayers()
{
	if (!(Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::Players) || !Vars::ESP::Player.Value)
		return;

	const auto& fFont = H::Fonts.GetFont(FONT_ESP);
	const int nTall = fFont.m_nTall;
	const auto& fFontSide = H::Fonts.GetFont(FONT_ESP_FLAG);
	const int nTallSide = fFontSide.m_nTall;
	const auto& fFontHP = H::Fonts.GetFont(FONT_ESP_HP);
	const int nTallHP = fFontHP.m_nTall;
	for (auto& [pEntity, tCache] : m_mPlayerCache)
	{
		float x, y, w, h;
		if (!GetDrawBounds(pEntity, x, y, w, h))
			continue;

		int l = x - H::Draw.Scale(6), r = x + w + H::Draw.Scale(6), m = x + w / 2;
		int t = y - H::Draw.Scale(5), b = y + h + H::Draw.Scale(5);
		int lOffset = 0, rOffset = 0, bOffset = 0, tOffset = 0;

		I::MatSystemSurface->DrawSetAlphaMultiplier(tCache.m_flAlpha);

		if (tCache.m_bBox)
			H::Draw.LineRectOutline(x, y, w, h, tCache.m_tColor, { 0, 0, 0, 255 });

		if (tCache.m_bBones)
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			switch (H::Entities.GetModel(pPlayer->entindex()))
			{
			case FNV1A::Hash32Const("models/vsh/player/saxton_hale.mdl"):
				DrawBones(pPlayer, { HITBOX_SAXTON_HEAD, HITBOX_SAXTON_CHEST, HITBOX_SAXTON_PELVIS }, tCache.m_tColor);
				DrawBones(pPlayer, { HITBOX_SAXTON_CHEST, HITBOX_SAXTON_LEFT_UPPER_ARM, HITBOX_SAXTON_LEFT_FOREARM, HITBOX_SAXTON_LEFT_HAND }, tCache.m_tColor);
				DrawBones(pPlayer, { HITBOX_SAXTON_CHEST, HITBOX_SAXTON_RIGHT_UPPER_ARM, HITBOX_SAXTON_RIGHT_FOREARM, HITBOX_SAXTON_RIGHT_HAND }, tCache.m_tColor);
				DrawBones(pPlayer, { HITBOX_SAXTON_PELVIS, HITBOX_SAXTON_LEFT_THIGH, HITBOX_SAXTON_LEFT_CALF, HITBOX_SAXTON_LEFT_FOOT }, tCache.m_tColor);
				DrawBones(pPlayer, { HITBOX_SAXTON_PELVIS, HITBOX_SAXTON_RIGHT_THIGH, HITBOX_SAXTON_RIGHT_CALF, HITBOX_SAXTON_RIGHT_FOOT }, tCache.m_tColor);
				break;
			default:
				DrawBones(pPlayer, { HITBOX_HEAD, HITBOX_CHEST, HITBOX_PELVIS }, tCache.m_tColor);
				DrawBones(pPlayer, { HITBOX_CHEST, HITBOX_LEFT_UPPER_ARM, HITBOX_LEFT_FOREARM, HITBOX_LEFT_HAND }, tCache.m_tColor);
				DrawBones(pPlayer, { HITBOX_CHEST, HITBOX_RIGHT_UPPER_ARM, HITBOX_RIGHT_FOREARM, HITBOX_RIGHT_HAND }, tCache.m_tColor);
				DrawBones(pPlayer, { HITBOX_PELVIS, HITBOX_LEFT_THIGH, HITBOX_LEFT_CALF, HITBOX_LEFT_FOOT }, tCache.m_tColor);
				DrawBones(pPlayer, { HITBOX_PELVIS, HITBOX_RIGHT_THIGH, HITBOX_RIGHT_CALF, HITBOX_RIGHT_FOOT }, tCache.m_tColor);
			}
		}

		if (tCache.m_bHealthBar)
		{
			const int healthBarWidth = 1; // 1 pixel wide health bar
			const int backgroundExtension = 1; // 1 pixel extension on each side
			const Color_t backgroundColor = { 0, 0, 0, 130 }; // Black background with 130 alpha

			// Draw the black background
			H::Draw.FillRect(
				x - H::Draw.Scale(6) - backgroundExtension, // X position extended by 1 pixel
				y - backgroundExtension, // Y position extended by 1 pixel
				healthBarWidth + 2 * backgroundExtension, // Width extended by 2 pixels
				h + 2 * backgroundExtension, // Height extended by 2 pixels
				backgroundColor
			);

			if (tCache.m_flHealth > 1.f)
			{
				// Overhealed: Cap the health bar at full height and keep "IndicatorGood" color
				Color_t cColor = Vars::Colors::IndicatorGood.Value;
				H::Draw.FillRectPercent(
					x - H::Draw.Scale(6), y,
					healthBarWidth, h,
					1.f, cColor,
					{ 0, 0, 0, 0 }, // No outline
					ALIGN_BOTTOM, true
				);
			}
			else if (tCache.m_flHealth > 0.5f)
			{
				// Mid health range: Lerp between "IndicatorGood" and "IndicatorMid"
				float midLerpFactor = (tCache.m_flHealth - 0.5f) * 2.f;  // Normalize to 0-1
				Color_t cColor = Vars::Colors::IndicatorMid.Value.Lerp(Vars::Colors::IndicatorGood.Value, midLerpFactor);
				H::Draw.FillRectPercent(
					x - H::Draw.Scale(6), y,
					healthBarWidth, h,
					tCache.m_flHealth, cColor,
					{ 0, 0, 0, 0 }, // No outline
					ALIGN_BOTTOM, true
				);
			}
			else
			{
				// Low health range: Lerp between "IndicatorMid" and "IndicatorBad"
				float lowLerpFactor = tCache.m_flHealth * 2.f;  // Normalize to 0-1
				Color_t cColor = Vars::Colors::IndicatorBad.Value.Lerp(Vars::Colors::IndicatorMid.Value, lowLerpFactor);
				H::Draw.FillRectPercent(
					x - H::Draw.Scale(6), y,
					healthBarWidth, h,
					tCache.m_flHealth, cColor,
					{ 0, 0, 0, 0 }, // No outline
					ALIGN_BOTTOM, true
				);
			}

			lOffset += H::Draw.Scale(6);
		}

		if (tCache.m_bUberBar)
		{
			const int uberBarHeight = 1; // 1 pixel high UberBar
			const int backgroundExtension = 1; // 1 pixel extension on each side
			const Color_t backgroundColor = { 0, 0, 0, 130 }; // Black background with 130 alpha

			// Draw the black background for the UberBar
			H::Draw.FillRect(
				x - backgroundExtension, // X position extended by 1 pixel
				y + h + H::Draw.Scale(4) - backgroundExtension, // Y position extended by 1 pixel
				w + 2 * backgroundExtension, // Width extended by 2 pixels
				uberBarHeight + 2 * backgroundExtension, // Height extended by 2 pixels
				backgroundColor
			);

			// Draw the UberBar
			H::Draw.FillRectPercent(
				x, // X position
				y + h + H::Draw.Scale(4), // Y position (adjusted for placement)
				w, // Width
				uberBarHeight, // Height (1 pixel)
				tCache.m_flUber, // Uber percentage
				Vars::Colors::IndicatorMisc.Value, // UberBar color
				{ 0, 0, 0, 0 } // No outline
			);

			bOffset += H::Draw.Scale(6);
		}

		int iVerticalOffset = H::Draw.Scale(3, Scale_Floor) - 1;
		for (auto& [iMode, sText, tColor, tOutline] : tCache.m_vText)
		{
			switch (iMode)
			{
			case ESPTextEnum::Top:
				H::Draw.String(fFont, m, t - tOffset, tColor, ALIGN_BOTTOM, sText.c_str());
				tOffset += nTall;
				break;
			case ESPTextEnum::Bottom:
				H::Draw.String(fFont, m, b + bOffset, tColor, ALIGN_TOP, sText.c_str());
				bOffset += nTall;
				break;
			case ESPTextEnum::Right:
				H::Draw.String(fFontSide, r, t + iVerticalOffset + rOffset, tColor, ALIGN_TOPLEFT, sText.c_str());
				rOffset += nTallSide;
				break;
			case ESPTextEnum::Health:
				H::Draw.String(fFontHP, l - lOffset, t + iVerticalOffset + h - h * std::min(tCache.m_flHealth, 1.f), tColor, ALIGN_TOPRIGHT, sText.c_str());
				break;
			case ESPTextEnum::Uber:
				H::Draw.String(fFontSide, r, y + h, tColor, ALIGN_TOPLEFT, sText.c_str());
				break;
			}
		}

		if (tCache.m_iClassIcon)
		{
			int size = H::Draw.Scale(18, Scale_Round);
			H::Draw.Texture(m, t - tOffset, size, size, tCache.m_iClassIcon - 1, ALIGN_BOTTOM);
		}

		if (tCache.m_pWeaponIcon)
		{
			float flW = tCache.m_pWeaponIcon->Width(), flH = tCache.m_pWeaponIcon->Height();
			float flScale = H::Draw.Scale(std::min((w + 40) / 2.f, 80.f) / std::max(flW, flH * 2));
			H::Draw.DrawHudTexture(m - flW / 2.f * flScale, b + bOffset, flScale, tCache.m_pWeaponIcon, Vars::Menu::Theme::Active.Value);
		}
	}

	I::MatSystemSurface->DrawSetAlphaMultiplier(1.f);
}

void CESP::DrawBuildings()
{
	if (!(Vars::ESP::Draw.Value & Vars::ESP::DrawEnum::Buildings) || !Vars::ESP::Building.Value)
		return;

	const auto& fFont = H::Fonts.GetFont(FONT_ESP);
	const int nTall = fFont.m_nTall;
	const auto& fFontSide = H::Fonts.GetFont(FONT_ESP_FLAG);
	const int nTallSide = fFontSide.m_nTall;
	const auto& fFontHP = H::Fonts.GetFont(FONT_ESP_HP);
	const int nTallHP = fFontHP.m_nTall;

	for (auto& [pEntity, tCache] : m_mBuildingCache)
	{
		float x, y, w, h;
		if (!GetDrawBounds(pEntity, x, y, w, h))
			continue;

		int l = x - H::Draw.Scale(6), r = x + w + H::Draw.Scale(6), m = x + w / 2;
		int t = y - H::Draw.Scale(5), b = y + h + H::Draw.Scale(5);
		int lOffset = 0, rOffset = 0, bOffset = 0, tOffset = 0;

		I::MatSystemSurface->DrawSetAlphaMultiplier(tCache.m_flAlpha);

		if (tCache.m_bBox)
			H::Draw.LineRectOutline(x, y, w, h, tCache.m_tColor, { 0, 0, 0, 255 });

		if (tCache.m_bHealthBar)
		{
			const int healthBarWidth = 1; // 1 pixel wide health bar
			const int backgroundExtension = 1; // 1 pixel extension on each side
			const Color_t backgroundColor = { 0, 0, 0, 130 }; // Black background with 130 alpha

			// Draw the black background
			H::Draw.FillRect(
				x - H::Draw.Scale(6) - backgroundExtension, // X position extended by 1 pixel
				y - backgroundExtension, // Y position extended by 1 pixel
				healthBarWidth + 2 * backgroundExtension, // Width extended by 2 pixels
				h + 2 * backgroundExtension, // Height extended by 2 pixels
				backgroundColor
			);

			if (tCache.m_flHealth > 1.f)
			{
				// Overhealed: Cap the health bar at full height and keep "IndicatorGood" color
				Color_t cColor = Vars::Colors::IndicatorGood.Value;
				H::Draw.FillRectPercent(
					x - H::Draw.Scale(6), y,
					healthBarWidth, h,
					1.f, cColor,
					{ 0, 0, 0, 0 }, // No outline
					ALIGN_BOTTOM, true
				);
			}
			else if (tCache.m_flHealth > 0.5f)
			{
				// Mid health range: Lerp between "IndicatorGood" and "IndicatorMid"
				float midLerpFactor = (tCache.m_flHealth - 0.5f) * 2.f;  // Normalize to 0-1
				Color_t cColor = Vars::Colors::IndicatorMid.Value.Lerp(Vars::Colors::IndicatorGood.Value, midLerpFactor);
				H::Draw.FillRectPercent(
					x - H::Draw.Scale(6), y,
					healthBarWidth, h,
					tCache.m_flHealth, cColor,
					{ 0, 0, 0, 0 }, // No outline
					ALIGN_BOTTOM, true
				);
			}
			else
			{
				// Low health range: Lerp between "IndicatorMid" and "IndicatorBad"
				float lowLerpFactor = tCache.m_flHealth * 2.f;  // Normalize to 0-1
				Color_t cColor = Vars::Colors::IndicatorBad.Value.Lerp(Vars::Colors::IndicatorMid.Value, lowLerpFactor);
				H::Draw.FillRectPercent(
					x - H::Draw.Scale(6), y,
					healthBarWidth, h,
					tCache.m_flHealth, cColor,
					{ 0, 0, 0, 0 }, // No outline
					ALIGN_BOTTOM, true
				);
			}

			lOffset += H::Draw.Scale(6);
		}

		int iVerticalOffset = H::Draw.Scale(3, Scale_Floor) - 1;
		for (auto& [iMode, sText, tColor, tOutline] : tCache.m_vText)
		{
			switch (iMode)
			{
			case ESPTextEnum::Top:
				H::Draw.String(fFont, m, t - tOffset, tColor, ALIGN_BOTTOM, sText.c_str());
				tOffset += nTall;
				break;
			case ESPTextEnum::Bottom:
				H::Draw.String(fFont, m, b + bOffset, tColor, ALIGN_TOP, sText.c_str());
				bOffset += nTall;
				break;
			case ESPTextEnum::Right:
				H::Draw.String(fFontSide, r, t + iVerticalOffset + rOffset, tColor, ALIGN_TOPLEFT, sText.c_str());
				rOffset += nTallSide;
				break;
			case ESPTextEnum::Health:
				H::Draw.String(fFontHP, l - lOffset, t + iVerticalOffset + h - h * std::min(tCache.m_flHealth, 1.f), tColor, ALIGN_TOPRIGHT, sText.c_str());
				break;
			}
		}
	}

	I::MatSystemSurface->DrawSetAlphaMultiplier(1.f);
}

void CESP::DrawWorld()
{
	const auto& fFont = H::Fonts.GetFont(FONT_ESP);
	const int nTall = fFont.m_nTall;
	const auto& fFontSide = H::Fonts.GetFont(FONT_ESP_FLAG);
	const int nTallSide = fFontSide.m_nTall;
	const auto& fFontHP = H::Fonts.GetFont(FONT_ESP_HP);
	const int nTallHP = fFontHP.m_nTall;
	for (auto& [pEntity, tCache] : m_mWorldCache)
	{
		float x, y, w, h;
		if (!GetDrawBounds(pEntity, x, y, w, h))
			continue;

		int l = x - H::Draw.Scale(6), r = x + w + H::Draw.Scale(6), m = x + w / 2;
		int t = y - H::Draw.Scale(5), b = y + h + H::Draw.Scale(5);
		int lOffset = 0, rOffset = 0, bOffset = 0, tOffset = 0;

		I::MatSystemSurface->DrawSetAlphaMultiplier(tCache.m_flAlpha);

		if (tCache.m_bBox)
			H::Draw.LineRectOutline(x, y, w, h, tCache.m_tColor, { 0, 0, 0, 255 });

		int iVerticalOffset = H::Draw.Scale(3, Scale_Floor) - 1;
		for (auto& [iMode, sText, tColor, tOutline] : tCache.m_vText)
		{
			switch (iMode)
			{
			case ESPTextEnum::Top:
				H::Draw.String(fFont, m, t - tOffset, tColor, ALIGN_BOTTOM, sText.c_str());
				tOffset += nTall;
				break;
			case ESPTextEnum::Bottom:
				H::Draw.String(fFont, m, b + bOffset, tColor, ALIGN_TOP, sText.c_str());
				bOffset += nTall;
				break;
			case ESPTextEnum::Right:
				H::Draw.String(fFontSide, r, t + iVerticalOffset + rOffset, tColor, ALIGN_TOPLEFT, sText.c_str());
				rOffset += nTallSide;
			}
		}
	}

	I::MatSystemSurface->DrawSetAlphaMultiplier(1.f);
}

bool CESP::GetDrawBounds(CBaseEntity* pEntity, float& x, float& y, float& w, float& h)
{
	auto& transform = const_cast<matrix3x4&>(pEntity->RenderableToWorldTransform());
	if (pEntity->entindex() == I::EngineClient->GetLocalPlayer())
	{
		Math::AngleMatrix({ 0.f, I::EngineClient->GetViewAngles().y, 0.f }, transform);
		Math::MatrixSetColumn(pEntity->GetAbsOrigin(), 3, transform);
	}

	float flLeft, flRight, flTop, flBottom;
	if (!SDK::IsOnScreen(pEntity, transform, &flLeft, &flRight, &flTop, &flBottom))
		return false;

	x = flLeft;
	y = flBottom;
	w = flRight - flLeft;
	h = flTop - flBottom;

	switch (pEntity->GetClassID())
	{
	case ETFClassID::CTFPlayer:
	case ETFClassID::CObjectSentrygun:
	case ETFClassID::CObjectDispenser:
	case ETFClassID::CObjectTeleporter:
		x += w * 0.125f;
		w *= 0.75f;
	}

	return !(x > H::Draw.m_nScreenW || x + w < 0 || y > H::Draw.m_nScreenH || y + h < 0);
}

const char* CESP::GetPlayerClass(int iClassNum)
{
	static const char* szClasses[] = {
		"Unknown", "Scout", "Sniper", "Soldier", "Demoman", "Medic", "Heavy", "Pyro", "Spy", "Engineer"
	};

	return iClassNum < 10 && iClassNum > 0 ? szClasses[iClassNum] : szClasses[0];
}

void CESP::DrawBones(CTFPlayer* pPlayer, std::vector<int> vecBones, Color_t clr)
{
	for (size_t n = 1; n < vecBones.size(); n++)
	{
		const auto vBone1 = pPlayer->GetHitboxCenter(vecBones[n]);
		const auto vBone2 = pPlayer->GetHitboxCenter(vecBones[n - 1]);

		Vec3 vScreenBone, vScreenParent;
		if (SDK::W2S(vBone1, vScreenBone) && SDK::W2S(vBone2, vScreenParent))
			H::Draw.Line(vScreenBone.x, vScreenBone.y, vScreenParent.x, vScreenParent.y, clr);
	}
}