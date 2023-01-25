/*
 Copyright (c) 2013 yvt

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

#include <sstream>

#include "Client.h"
#include "Fonts.h"
#include "IAudioChunk.h"
#include "IAudioDevice.h"
#include "IFont.h"
#include "IImage.h"
#include "IRenderer.h"
#include "LimboView.h"
#include "World.h"
#include <Core/Strings.h>

namespace spades {
	namespace client {

		// TODO: make limbo view scriptable using the existing UI framework.

		LimboView::LimboView(Client* client) : client(client), renderer(client->GetRenderer()) {
			// layout now!
			float menuWidth = 200.0F;
			float menuHeight = menuWidth / 8.0F;
			float rowHeight = menuHeight + 3.0F;

			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();

			float spacingWidth = 8.0F;
			float contentsWidth = sw + spacingWidth;
			float maxContentsWidth = 800.0F + spacingWidth;
			if (contentsWidth >= maxContentsWidth)
				contentsWidth = maxContentsWidth;

			float left = (sw - contentsWidth) * 0.5F;
			float top = sh - 150.0F;

			float teamX = left + 10.0F;
			float firstY = top + 35.0F;

			World* w = client->GetWorld();

			items.push_back(MenuItem(MenuTeam1,
				AABB2(teamX, firstY, menuWidth, menuHeight),
				w ? w->GetTeamName(0) : "Team 1"));
			items.push_back(MenuItem(MenuTeam2,
				AABB2(teamX, firstY + rowHeight, menuWidth, menuHeight),
				w ? w->GetTeamName(1) : "Team 2"));
			items.push_back(MenuItem(MenuTeamSpectator,
				AABB2(teamX, firstY + rowHeight * 2.0F, menuWidth, menuHeight),
				w ? w->GetTeamName(2) : "Spectator"));

			float weapX = left + 260.0F;

			items.push_back(MenuItem(MenuWeaponRifle,
				AABB2(weapX, firstY, menuWidth, menuHeight),
				_Tr("Client", "Rifle")));
			items.push_back(MenuItem(MenuWeaponSMG,
				AABB2(weapX, firstY + rowHeight, menuWidth, menuHeight),
				_Tr("Client", "SMG")));
			items.push_back(MenuItem(MenuWeaponShotgun,
				AABB2(weapX, firstY + rowHeight * 2.0F, menuWidth, menuHeight),
				_Tr("Client", "Shotgun")));

			//! The "Spawn" button that you press when you're ready to "spawn".
			items.push_back(MenuItem(MenuSpawn,
				AABB2(left + contentsWidth - 166.0F, firstY + 4.0F, 156.0F, 64.0F),
				_Tr("Client", "Spawn")));

			cursorPos = MakeVector2(sw * 0.5F, sh * 0.5F);

			selectedTeam = 2;
			selectedWeapon = RIFLE_WEAPON;
		}
		LimboView::~LimboView() {}

		void LimboView::MouseEvent(float x, float y) {
			cursorPos.x = Clamp(x, 0.0F, renderer.ScreenWidth());
			cursorPos.y = Clamp(y, 0.0F, renderer.ScreenHeight());
		}

		void LimboView::KeyEvent(const std::string& key) {
			if (key == "LeftMouseButton") {
				for (const auto& item : items) {
					if (item.hover) {
						IAudioDevice& dev = *client->audioDevice;
						Handle<IAudioChunk> c = dev.RegisterSound("Sounds/Feedback/Limbo/Select.opus");
						dev.PlayLocal(c.GetPointerOrNull(), AudioParam());
						switch (item.type) {
							case MenuTeam1: selectedTeam = 0; break;
							case MenuTeam2: selectedTeam = 1; break;
							case MenuTeamSpectator: selectedTeam = 2; break;
							case MenuWeaponRifle: selectedWeapon = RIFLE_WEAPON; break;
							case MenuWeaponSMG: selectedWeapon = SMG_WEAPON; break;
							case MenuWeaponShotgun: selectedWeapon = SHOTGUN_WEAPON; break;
							case MenuSpawn: client->SpawnPressed(); break;
						}
					}
				}
			} else if (key == "1") {
				if (selectedTeam == 2) {
					selectedTeam = 0;
				} else {
					selectedWeapon = RIFLE_WEAPON;
					client->SpawnPressed();
				}
			} else if (key == "2") {
				if (selectedTeam == 2) {
					selectedTeam = 1;
				} else {
					selectedWeapon = SMG_WEAPON;
					client->SpawnPressed();
				}
			} else if (key == "3") {
				if (selectedTeam != 2)
					selectedWeapon = SHOTGUN_WEAPON;
				client->SpawnPressed(); // if we have 3 and are already spec someone wants to spec..
			}
		}

		void LimboView::Update(float dt) {
			// spectator team was actually 255
			if (selectedTeam > 2)
				selectedTeam = 2;
			for (size_t i = 0; i < items.size(); i++) {
				MenuItem& item = items[i];
				item.visible = true;

				switch (item.type) {
					case MenuWeaponRifle:
					case MenuWeaponShotgun:
					case MenuWeaponSMG:
						if (selectedTeam == 2)
							item.visible = false;
					default:;
				}

				bool newHover = item.rect && cursorPos;
				if (!item.visible)
					newHover = false;
				if (newHover && !item.hover) {
					IAudioDevice& dev = *client->audioDevice;
					Handle<IAudioChunk> c = dev.RegisterSound("Sounds/Feedback/Limbo/Hover.opus");
					dev.PlayLocal(c.GetPointerOrNull(), AudioParam());
				}
				item.hover = newHover;
			}
		}

		void LimboView::Draw() {
			IFont& font = client->fontManager->GetGuiFont();

			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();

			float spacingW = 8.0F;
			float contentsW = sw + spacingW;
			float maxContentsW = 800.0F + spacingW;
			if (contentsW >= maxContentsW)
				contentsW = maxContentsW;

			float left = (sw - contentsW) * 0.5F;
			float top = sh - 150.0F;

			Vector4 color = MakeVector4(1, 1, 1, 1);
			Vector4 shadowColor = MakeVector4(0, 0, 0, 0.4F);

			{
				auto msg = _Tr("Client", "Select Team:");
				Vector2 pos = {left + 10.0F, top + 10.0F};
				font.DrawShadow(msg, pos, 1.0F, color, shadowColor);
			}

			if (selectedTeam != 2) {
				auto msg = _Tr("Client", "Select Weapon:");
				Vector2 pos = {left + 260.0F, top + 10.0F};
				font.DrawShadow(msg, pos, 1.0F, color, shadowColor);
			}

			for (const auto& item : items) {
				if (!item.visible)
					continue;

				bool selected = false;
				int index = 0;
				switch (item.type) {
					case MenuTeam1:
					case MenuTeam2:
					case MenuTeamSpectator:
						selected = (selectedTeam == item.type);
						index = (selectedTeam == 2) ? (1 + item.type) : 0;
						break;
					case MenuWeaponRifle:
					case MenuWeaponSMG:
					case MenuWeaponShotgun:
						selected = (selectedWeapon == (item.type - 3));
						index = (selectedTeam != 2) ? (1 + (item.type - 3)) : 0;
						break;
					default: selected = false;
				}

				Vector4 fillColor = {0.2F, 0.2F, 0.2F, 0.5F};
				if (item.hover)
					fillColor = MakeVector4(0.4F, 0.4F, 0.4F, 1) * 0.7F;
				if (selected)
					fillColor = MakeVector4(0.7F, 0.7F, 0.7F, 1) * 0.9F;

				renderer.SetColorAlphaPremultiplied(fillColor);
				renderer.DrawImage(nullptr, item.rect);

				if (item.type == MenuSpawn) {
					auto msg = item.text;
					Vector2 size = font.Measure(msg);
					Vector2 pos = item.rect.min;
					pos.x += (item.rect.GetWidth() - size.x) * 0.5F + 2.0F;
					pos.y += (item.rect.GetHeight() - size.y) * 0.5F;
					font.DrawShadow(msg, pos, 1.0F, color, shadowColor);
				} else {
					auto msg = item.text;
					if (item.type == MenuTeam1)
						msg = client->GetWorld()->GetTeamName(0);
					if (item.type == MenuTeam2)
						msg = client->GetWorld()->GetTeamName(1);

					Vector2 size = font.Measure(msg);
					Vector2 pos = item.rect.min;
					pos.x += 5.0F;
					pos.y += (item.rect.GetHeight() - size.y) * 0.5F;
					font.DrawShadow(msg, pos, 1.0F, color, shadowColor);
					if (index > 0) {
						std::stringstream ss;
						ss << index;
						msg = ss.str();
						pos.x = (item.rect.GetMaxX() - 5.0F) - font.Measure(msg).x;
						font.DrawShadow(msg, pos, 1.0F, color, shadowColor);
					}
				}
			}

			Handle<IImage> cursor = renderer.RegisterImage("Gfx/UI/Cursor.png");

			renderer.SetColorAlphaPremultiplied(color);
			renderer.DrawImage(cursor, AABB2(cursorPos.x - 8, cursorPos.y - 8, 32, 32));
		}
	} // namespace client
} // namespace spades