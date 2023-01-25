/*
 Copyright (c) 2013 yvt,
 based on code of pysnip (c) Mathias Kaerlev 2011-2012.

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "Player.h"

#include "GameMap.h"
#include "GameMapWrapper.h"
#include "Grenade.h"
#include "HitTestDebugger.h"
#include "IWorldListener.h"
#include "PhysicsConstants.h"
#include "Weapon.h"
#include "World.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Settings.h>

namespace spades {
	namespace client {

		Player::Player(World& w, int pId, WeaponType wType, int tId, Vector3 pos, IntVector3 col)
		    : world(w) {
			SPADES_MARK_FUNCTION();

			lastClimbTime = -100;
			lastJumpTime = -100;
			tool = ToolWeapon;
			airborne = false;
			wade = false;
			position = pos;
			velocity = MakeVector3(0, 0, 0);
			orientation = MakeVector3(tId ? -1.0F : 1, 0, 0);
			eye = MakeVector3(0, 0, 0);
			moveDistance = 0.0F;
			moveSteps = 0;

			playerId = pId;
			weapon.reset(Weapon::CreateWeapon(wType, *this, *w.GetGameProperties()));
			weaponType = wType;
			teamId = tId;
			weapon->Reset();
			color = col;

			health = 100;
			grenades = 3;
			blockStocks = 50;
			blockColor = MakeIntVector3(111, 111, 111);

			nextSpadeTime = 0.0F;
			nextDigTime = 0.0F;
			nextGrenadeTime = 0.0F;
			nextBlockTime = 0.0F;
			firstDig = false;
			lastReloadingTime = 0.0F;
		
			pendingPlaceBlock = false;
			pendingRestockBlock = false;

			blockCursorActive = false;
			blockCursorDragging = false;

			holdingGrenade = false;
			reloadingServerSide = false;
			canPending = false;
		}

		Player::~Player() { SPADES_MARK_FUNCTION(); }

		bool Player::IsLocalPlayer() { return world.GetLocalPlayer() == this; }

		void Player::SetInput(PlayerInput newInput) {
			SPADES_MARK_FUNCTION();

			if (!IsAlive())
				return;

			if (newInput.crouch != input.crouch) {
				if (newInput.crouch && !airborne) {
					position.z += 0.9F;
				} else {
					// Refuse the standing-up request if there's no room
					if (!TryUncrouch()) {
						// ... unless the request is from the server.
						if (IsLocalPlayer())
							newInput.crouch = true;
					}
				}
			}

			input = newInput;
		}

		void Player::SetWeaponInput(WeaponInput newInput) {
			SPADES_MARK_FUNCTION();

			auto* listener = world.GetListener();

			if (!IsAlive())
				return;

			if (IsWalking() && !input.crouch && input.sprint) {
				newInput.primary = false;
				newInput.secondary = false;
			}

			if (tool == ToolSpade) {
				if (newInput.primary)
					newInput.secondary = false;
				if (newInput.secondary != weapInput.secondary) {
					if (newInput.secondary) {
						nextDigTime = world.GetTime() + GetToolSecondaryDelay();
						firstDig = true;
					}
				}
			} else if (tool == ToolGrenade) {
				if (!IsReadyToUseTool() && IsLocalPlayer())
					newInput.primary = false;

				if (newInput.primary != weapInput.primary) {
					if (!newInput.primary) {
						if (holdingGrenade) {
							nextGrenadeTime = world.GetTime() + GetToolPrimaryDelay();
							ThrowGrenade();
						}
					} else {
						holdingGrenade = true;
						grenadeTime = world.GetTime();

						if (listener)
							listener->PlayerPulledGrenadePin(*this);
					}
				}
			} else if (tool == ToolBlock) {
				// work-around for bug that placing block occasionally becomes impossible
				if (world.GetTime() - nextBlockTime > GetToolPrimaryDelay())
					nextBlockTime = world.GetTime();

				if (world.GetTime() < nextBlockTime && IsLocalPlayer()) {
					newInput.primary = false;
					newInput.secondary = false;
				}

				if (newInput.primary)
					newInput.secondary = false;
				if (newInput.secondary != weapInput.secondary) {
					if (newInput.secondary) {
						if (blockCursorActive) {
							blockCursorDragging = true;
							blockCursorDragPos = blockCursorPos;
						} else {
							if (listener && IsLocalPlayer()) // cannot build; invalid position.
								listener->LocalPlayerBuildError(BuildFailureReason::InvalidPosition);
						}
					} else {
						if (blockCursorDragging) {
							if (blockCursorActive) {
								int blocks = world.CubeLine(blockCursorDragPos, blockCursorPos, 64).size();
								if (blocks <= blockStocks) {
									if (listener && IsLocalPlayer())
										listener->LocalPlayerCreatedLineBlock(blockCursorDragPos, blockCursorPos);
									// blockStocks -= blocks.size(); decrease when created
								} else {
									if (listener && IsLocalPlayer()) // cannot build; insufficient blocks.
										listener->LocalPlayerBuildError(BuildFailureReason::InsufficientBlocks);
								}
								nextBlockTime = world.GetTime() + GetToolSecondaryDelay();
							} else {
								if (listener && IsLocalPlayer()) // cannot build; invalid position.
									listener->LocalPlayerBuildError(BuildFailureReason::InvalidPosition);
							}
						}

						blockCursorActive = false;
						blockCursorDragging = false;
					}
				}

				if (newInput.primary != weapInput.primary || newInput.primary) {
					if (newInput.primary) {
						if (!weapInput.primary)
							lastSingleBlockBuildSeqDone = false;
						if (blockCursorActive && blockStocks > 0) {
							if (listener && IsLocalPlayer())
								listener->LocalPlayerBlockAction(blockCursorPos, BlockActionCreate);

							lastSingleBlockBuildSeqDone = true;
							// blockStocks--; decrease when created

							nextBlockTime = world.GetTime() + GetToolPrimaryDelay();
						} else if (blockStocks > 0 && airborne && canPending && IsLocalPlayer()) {
							pendingPlaceBlock = true;
							pendingPlaceBlockPos = blockCursorPos;
						}

						blockCursorActive = false;
						blockCursorDragging = false;
					} else {
						if (!lastSingleBlockBuildSeqDone) {
							if (listener && IsLocalPlayer()) // cannot build; invalid position.
								listener->LocalPlayerBuildError(BuildFailureReason::InvalidPosition);
						}
					}
				}
			} else if (tool == ToolWeapon) {
				weapon->SetShooting(newInput.primary);

				// Update the weapon state asap so it picks up the weapon fire event even
				// if the player presses the mouse button and releases it really fast.
				// We shouldn't do this for the local player because the client haven't sent
				// a weapon update packet at this point and the hit will be rejected by the server.
				if (!IsLocalPlayer() && weapon->FrameNext(0.0F))
					FireWeapon();
			} else {
				SPAssert(false);
			}

			weapInput = newInput;
		}

		void Player::Reload() {
			SPADES_MARK_FUNCTION();

			if (!IsAlive())
				return; // dead man cannot reload

			weapon->Reload();
			if (IsLocalPlayer() && weapon->IsReloading())
				reloadingServerSide = true;
		}

		void Player::ReloadDone(int clip, int stock) {
			reloadingServerSide = false;
			weapon->ReloadDone(clip, stock);
		}

		void Player::Restock() {
			SPADES_MARK_FUNCTION();

			if (!IsAlive())
				return; // dead man cannot restock

			health = 100;
			grenades = 3;
			weapon->Restock();
			pendingRestockBlock = true;

			if (world.GetListener())
				world.GetListener()->PlayerRestocked(*this);
		}

		void Player::GotBlock() {
			if (blockStocks < 50)
				blockStocks++;
		}

		void Player::SetTool(spades::client::Player::ToolType t) {
			SPADES_MARK_FUNCTION();

			if (t == tool)
				return;

			tool = t;
			holdingGrenade = false;
			blockCursorActive = false;
			blockCursorDragging = false;
			reloadingServerSide = false;

			WeaponInput winp;
			SetWeaponInput(winp);

			weapon->AbortReload();

			if (world.GetListener())
				world.GetListener()->PlayerChangedTool(*this);
		}

		void Player::SetPosition(const spades::Vector3& v) {
			SPADES_MARK_FUNCTION();

			eye = position = v;
		}

		void Player::SetVelocity(const spades::Vector3& v) {
			SPADES_MARK_FUNCTION();

			velocity = v;
		}

		void Player::SetOrientation(const spades::Vector3& v) {
			SPADES_MARK_FUNCTION();

			orientation = v;
		}

		void Player::Turn(float longitude, float latitude) {
			SPADES_MARK_FUNCTION();

			Vector3 o = GetFront();

			float yaw = atan2f(o.y, o.x);
			float pitch = -atan2f(o.z, o.GetLength2D());

			yaw += longitude;
			pitch += latitude;

			// Check pitch bounds
			if (pitch > DEG2RAD(89))
				pitch = DEG2RAD(89);
			if (pitch < -DEG2RAD(89))
				pitch = -DEG2RAD(89);

			o.x = cosf(pitch) * cosf(yaw);
			o.y = cosf(pitch) * sinf(yaw);
			o.z = -sinf(pitch);

			SetOrientation(o);
		}

		void Player::SetHP(int hp, HurtType type, spades::Vector3 p) {
			health = hp;
			if (world.GetListener())
				world.GetListener()->LocalPlayerHurt(type, p);
		}

		void Player::Update(float dt) {
			SPADES_MARK_FUNCTION();

			auto* listener = world.GetListener();

			MovePlayer(dt);

			if (tool == ToolSpade) {
				if (weapInput.primary) {
					if (world.GetTime() > nextSpadeTime) {
						UseSpade(false);
						nextSpadeTime = world.GetTime() + GetToolPrimaryDelay();
					}
				} else if (weapInput.secondary) {
					if (world.GetTime() > nextDigTime) {
						UseSpade(true);
						nextDigTime = world.GetTime() + GetToolSecondaryDelay();
						firstDig = false;
					}
				}
			} else if (tool == ToolBlock && IsLocalPlayer()) {
				const Handle<GameMap>& map = world.GetMap();
				SPAssert(map);

				GameMap::RayCastResult res;
				res = map->CastRay2(GetEye(), GetFront(), 12);

				canPending = false;
				if (blockCursorDragging) {
					// check the starting point is not floating
					IntVector3 start = blockCursorDragPos;
					if (map->IsSurface(start.x, start.y, start.z)) {
						if (listener && IsLocalPlayer()) // cannot build; floating
							listener->LocalPlayerBuildError(BuildFailureReason::InvalidPosition);
						blockCursorDragging = false;
					}
				}

				if (res.hit
					&& Collision3D(res.hitBlock + res.normal)
					&& !OverlapsWithBlock(res.hitBlock + res.normal)
					&&  map->IsValidBuildCoord(res.hitBlock + res.normal)
					&& !pendingPlaceBlock) {
					// Building is possible, and there's no delayed block placement.
					blockCursorActive = true;
					blockCursorPos = res.hitBlock + res.normal;
				} else if (pendingPlaceBlock) {
					// Delayed Placement: When player attempts to place a block
					// while jumping and placing block is currently impossible
					// building will be delayed until it becomes possible
					// as long as player is airborne.
					if (airborne == false || blockStocks <= 0) {
						// player is no longer airborne, or doesn't have a block to place.
						pendingPlaceBlock = false;
						lastSingleBlockBuildSeqDone = true;
					} else if (Collision3D(pendingPlaceBlockPos)
						&& !OverlapsWithBlock(pendingPlaceBlockPos)
						&& map->IsValidBuildCoord(pendingPlaceBlockPos)) {
						// now building became possible.
						SPAssert(IsLocalPlayer());

						if (listener)
							listener->LocalPlayerBlockAction(pendingPlaceBlockPos, BlockActionCreate);

						pendingPlaceBlock = false;
						lastSingleBlockBuildSeqDone = true;
						// blockStocks--; decrease when created

						nextBlockTime = world.GetTime() + GetToolPrimaryDelay();
					}
				} else {
					// Delayed Block Placement can be activated only
					// when the only reason making placement impossible
					// is that block to be placed overlaps with the player's hitbox.
					canPending = res.hit
						&& Collision3D(res.hitBlock + res.normal)
						&& map->IsValidBuildCoord(res.hitBlock + res.normal);
					blockCursorActive = false;

					int dist = 11;
					for (; (dist >= 1) && !Collision3D(res.hitBlock + res.normal); dist--)
						res = map->CastRay2(eye, orientation, dist);
					for (; (dist < 12) && Collision3D(res.hitBlock + res.normal); dist++)
						res = map->CastRay2(eye, orientation, dist);
					blockCursorPos = res.hitBlock + res.normal;
				}
			} else if (tool == ToolBlock && !IsLocalPlayer()) {
				if (weapInput.primary && world.GetTime() > nextBlockTime)
					nextBlockTime = world.GetTime() + GetToolPrimaryDelay();
			} else if (tool == ToolGrenade) {
				if (holdingGrenade && GetGrenadeCookTime() >= 3.0F)
					ThrowGrenade();
			}

			if (tool != ToolWeapon)
				weapon->SetShooting(false);

			if (weapon->FrameNext(dt))
				FireWeapon();

			if (weapon->IsReloading()) {
				lastReloadingTime = world.GetTime();
			} else if (reloadingServerSide) {
				// for some reason a server didn't return a WeaponReload packet.
				if (world.GetTime() + lastReloadingTime + 0.8F) {
					reloadingServerSide = false;
					weapon->ForceReloadDone();
				}
			}

			if (pendingRestockBlock) {
				blockStocks = 50;
				pendingRestockBlock = false;
			}
		}

		bool Player::RayCastApprox(spades::Vector3 start, spades::Vector3 dir, float tolerance) {
			Vector3 diff = position - start;

			// |P-A| * cos(theta)
			float c = Vector3::Dot(diff, dir);

			// |P-A|^2
			float sq = diff.GetSquaredLength();

			// |P-A| * sin(theta)
			float dist = sqrtf(sq - c * c);

			return dist < tolerance;
		}

		enum class HitBodyPart { None, Head, Torso, Limb1, Limb2, Arms };

		void Player::FireWeapon() {
			SPADES_MARK_FUNCTION();

			auto* listener = world.GetListener();

			Vector3 dir = GetFront();
			Vector3 muzzle = GetEye() + (dir * 0.01F);

			// for hit-test debugging
			std::map<int, HitTestDebugger::PlayerHit> playerHits;
			std::vector<Vector3> bulletVectors;

			int pellets = weapon->GetPelletSize();
			float spread = weapon->GetSpread();
			if (!weapInput.secondary)
				spread *= 2;

			const Handle<GameMap>& map = world.GetMap();
			SPAssert(map);

			// pyspades takes destroying more than one block as a speed hack (shotgun does this)
			bool blockDestroyed = false;

			// The custom state data, optionally set by `BulletHitPlayer`'s implementation
			std::unique_ptr<IBulletHitScanState> stateCell;

			Vector3 pelletDir = dir;
			for (int i = 0; i < pellets; i++) {
				// AoS 0.75's way (pelletDir shouldn't be normalized!)
				pelletDir.x += (SampleRandomFloat() - SampleRandomFloat()) * spread;
				pelletDir.y += (SampleRandomFloat() - SampleRandomFloat()) * spread;
				pelletDir.z += (SampleRandomFloat() - SampleRandomFloat()) * spread;

				dir = pelletDir.Normalize();

				bulletVectors.push_back(dir);

				// first do map raycast
				GameMap::RayCastResult mapResult;
				mapResult = map->CastRay2(muzzle, dir, 256);

				stmp::optional<Player&> hitPlayer;
				float hitPlayerDist2D = 0.0F; // disregarding Z coordinate
				float hitPlayerDist3D = 0.0F;
				HitBodyPart hitPart = HitBodyPart::None;

				for (size_t i = 0; i < world.GetNumPlayerSlots(); i++) {
					// TODO: This is a repeated pattern, add something like
					// `World::GetExistingPlayerRange()` returning a range
					auto maybeOther = world.GetPlayer(i);
					if (maybeOther == this || !maybeOther)
						continue;

					Player& other = maybeOther.value();
					if (!other.IsAlive() || other.IsSpectator())
						continue; // filter deads/spectators
					if (!other.RayCastApprox(muzzle, dir))
						continue; // quickly reject players unlikely to be hit

					Vector3 hitPos;
					HitBoxes hb = other.GetHitBoxes();
					if (hb.head.RayCast(muzzle, dir, &hitPos)) {
						float const dist = (hitPos - muzzle).GetLength2D();
						if (!hitPlayer || dist < hitPlayerDist2D) {
							hitPlayer = other;
							hitPlayerDist2D = dist;
							hitPlayerDist3D = (hitPos - muzzle).GetLength();
							hitPart = HitBodyPart::Head;
						}
					}

					if (hb.torso.RayCast(muzzle, dir, &hitPos)) {
						float const dist = (hitPos - muzzle).GetLength2D();
						if (!hitPlayer || dist < hitPlayerDist2D) {
							hitPlayer = other;
							hitPlayerDist2D = dist;
							hitPlayerDist3D = (hitPos - muzzle).GetLength();
							hitPart = HitBodyPart::Torso;
						}
					}

					for (int j = 0; j < 3; j++) {
						if (hb.limbs[j].RayCast(muzzle, dir, &hitPos)) {
							float const dist = (hitPos - muzzle).GetLength2D();
							if (!hitPlayer || dist < hitPlayerDist2D) {
								hitPlayer = other;
								hitPlayerDist2D = dist;
								hitPlayerDist3D = (hitPos - muzzle).GetLength();
								switch (j) {
									case 0: hitPart = HitBodyPart::Limb1; break;
									case 1: hitPart = HitBodyPart::Limb2; break;
									case 2: hitPart = HitBodyPart::Arms; break;
								}
							}
						}
					}
				}

				Vector3 finalHitPos = muzzle + dir * 128.0F;

				if (mapResult.hit && (mapResult.hitPos - muzzle).GetLength2D() < FOG_DISTANCE &&
				    (!hitPlayer || (mapResult.hitPos - muzzle).GetLength2D() < hitPlayerDist2D)) {
					finalHitPos = mapResult.hitPos;

					IntVector3 outBlockPos = mapResult.hitBlock;
					if (map->IsValidMapCoord(outBlockPos.x, outBlockPos.y, outBlockPos.z)) {
						SPAssert(map->IsSolid(outBlockPos.x, outBlockPos.y, outBlockPos.z));

						// block damage shouldn't be shared
						if (IsLocalPlayer() && outBlockPos.z < map->GroundDepth()) {
							uint32_t col = map->GetColor(outBlockPos.x, outBlockPos.y, outBlockPos.z);
							int health = (col >> 24) - weapon->GetDamage(HitTypeBlock);
							if (health <= 0 && !blockDestroyed) {
								health = 0;
								blockDestroyed = true;
								if (listener) // send destroy cmd for local
									listener->LocalPlayerBlockAction(outBlockPos, BlockActionTool);
							}

							if (map->IsSolid(outBlockPos.x, outBlockPos.y, outBlockPos.z)) {
								col = (col & 0xFFFFFF) | ((std::max(health, 0) & 0xFF) << 24);
								map->Set(outBlockPos.x, outBlockPos.y, outBlockPos.z, true, col);
								world.MarkBlockForRegeneration(outBlockPos);
							}
						}

						if (listener)
							listener->BulletHitBlock(finalHitPos, outBlockPos, mapResult.normal);
					}
				} else if (hitPlayer) {
					if (hitPlayerDist2D < FOG_DISTANCE) {
						finalHitPos = muzzle + dir * hitPlayerDist3D;

						HitType hitType;
						switch (hitPart) {
							case HitBodyPart::Head:
								playerHits[hitPlayer->playerId].numHeadHits++;
								hitType = HitTypeHead;
								break;
							case HitBodyPart::Torso:
								playerHits[hitPlayer->playerId].numTorsoHits++;
								hitType = HitTypeTorso;
								break;
							case HitBodyPart::Limb1:
								playerHits[hitPlayer->playerId].numLimbHits[0]++;
								hitType = HitTypeLegs;
								break;
							case HitBodyPart::Limb2:
								playerHits[hitPlayer->playerId].numLimbHits[1]++;
								hitType = HitTypeLegs;
								break;
							case HitBodyPart::Arms:
								playerHits[hitPlayer->playerId].numLimbHits[2]++;
								hitType = HitTypeArms;
								break;
							case HitBodyPart::None: SPAssert(false); break;
						}

						if (listener)
							listener->BulletHitPlayer(*hitPlayer,
								hitType, finalHitPos, *this, stateCell);
					}
				}

				if (listener)
					listener->AddBulletTracer(*this, muzzle, finalHitPos);
			} // one pellet done

			// do hit test debugging
			auto* debugger = world.GetHitTestDebugger();
			if (debugger && IsLocalPlayer())
				debugger->SaveImage(playerHits, bulletVectors);

			if (IsLocalPlayer()) {
				Vector2 rec = weapon->GetRecoil();

				// Horizontal recoil is driven by a triangular wave generator.
				int timer = world.GetTimeMS();
				rec.x *= (timer % 1024 < 512)
					? (timer % 512) - 255.5F
					: 255.5F - (timer % 512);

				if (IsWalking() && !weapInput.secondary)
					rec *= 2;

				if (airborne)
					rec *= 2;
				else if (input.crouch)
					rec /= 2;

				Turn(rec.x, rec.y);
			}

			reloadingServerSide = false;
		}

		void Player::ThrowGrenade() {
			SPADES_MARK_FUNCTION();

			if (!holdingGrenade)
				return;

			auto* listener = world.GetListener();

			if (IsLocalPlayer()) {
				Vector3 const dir = GetFront();
				Vector3 const muzzle = GetEye() + (dir * 0.1F);
				Vector3 const vel = IsAlive()
					? (dir + GetVelocity())
					: Vector3(0, 0, 0);

				float const fuse = 3.0F - GetGrenadeCookTime();

				auto nade = stmp::make_unique<Grenade>(world, muzzle, vel, fuse);
				if (listener)
					listener->PlayerThrewGrenade(*this, *nade);
				world.AddGrenade(std::move(nade));
				grenades--;
			} else {
				// grenade packet will be sent by server
				if (listener)
					listener->PlayerThrewGrenade(*this, {});
			}

			holdingGrenade = false;
		}

		void Player::UseSpade(bool dig) {
			SPADES_MARK_FUNCTION();

			auto* listener = world.GetListener();

			Vector3 muzzle = GetEye(), dir = GetFront();

			const Handle<GameMap>& map = world.GetMap();
			SPAssert(map);

			GameMap::RayCastResult mapResult;
			mapResult = map->CastRay2(muzzle, dir, 32);

			stmp::optional<Player&> hitPlayer;
			for (size_t i = 0; i < world.GetNumPlayerSlots(); i++) {
				auto maybeOther = world.GetPlayer(i);
				if (maybeOther == this || !maybeOther)
					continue;

				Player& other = maybeOther.value();
				if (!other.IsAlive() || other.IsSpectator())
					continue;

				Vector3 pos = other.GetPosition();
				if ((position - pos).GetLength() > MELEE_DISTANCE)
					continue;

				pos.z += 1;

				Vector3 vc;
				vc.x = Vector3::Dot(position - pos, GetRight());
				vc.y = Vector3::Dot(position - pos, GetUp());
				vc.z = Vector3::Dot(position - pos, GetFront());

				Vector2 view = MakeVector2(vc.x, vc.y) / vc.z;
				if (view.GetChebyshevLength() < MELEE_TOLERANCE) {
					hitPlayer = other;
					break;
				}
			}

			IntVector3 outBlockPos = mapResult.hitBlock;
			if (mapResult.hit && Collision3D(outBlockPos) && (!hitPlayer || dig)) {
				if (map->IsValidMapCoord(outBlockPos.x, outBlockPos.y, outBlockPos.z)) {
					SPAssert(map->IsSolid(outBlockPos.x, outBlockPos.y, outBlockPos.z));

					// block damage shouldn't be shared
					if (IsLocalPlayer() && outBlockPos.z < map->GroundDepth()) {
						if (!dig) {
							uint32_t col = map->GetColor(outBlockPos.x, outBlockPos.y, outBlockPos.z);
							int health = (col >> 24) - 55;
							if (health <= 0) {
								health = 0;
								if (listener) // send destroy cmd for local
									listener->LocalPlayerBlockAction(outBlockPos, BlockActionTool);
							}

							if (map->IsSolid(outBlockPos.x, outBlockPos.y, outBlockPos.z)) {
								col = (col & 0xFFFFFF) | ((std::max(health, 0) & 0xFF) << 24);
								map->Set(outBlockPos.x, outBlockPos.y, outBlockPos.z, true, col);
								world.MarkBlockForRegeneration(outBlockPos);
							}
						} else {
							if (listener)
								listener->LocalPlayerBlockAction(outBlockPos, BlockActionDig);
						}
					}

					if (listener)
						listener->PlayerHitBlockWithSpade(*this,
							mapResult.hitPos, outBlockPos, mapResult.normal);
				}
			} else if (hitPlayer && !dig) {
				// The custom state data, optionally set by `BulletHitPlayer`'s implementation
				std::unique_ptr<IBulletHitScanState> stateCell;

				if (listener)
					listener->BulletHitPlayer(*hitPlayer,
						HitTypeMelee, hitPlayer->GetEye(), *this, stateCell);
			}

			if (listener)
				listener->PlayerMissedSpade(*this);
		}

		Vector3 Player::GetFront() {
			SPADES_MARK_FUNCTION_DEBUG();
			return orientation;
		}

		Vector3 Player::GetFront2D() {
			SPADES_MARK_FUNCTION_DEBUG();
			return MakeVector3(orientation.x, orientation.y, 0).Normalize();
		}

		Vector3 Player::GetRight() {
			SPADES_MARK_FUNCTION_DEBUG();
			return -Vector3::Cross(MakeVector3(0, 0, -1), GetFront2D()).Normalize();
		}

		Vector3 Player::GetUp() {
			SPADES_MARK_FUNCTION_DEBUG();
			return Vector3::Cross(GetRight(), GetFront()).Normalize();
		}

		Vector3 Player::GetOrigin() {
			SPADES_MARK_FUNCTION_DEBUG();
			Vector3 v = eye;
			v.z += (input.crouch ? 0.45F : 0.9F);
			v.z += 0.3F;
			return v;
		}

		void Player::BoxClipMove(float fsynctics) {
			SPADES_MARK_FUNCTION();

			bool climb = false;
			float offset, m;
			if (input.crouch) {
				offset = 0.45F;
				m = 0.9F;
			} else {
				offset = 0.9F;
				m = 1.35F;
			}

			float f = fsynctics * 32.0F;
			float nx = f * velocity.x + position.x;
			float ny = f * velocity.y + position.y;
			float nz = position.z + offset;
			float z;

			const Handle<GameMap>& map = world.GetMap();
			SPAssert(map);

			z = m;
			float bx = nx + ((velocity.x > 0.0F) ? 0.45F : -0.45F);
			while (z >= -1.36F
				&& !map->ClipBox(bx, position.y - 0.45F, nz + z)
				&& !map->ClipBox(bx, position.y + 0.45F, nz + z))
				z -= 0.9F;
			if (z < -1.36F)
				position.x = nx;
			else if (!input.crouch && orientation.z < 0.5F && !input.sprint) {
				z = 0.35F;
				while (z >= -2.36F
					&& !map->ClipBox(bx, position.y - 0.45F, nz + z)
					&& !map->ClipBox(bx, position.y + 0.45F, nz + z))
					z -= 0.9F;
				if (z < -2.36F) {
					position.x = nx;
					climb = true;
				} else {
					velocity.x = 0.0F;
				}
			} else {
				velocity.x = 0.0F;
			}

			z = m;
			float by = ny + ((velocity.y > 0.0F) ? 0.45F : -0.45F);
			while (z >= -1.36F
				&& !map->ClipBox(position.x - 0.45F, by, nz + z)
				&& !map->ClipBox(position.x + 0.45F, by, nz + z))
				z -= 0.9F;
			if (z < -1.36F)
				position.y = ny;
			else if (!input.crouch && orientation.z < 0.5F && !input.sprint && !climb) {
				z = 0.35F;
				while (z >= -2.36F
					&& !map->ClipBox(position.x - 0.45F, by, nz + z)
					&& !map->ClipBox(position.x + 0.45F, by, nz + z))
					z -= 0.9F;
				if (z < -2.36F) {
					position.y = ny;
					climb = true;
				} else {
					velocity.y = 0.0F;
				}
			} else if (!climb) {
				velocity.y = 0.0F;
			}

			if (climb) {
				velocity.x *= 0.5F;
				velocity.y *= 0.5F;
				lastClimbTime = world.GetTime();
				nz--;
				m = -1.35F;
			} else {
				if (velocity.z < 0.0F)
					m = -m;
				nz += velocity.z * f;
			}

			airborne = true;
			if (map->ClipBox(position.x - 0.45F, position.y - 0.45F, nz + m) ||
			    map->ClipBox(position.x - 0.45F, position.y + 0.45F, nz + m) ||
			    map->ClipBox(position.x + 0.45F, position.y - 0.45F, nz + m) ||
			    map->ClipBox(position.x + 0.45F, position.y + 0.45F, nz + m)) {
				if (velocity.z >= 0.0F) {
					wade = position.z > 61.0F;
					airborne = false;
				}

				velocity.z = 0.0F;
			} else {
				position.z = nz - offset;
			}

			RepositionPlayer(position);
		}

		void Player::PlayerJump() {
			input.jump = false;
			velocity.z = -0.36F;

			if (world.GetListener() && world.GetTime() - lastJumpTime > 0.1F) {
				world.GetListener()->PlayerJumped(*this);
				lastJumpTime = world.GetTime();
			}
		}

		void Player::MoveCorpse(float fsynctics) {
			Vector3 oldPos = position; // old position

			// do velocity & gravity (friction is negligible)
			float f = fsynctics * 32.0F;
			velocity.z += fsynctics * 0.5F;
			position += velocity * f;

			const Handle<GameMap>& map = world.GetMap();
			SPAssert(map);

			// Collision
			IntVector3 lp = position.Floor();

			if (map->ClipWorld(lp.x, lp.y, lp.z)) {
				IntVector3 lp2 = oldPos.Floor();
				if (lp.z != lp2.z && ((lp.x == lp2.x && lp.y == lp2.y)
					|| !map->ClipWorld(lp.x, lp.y, lp2.z)))
					velocity.z = -velocity.z;
				else if (lp.x != lp2.x && ((lp.y == lp2.y && lp.z == lp2.z)
					|| !map->ClipWorld(lp2.x, lp.y, lp.z)))
					velocity.x = -velocity.x;
				else if (lp.y != lp2.y && ((lp.x == lp2.x && lp.z == lp2.z)
					|| !map->ClipWorld(lp.x, lp2.y, lp.z)))
					velocity.y = -velocity.y;

				position = oldPos; // set back to old position
				velocity *= 0.36F; // lose some velocity due to friction
			}

			if (map->ClipBox(position.x, position.y, position.z)) {
				velocity.z -= fsynctics * 6.0F;
				position += velocity * f;
			}

			SetPosition(position);
		}

		void Player::MovePlayer(float fsynctics) {
			if (!IsAlive()) {
				MoveCorpse(fsynctics);
				return;
			}

			if (input.jump && IsOnGroundOrWade())
				PlayerJump();

			float f = fsynctics; // player acceleration scalar
			if (airborne)
				f *= 0.1F;
			else if (input.crouch)
				f *= 0.3F;
			else if (IsZoomed() || input.sneak)
				f *= 0.5F;
			else if (input.sprint)
				f *= 1.3F;

			if ((input.moveForward || input.moveBackward) && (input.moveRight || input.moveLeft))
				f *= sqrtf(0.5F); // if strafe + forward/backwards then limit diagonal velocity

			Vector3 front = GetFront();
			if (input.moveForward) {
				velocity.x += front.x * f;
				velocity.y += front.y * f;
			} else if (input.moveBackward) {
				velocity.x -= front.x * f;
				velocity.y -= front.y * f;
			}

			Vector3 right = GetRight();
			if (input.moveLeft) {
				velocity.x -= right.x * f;
				velocity.y -= right.y * f;
			} else if (input.moveRight) {
				velocity.x += right.x * f;
				velocity.y += right.y * f;
			}

			// this is a linear approximation that's done in pysnip
			// accurate computation is not difficult
			f = fsynctics + 1.0F;
			velocity.z += fsynctics;
			velocity.z /= f; // air friction

			if (wade) // water friction
				f = fsynctics * 6.0F + 1.0F;
			else if (!airborne) // ground friction
				f = fsynctics * 4.0F + 1.0F;

			velocity.x /= f;
			velocity.y /= f;

			float f2 = velocity.z;
			BoxClipMove(fsynctics);

			// hit ground... check if hurt
			if (!velocity.z && f2 > FALL_SLOW_DOWN) {
				velocity.x *= 0.5F;
				velocity.y *= 0.5F;

				bool hurt = f2 > FALL_DAMAGE_VELOCITY;
				if (world.GetListener())
					world.GetListener()->PlayerLanded(*this, hurt);
			}

			if (IsOnGroundOrWade()) {
				// count move distance
				f = fsynctics * 32.0F;
				float dx = f * velocity.x;
				float dy = f * velocity.y;
				float dist = sqrtf(dx*dx + dy*dy);
				moveDistance += dist * 0.3F;

				bool madeFootstep = false;
				while (moveDistance > 1.0F) {
					moveSteps++;
					moveDistance -= 1.0F;

					if (world.GetListener() && !madeFootstep) {
						if (!input.crouch && !input.sneak && !IsZoomed())
							world.GetListener()->PlayerMadeFootstep(*this);
						madeFootstep = true;
					}
				}
			}
		}

		bool Player::TryUncrouch() {
			SPADES_MARK_FUNCTION();

			float x1 = position.x + 0.45F;
			float x2 = position.x - 0.45F;
			float y1 = position.y + 0.45F;
			float y2 = position.y - 0.45F;
			float z1 = position.z + 2.25F;
			float z2 = position.z - 1.35F;

			const Handle<GameMap>& map = world.GetMap();
			SPAssert(map);

			// lower feet
			if (airborne && !(map->ClipBox(x1, y1, z1)
				|| map->ClipBox(x2, y1, z1)
				|| map->ClipBox(x1, y2, z1)
				|| map->ClipBox(x2, y2, z1)))
				return true;
			else if (!(map->ClipBox(x1, y1, z2)
				|| map->ClipBox(x2, y1, z2)
				|| map->ClipBox(x1, y2, z2)
				|| map->ClipBox(x2, y2, z2))) {
				position.z -= 0.9F;
				eye.z -= 0.9F;
				return true;
			}
			return false;
		}

		void Player::RepositionPlayer(const spades::Vector3& pos) {
			SPADES_MARK_FUNCTION();

			SetPosition(pos);
			float f = lastClimbTime - world.GetTime();
			float f2 = 0.25F;
			if (f > -f2)
				eye.z += (f + f2) / f2;
		}

		bool Player::IsReadyToUseTool() {
			SPADES_MARK_FUNCTION_DEBUG();
			switch (tool) {
				case ToolSpade: return true;
				case ToolBlock: return world.GetTime() > nextBlockTime && blockStocks > 0;
				case ToolGrenade: return world.GetTime() > nextGrenadeTime && grenades > 0;
				case ToolWeapon: return weapon->IsReadyToShoot();
				default: return false;
			}
		}

		bool Player::IsToolSelectable(ToolType type) {
			SPADES_MARK_FUNCTION_DEBUG();
			switch (type) {
				case ToolSpade: return true;
				case ToolBlock: return blockStocks > 0;
				case ToolGrenade: return grenades > 0;
				case ToolWeapon: return weapon->IsSelectable();
				default: return false;
			}
		}

		float Player::GetTimeToRespawn() { return respawnTime - world.GetTime(); }
		float Player::GetTimeToNextSpade() { return nextSpadeTime - world.GetTime(); }
		float Player::GetTimeToNextDig() { return nextDigTime - world.GetTime(); }
		float Player::GetTimeToNextBlock() { return nextBlockTime - world.GetTime(); }
		float Player::GetTimeToNextGrenade() { return nextGrenadeTime - world.GetTime(); }

		float Player::GetToolPrimaryDelay() {
			SPADES_MARK_FUNCTION_DEBUG();
			switch (tool) {
				case ToolSpade: return 0.2F;
				case ToolBlock:
				case ToolGrenade: return 0.5F;
				case ToolWeapon: return weapon->GetDelay();
				default: SPInvalidEnum("tool", tool);
			}
		}

		float Player::GetToolSecondaryDelay() {
			SPADES_MARK_FUNCTION_DEBUG();
			switch (tool) {
				case ToolSpade: return 1.0F;
				case ToolBlock: return GetToolPrimaryDelay();
				default: SPInvalidEnum("tool", tool);
			}
		}

		float Player::GetSpadeAnimationProgress() {
			SPADES_MARK_FUNCTION_DEBUG();
			SPAssert(tool == ToolSpade);
			SPAssert(weapInput.primary);
			return 1.0F - (GetTimeToNextSpade() / GetToolPrimaryDelay());
		}

		float Player::GetDigAnimationProgress() {
			SPADES_MARK_FUNCTION_DEBUG();
			SPAssert(tool == ToolSpade);
			SPAssert(weapInput.secondary);
			return 1.0F - (GetTimeToNextDig() / GetToolSecondaryDelay());
		}

		float Player::GetGrenadeCookTime() { return world.GetTime() - grenadeTime; }

		void Player::KilledBy(KillType type, Player& killer, int respawnTime) {
			SPADES_MARK_FUNCTION();

			health = 0;
			weapon->SetShooting(false);

			// if local player is killed while cooking grenade, drop the live grenade.
			if (IsLocalPlayer() && IsCookingGrenade())
				ThrowGrenade();

			if (world.GetListener())
				world.GetListener()->PlayerKilledPlayer(killer, *this, type);

			blockCursorDragging = false; // do death cleanup

			input = PlayerInput();
			weapInput = WeaponInput();
			this->respawnTime = world.GetTime() + respawnTime;
		}

		std::string Player::GetTeamName() { return world.GetTeamName(teamId); }
		std::string Player::GetName() { return world.GetPlayerName(playerId); }
		int Player::GetScore() { return world.GetPlayerScore(playerId); }

		IntVector3 Player::GetColor() {
			return IsSpectator() ? MakeIntVector3(200, 200, 200) : world.GetTeamColor(teamId);
		}

		Player::HitBoxes Player::GetHitBoxes() {
			SPADES_MARK_FUNCTION_DEBUG();

			Player::HitBoxes hb;

			Vector3 o = GetFront();

			float yaw = atan2f(o.y, o.x) + M_PI_F * 0.5F;
			float pitch = -atan2f(o.z, o.GetLength2D());

			// lower axis
			Matrix4 const lower = Matrix4::Translate(GetOrigin())
				* Matrix4::Rotate(MakeVector3(0, 0, 1), yaw);
			Matrix4 const torso = lower
				* Matrix4::Translate(0, 0, -(input.crouch ? 0.55F : 1));
			Matrix4 const head = torso
				* Matrix4::Rotate(MakeVector3(1, 0, 0), pitch);

			if (input.crouch) {
				hb.limbs[0] = AABB3(-0.4F, -0.1F, -0.2F, 0.3F, 0.4F, 0.8F);
				hb.limbs[1] = AABB3(0.1F, -0.1F, -0.2F, 0.3F, 0.4F, 0.8F);
				hb.torso = AABB3(-0.4F, -0.1F, -0.1F, 0.8F, 0.8F, 0.7F);
				hb.limbs[2] = AABB3(-0.6F, -0.15F, -0.1F, 1.2F, 0.3F, 0.7F);
				hb.head = AABB3(-0.3F, -0.3F, -0.6F, 0.6F, 0.6F, 0.6F);
			} else {
				hb.limbs[0] = AABB3(-0.4F, -0.2F, -0.15F, 0.3F, 0.4F, 1.2F);
				hb.limbs[1] = AABB3(0.1F, -0.2F, -0.15F, 0.3F, 0.4F, 1.2F);
				hb.torso = AABB3(-0.4F, -0.2F, 0.0F, 0.8F, 0.4F, 0.9F);
				hb.limbs[2] = AABB3(-0.6F, -0.15F, 0.0F, 1.2F, 0.3F, 0.9F);
				hb.head = AABB3(-0.3F, -0.3F, -0.6F, 0.6F, 0.6F, 0.6F);
			}

			hb.limbs[0] = lower * hb.limbs[0];
			hb.limbs[1] = lower * hb.limbs[1];
			hb.torso = torso * hb.torso;
			hb.limbs[2] = torso * hb.limbs[2];
			hb.head = head * hb.head;

			return hb;
		}

		Weapon& Player::GetWeapon() {
			SPADES_MARK_FUNCTION();
			SPAssert(weapon);
			return *weapon;
		}

		void Player::SetWeaponType(WeaponType weap) {
			SPADES_MARK_FUNCTION_DEBUG();
			if (this->weapon->GetWeaponType() == weap)
				return;
			this->weapon.reset(Weapon::CreateWeapon(weap, *this, *world.GetGameProperties()));
			this->weaponType = weap;
		}

		bool Player::OverlapsWith(const spades::AABB3& box) {
			SPADES_MARK_FUNCTION_DEBUG();
			float offset, m;
			if (input.crouch) {
				offset = 0.45F;
				m = 0.9F;
			} else {
				offset = 0.9F;
				m = 1.35F;
			}
			m -= 0.5F;
			AABB3 playerBox(eye.x - 0.45F, eye.y - 0.45F, eye.z, 0.9F, 0.9F, offset + m);
			return box.Intersects(playerBox);
		}

		bool Player::OverlapsWithBlock(const spades::IntVector3& v) {
			SPADES_MARK_FUNCTION_DEBUG();
			return OverlapsWith(AABB3(v.x, v.y, v.z, 1, 1, 1));
		}

#pragma mark - Block Construction

		static bool VectorCollision(Vector3 v1, Vector3 v2, float distance) {
			return (fabsf(v1.x - v2.x) < distance &&
					fabsf(v1.y - v2.y) < distance &&
			        fabsf(v1.z - v2.z) < distance);
		}

		bool Player::Collision3D(spades::IntVector3 v, float distance) {
			return VectorCollision(position, MakeVector3(v) + 0.5F, distance);
		}
	} // namespace client
} // namespace spades