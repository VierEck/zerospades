/*
 Copyright (c) 2013 yvt
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

#include "Client.h"

#include <Core/Settings.h>
#include <Core/Strings.h>

#include "IAudioChunk.h"
#include "IAudioDevice.h"

#include "ChatWindow.h"
#include "ClientUI.h"
#include "Corpse.h"
#include "LimboView.h"
#include "MapView.h"
#include "PaletteView.h"

#include "GameMap.h"
#include "Weapon.h"
#include "World.h"

#include "NetClient.h"

using namespace std;

DEFINE_SPADES_SETTING(cg_mouseSensitivity, "1");
DEFINE_SPADES_SETTING(cg_zoomedMouseSensScale, "1");
DEFINE_SPADES_SETTING(cg_mouseSensScale, "0");
DEFINE_SPADES_SETTING(cg_mouseAccel, "1");
DEFINE_SPADES_SETTING(cg_mouseExpPower, "1");
DEFINE_SPADES_SETTING(cg_invertMouseY, "0");

DEFINE_SPADES_SETTING(cg_holdAimDownSight, "0");

DEFINE_SPADES_SETTING(cg_keyAttack, "LeftMouseButton");
DEFINE_SPADES_SETTING(cg_keyAltAttack, "RightMouseButton");
DEFINE_SPADES_SETTING(cg_keyToolSpade, "1");
DEFINE_SPADES_SETTING(cg_keyToolBlock, "2");
DEFINE_SPADES_SETTING(cg_keyToolWeapon, "3");
DEFINE_SPADES_SETTING(cg_keyToolGrenade, "4");
DEFINE_SPADES_SETTING(cg_keyReloadWeapon, "r");
DEFINE_SPADES_SETTING(cg_keyFlashlight, "f");
DEFINE_SPADES_SETTING(cg_keyLastTool, "");

DEFINE_SPADES_SETTING(cg_keyMoveLeft, "a");
DEFINE_SPADES_SETTING(cg_keyMoveRight, "d");
DEFINE_SPADES_SETTING(cg_keyMoveForward, "w");
DEFINE_SPADES_SETTING(cg_keyMoveBackward, "s");
DEFINE_SPADES_SETTING(cg_keyJump, "Space");
DEFINE_SPADES_SETTING(cg_keyCrouch, "Control");
DEFINE_SPADES_SETTING(cg_keySprint, "Shift");
DEFINE_SPADES_SETTING(cg_keySneak, "v");

DEFINE_SPADES_SETTING(cg_keyCaptureColor, "e");
DEFINE_SPADES_SETTING(cg_keyGlobalChat, "t");
DEFINE_SPADES_SETTING(cg_keyTeamChat, "y");
DEFINE_SPADES_SETTING(cg_keyZoomChatLog, "h");
DEFINE_SPADES_SETTING(cg_keyChangeMapScale, "m");
DEFINE_SPADES_SETTING(cg_keyToggleMapZoom, "n");
DEFINE_SPADES_SETTING(cg_keyToggleHitTestZoom, "g");
DEFINE_SPADES_SETTING(cg_keyScoreboard, "Tab");
DEFINE_SPADES_SETTING(cg_keyLimbo, "l");

DEFINE_SPADES_SETTING(cg_keyScreenshot, "0");
DEFINE_SPADES_SETTING(cg_keySceneshot, "9");
DEFINE_SPADES_SETTING(cg_keySaveMap, "8");

DEFINE_SPADES_SETTING(cg_switchToolByWheel, "1");
DEFINE_SPADES_SETTING(cg_debugCorpse, "0");

SPADES_SETTING(cg_manualFocus);
DEFINE_SPADES_SETTING(cg_keyAutoFocus, "MiddleMouseButton");

SPADES_SETTING(s_volume);
SPADES_SETTING(cg_debugHitTest);

namespace spades {
	namespace client {

		bool Client::WantsToBeClosed() { return readyToClose; }
		void Client::Closing() { SPADES_MARK_FUNCTION(); }

		bool Client::NeedsAbsoluteMouseCoordinate() {
			SPADES_MARK_FUNCTION();
			if (scriptedUI->NeedsInput())
				return true;
			if (!world)
				return true; // now loading.
			if (IsLimboViewActive())
				return true;

			return false;
		}

		void Client::MouseEvent(float x, float y) {
			SPADES_MARK_FUNCTION();

			if (scriptedUI->NeedsInput()) {
				scriptedUI->MouseEvent(x, y);
				return;
			}

			if (IsLimboViewActive()) {
				limbo->MouseEvent(x, y);
				return;
			}

			auto cameraMode = GetCameraMode();

			switch (cameraMode) {
				case ClientCameraMode::None:
				case ClientCameraMode::NotJoined:
				case ClientCameraMode::FirstPersonFollow:
					// No-op
					break;
				case ClientCameraMode::Free:
				case ClientCameraMode::ThirdPersonLocal:
				case ClientCameraMode::ThirdPersonFollow: {
					// Move the third-person or free-floating camera
					x = -x;
					if (!cg_invertMouseY)
						y = -y;

					auto& state = followAndFreeCameraState;

					state.yaw -= x * 0.003F;
					state.pitch -= y * 0.003F;
					if (state.pitch > DEG2RAD(89))
						state.pitch = DEG2RAD(89);
					if (state.pitch < -DEG2RAD(89))
						state.pitch = -DEG2RAD(89);
					state.yaw = fmodf(state.yaw, DEG2RAD(360));
					break;
				}
				case ClientCameraMode::FirstPersonLocal: {
					SPAssert(world);
					SPAssert(world->GetLocalPlayer());

					if (!x && !y)
						break;

					Player& p = world->GetLocalPlayer().value();
					if (p.IsAlive()) {
						float sensitivity = cg_mouseSensitivity;

						// scale by FOV
						float zoomScale = GetAimDownZoomScale();
						if (zoomScale != 1) {
							float k = atanf(tanf(lastSceneDef.fovX * 0.5F));

							x *= k;
							y *= k;
						}

						// TODO: mouse acceleration is framerate dependant
						if (cg_mouseAccel) {
							float rad = x * x + y * y;
							if (rad > 0.0F) {
								if ((float)cg_mouseExpPower < 0.001F ||
								    isnan((float)cg_mouseExpPower)) {
									SPLog("Invalid cg_mouseExpPower value, resetting to 1.0");
									cg_mouseExpPower = 1.0F;
								}

								float const fExp = powf(renderer->ScreenWidth() * 0.1F, 2);
								rad = powf(rad / fExp, (float)cg_mouseExpPower * 0.5F - 0.5F);

								// shouldn't happen...
								if (isnan(rad))
									rad = 1.0F;

								x *= rad;
								y *= rad;
							}
						}
						
						float aimDownState = GetAimDownState();
						if (aimDownState > 0.0F) {
							float scale = cg_zoomedMouseSensScale;
							scale = powf(scale, aimDownState);

							x *= scale;
							y *= scale;
						}

						x *= sensitivity;
						y *= sensitivity;

						float sensScale;
						switch ((int)cg_mouseSensScale) {
							case 1: // quake/source
								sensScale = DEG2RAD(0.022F);
								break;
							case 2: // overwatch
								sensScale = DEG2RAD(0.0066F);
								break;
							case 3: // valorant
								sensScale = DEG2RAD(0.07F);
								break;
							default: sensScale = 0.003F; break;
						}

						x *= sensScale;
						y *= sensScale;

						if (!cg_invertMouseY)
							y = -y;

						p.Turn(x, y);
					}

					break;
				}
			}
		}

		void Client::WheelEvent(float x, float y) {
			SPADES_MARK_FUNCTION();

			if (scriptedUI->NeedsInput()) {
				scriptedUI->WheelEvent(x, y);
				return;
			}

			if (y > 0.5F) {
				KeyEvent("WheelDown", true);
				KeyEvent("WheelDown", false);
			} else if (y < -0.5F) {
				KeyEvent("WheelUp", true);
				KeyEvent("WheelUp", false);
			}
		}

		void Client::TextInputEvent(const std::string& ch) {
			SPADES_MARK_FUNCTION();

			if (scriptedUI->NeedsInput() && !scriptedUI->IsIgnored(ch)) {
				scriptedUI->TextInputEvent(ch);
				return;
			}

			// we don't get "/" here anymore
		}

		void Client::TextEditingEvent(const std::string& ch, int start, int len) {
			SPADES_MARK_FUNCTION();

			if (scriptedUI->NeedsInput() && !scriptedUI->IsIgnored(ch)) {
				scriptedUI->TextEditingEvent(ch, start, len);
				return;
			}
		}

		bool Client::AcceptsTextInput() {
			SPADES_MARK_FUNCTION();
			if (scriptedUI->NeedsInput())
				return scriptedUI->AcceptsTextInput();
			return false;
		}

		AABB2 Client::GetTextInputRect() {
			SPADES_MARK_FUNCTION();
			if (scriptedUI->NeedsInput())
				return scriptedUI->GetTextInputRect();
			return AABB2();
		}

		static bool CheckKey(const std::string& cfg, const std::string& input) {
			if (cfg.empty())
				return false;

			static const std::string space1("space");
			static const std::string space2("spacebar");
			static const std::string space3("spacekey");

			if (EqualsIgnoringCase(cfg, space1) ||
				EqualsIgnoringCase(cfg, space2) ||
			    EqualsIgnoringCase(cfg, space3)) {
				if (input == " ")
					return true;
			} else {
				if (EqualsIgnoringCase(cfg, input))
					return true;
			}
			return false;
		}

		void Client::KeyEvent(const std::string& name, bool down) {
			SPADES_MARK_FUNCTION();

			if (scriptedUI->NeedsInput()) {
				if (!scriptedUI->IsIgnored(name)) {
					scriptedUI->KeyEvent(name, down);
				} else {
					if (!down)
						scriptedUI->SetIgnored("");
				}
				return;
			}

			if (name == "Escape") {
				if (down) {
					if (inGameLimbo) {
						inGameLimbo = false;
					} else {
						if (GetWorld() == nullptr) { 
							// loading now, abort download, and quit the game immediately.
							readyToClose = true;
						} else {
							scriptedUI->EnterClientMenu();
						}
					}
				}
			} else if (world) {
				if (IsLimboViewActive()) {
					if (down)
						limbo->KeyEvent(name);
					return;
				}

				auto cameraMode = GetCameraMode();

				switch (cameraMode) {
					case ClientCameraMode::None:
					case ClientCameraMode::NotJoined:
					case ClientCameraMode::FirstPersonLocal: break;
					case ClientCameraMode::ThirdPersonLocal:
						if (world->GetLocalPlayer()->IsAlive())
							break;
					case ClientCameraMode::FirstPersonFollow:
					case ClientCameraMode::ThirdPersonFollow:
					case ClientCameraMode::Free:
						if (CheckKey(cg_keyAttack, name)) {
							if (down) {
								if (cameraMode == ClientCameraMode::Free ||
								    cameraMode == ClientCameraMode::ThirdPersonLocal) {
									// Start with the local player
									followedPlayerId = world->GetLocalPlayerIndex().value();
								}
								if (world->GetLocalPlayer()->IsSpectator() ||
								    time - lastAliveTime > 1.3F)
									FollowNextPlayer(false);
							}
							return;
						} else if (CheckKey(cg_keyAltAttack, name)) {
							if (down) {
								if (cameraMode == ClientCameraMode::Free ||
								    cameraMode == ClientCameraMode::ThirdPersonLocal) {
									// Start with the local player
									followedPlayerId = world->GetLocalPlayerIndex().value();
								}
								if (world->GetLocalPlayer()->IsSpectator() ||
								    time - lastAliveTime > 1.3F)
									FollowNextPlayer(true);
							}
							return;
						} else if (CheckKey(cg_keyJump, name) && cameraMode != ClientCameraMode::Free) {
							if (down && GetCameraTargetPlayer().IsAlive())
								followCameraState.firstPerson = !followCameraState.firstPerson;
							return;
						} else if (CheckKey(cg_keyReloadWeapon, name) &&
						           world->GetLocalPlayer()->IsSpectator() &&
						           followCameraState.enabled) {
							if (down) {
								// Unfollow
								followCameraState.enabled = false;
							}
							return;
						}
						break;
				}

				if (world->GetLocalPlayer()) {
					Player& p = world->GetLocalPlayer().value();

					if (name == "-" || name == "+") {
						int volume = s_volume;

						if (name == "-")
							volume = std::max(volume - 10, 0);
						if (name == "+")
							volume = std::min(volume + 10, 100);

						if (down) {
							s_volume = volume;

							auto volstr = "Volume: " + ToString(volume);
							chatWindow->AddMessage(ChatWindow::ColoredMessage(volstr, MsgColorRed));
						}
					}

					if (!p.IsSpectator() && p.IsAlive()) {
						if (p.IsToolBlock() && down) {
							if (paletteView->KeyInput(name))
								return;
						}

						if (name == "P" && down && cg_debugCorpse) {
							auto corp = stmp::make_unique<Corpse>(*renderer, *map, p);
							corp->AddImpulse(p.GetFront() * 32.0F);
							corpses.emplace_back(std::move(corp));

							if (corpses.size() > corpseHardLimit)
								corpses.pop_front();
							else if (corpses.size() > corpseSoftLimit)
								RemoveInvisibleCorpses();
						}
					}

					if (CheckKey(cg_keyToggleHitTestZoom, name) && cg_debugHitTest) {
						if (debugHitTestImage && !p.IsSpectator()) {
							debugHitTestZoom = down;
							Handle<IAudioChunk> c = debugHitTestZoom
							    ? audioDevice->RegisterSound("Sounds/Misc/OpenMap.opus")
							    : audioDevice->RegisterSound("Sounds/Misc/CloseMap.opus");
							audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
						}
					}

					if (CheckKey(cg_keyMoveLeft, name)) {
						playerInput.moveLeft = down;
						keypadInput.left = down;
						playerInput.moveRight = down ? false : keypadInput.right;
					} else if (CheckKey(cg_keyMoveRight, name)) {
						playerInput.moveRight = down;
						keypadInput.right = down;
						playerInput.moveLeft = down ? false : keypadInput.left;
					} else if (CheckKey(cg_keyMoveForward, name)) {
						playerInput.moveForward = down;
						keypadInput.forward = down;
						playerInput.moveBackward = down ? false : keypadInput.backward;
					} else if (CheckKey(cg_keyMoveBackward, name)) {
						playerInput.moveBackward = down;
						keypadInput.backward = down;
						playerInput.moveForward = down ? false : keypadInput.forward;
					} else if (CheckKey(cg_keyCrouch, name)) {
						playerInput.crouch = down;
					} else if (CheckKey(cg_keySprint, name)) {
						playerInput.sprint = down;
					} else if (CheckKey(cg_keySneak, name)) {
						playerInput.sneak = down;
					} else if (CheckKey(cg_keyJump, name)) {
						playerInput.jump = down;
					} else if (CheckKey(cg_keyAttack, name)) {
						weapInput.primary = down;

						if (p.IsToolWeapon() && weapInput.primary && !CanLocalPlayerUseWeapon())
							PlayerDryFiredWeapon(p);
					} else if (CheckKey(cg_keyAltAttack, name)) {
						bool lastVal = weapInput.secondary;
						if (p.IsToolWeapon() && !cg_holdAimDownSight) {
							if (down && !p.GetWeapon().IsReloading())
								weapInput.secondary = !weapInput.secondary;
						} else {
							weapInput.secondary = down;
						}

						if (p.IsToolWeapon() && weapInput.secondary &&
						    !lastVal && CanLocalPlayerUseWeapon()) {
							AudioParam params;
							params.volume = 0.08F;
							Handle<IAudioChunk> c =
							  audioDevice->RegisterSound("Sounds/Weapons/AimDownSightLocal.opus");
							audioDevice->PlayLocal(c.GetPointerOrNull(), MakeVector3(0.4F, -0.3F, 0.5F), params);
						}
					} else if (CheckKey(cg_keyReloadWeapon, name)) {
						if (p.GetTool() == Player::ToolWeapon) {
							Weapon& w = p.GetWeapon();
							if (w.GetAmmo() < w.GetClipSize() && w.GetStock() > 0 &&
							    !(w.IsReloading() || p.IsAwaitingReloadCompletion()) &&
							    !(w.IsShooting() && w.GetAmmo() > 0)) {
								if (weapInput.secondary && !w.IsReloadSlow()) {
									// if we send WeaponInput after sending Reload,
									// server might cancel the reload.
									// https://github.com/infogulch/pyspades/blob/protocol075/pyspades/server.py
									hasDelayedReload = true;
									weapInput.secondary = false;
									return;
								}

								p.Reload();
								net->SendReload();
							}
						}
					} else if (CheckKey(cg_keyToolSpade, name) && down) {
						if (!p.IsSpectator() && p.IsAlive())
							SetSelectedTool(Player::ToolSpade);
					} else if (CheckKey(cg_keyToolBlock, name) && down) {
						if (!p.IsSpectator() && p.IsAlive()) {
							if (p.IsToolSelectable(Player::ToolBlock))
								SetSelectedTool(Player::ToolBlock);
							else
								ShowAlert(_Tr("Client", "Out of Blocks"), AlertType::Error);
						}
					} else if (CheckKey(cg_keyToolWeapon, name) && down) {
						if (!p.IsSpectator() && p.IsAlive()) {
							if (p.IsToolSelectable(Player::ToolWeapon))
								SetSelectedTool(Player::ToolWeapon);
							else
								ShowAlert(_Tr("Client", "Out of Ammo"), AlertType::Error);
						}
					} else if (CheckKey(cg_keyToolGrenade, name) && down) {
						if (!p.IsSpectator() && p.IsAlive()) {
							if (p.IsToolSelectable(Player::ToolGrenade))
								SetSelectedTool(Player::ToolGrenade);
							else
								ShowAlert(_Tr("Client", "Out of Grenades"), AlertType::Error);
						}
					} else if (CheckKey(cg_keyLastTool, name) && down) {
						if (!p.IsSpectator() && p.IsAlive()) {
							if (hasLastTool && p.IsToolSelectable(lastTool)) {
								hasLastTool = false;
								SetSelectedTool(lastTool);
							}
						}
					} else if (CheckKey(cg_keyGlobalChat, name) && down) {
						// global chat
						scriptedUI->EnterGlobalChatWindow();
						scriptedUI->SetIgnored(name);
					} else if (CheckKey(cg_keyTeamChat, name) && down) {
						// team chat
						scriptedUI->EnterTeamChatWindow();
						scriptedUI->SetIgnored(name);
					} else if (CheckKey(cg_keyZoomChatLog, name)) {
						chatWindow->SetExpanded(down);
					} else if (CheckKey(cg_keyCaptureColor, name) && down) {
						if (!p.IsSpectator() && p.IsAlive() && p.IsToolBlock())
							CaptureColor();
					} else if (CheckKey(cg_keyChangeMapScale, name) && down) {
						mapView->SwitchScale();
						Handle<IAudioChunk> c =
						  audioDevice->RegisterSound("Sounds/Misc/SwitchMapZoom.opus");
						audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
					} else if (CheckKey(cg_keyToggleMapZoom, name) && down) {
						Handle<IAudioChunk> c = largeMapView->ToggleZoom()
						    ? audioDevice->RegisterSound("Sounds/Misc/OpenMap.opus")
						    : audioDevice->RegisterSound("Sounds/Misc/CloseMap.opus");
						audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
					} else if (CheckKey(cg_keyScoreboard, name)) {
						scoreboardVisible = down;
					} else if (CheckKey(cg_keyLimbo, name) && down) {
						limbo->SetSelectedTeam(p.GetTeamId());
						limbo->SetSelectedWeapon(p.GetWeapon().GetWeaponType());
						inGameLimbo = true;
					} else if (CheckKey(cg_keySceneshot, name) && down) {
						TakeScreenShot(true);
					} else if (CheckKey(cg_keyScreenshot, name) && down) {
						TakeScreenShot(false);
					} else if (CheckKey(cg_keySaveMap, name) && down) {
						TakeMapShot();
					} else if (CheckKey(cg_keyFlashlight, name) && down) {
						// spectators and dead players should not be able to toggle the flashlight
						if (!p.IsSpectator() && p.IsAlive()) {
							flashlightOn = !flashlightOn;
							flashlightOnTime = time;
							Handle<IAudioChunk> c =
							  audioDevice->RegisterSound("Sounds/Player/Flashlight.opus");
							audioDevice->PlayLocal(c.GetPointerOrNull(), AudioParam());
						}
					} else if (CheckKey(cg_keyAutoFocus, name) && down && (bool)cg_manualFocus) {
						autoFocusEnabled = true;
					} else if (down) {
						bool rev = (int)cg_switchToolByWheel > 0;
						if (name == (rev ? "WheelDown" : "WheelUp")) {
							if ((bool)cg_manualFocus) {
								// When DoF control is enabled,
								// tool switch is overrided by focal length control.
								float dist = 1.0F / targetFocalLength;
								dist = std::min(dist + 0.01F, 1.0F);
								targetFocalLength = 1.0F / dist;
								autoFocusEnabled = false;
							} else if (cg_switchToolByWheel && !p.IsSpectator() && p.IsAlive()) {
								Player::ToolType t = p.GetTool();
								do {
									switch (t) {
										case Player::ToolSpade: t = Player::ToolGrenade; break;
										case Player::ToolBlock: t = Player::ToolSpade; break;
										case Player::ToolWeapon: t = Player::ToolBlock; break;
										case Player::ToolGrenade: t = Player::ToolWeapon; break;
									}
								} while (!p.IsToolSelectable(t));
								SetSelectedTool(t);
							}
						} else if (name == (rev ? "WheelUp" : "WheelDown")) {
							if ((bool)cg_manualFocus) {
								// When DoF control is enabled,
								// tool switch is overrided by focal length control.
								float dist = 1.0F / targetFocalLength;
								// limit to fog max distance
								dist = std::max(dist - 0.01F, 1.0F / 128.0F);
								targetFocalLength = 1.0F / dist;
								autoFocusEnabled = false;
							} else if (cg_switchToolByWheel && !p.IsSpectator() && p.IsAlive()) {
								Player::ToolType t = p.GetTool();
								do {
									switch (t) {
										case Player::ToolSpade: t = Player::ToolBlock; break;
										case Player::ToolBlock: t = Player::ToolWeapon; break;
										case Player::ToolWeapon: t = Player::ToolGrenade; break;
										case Player::ToolGrenade: t = Player::ToolSpade; break;
									}
								} while (!p.IsToolSelectable(t));
								SetSelectedTool(t);
							}
						}
					}
				} else {
					// limbo
				}
			}
		}
	} // namespace client
} // namespace spades