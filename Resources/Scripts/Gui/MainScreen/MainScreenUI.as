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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "MainMenu.as"
#include "CreateProfileScreen.as"

namespace spades {

	class MainScreenUI {
		private Renderer@ renderer;
		private AudioDevice@ audioDevice;
		FontManager@ fontManager;
		MainScreenHelper@ helper;

		spades::ui::UIManager@ manager;

		MainScreenMainMenu@ mainMenu;

		bool shouldExit = false;

		private float time = -1.0F;

		private ConfigItem cg_playerName("cg_playerName");
		private ConfigItem cg_playerNameIsSet("cg_playerNameIsSet", "0");

		MainScreenUI(Renderer@ renderer, AudioDevice@ audioDevice, FontManager@ fontManager, MainScreenHelper@ helper) {
			@this.renderer = renderer;
			@this.audioDevice = audioDevice;
			@this.fontManager = fontManager;
			@this.helper = helper;

			SetupRenderer();

			@manager = spades::ui::UIManager(renderer, audioDevice);
			@manager.RootElement.Font = fontManager.GuiFont;

			@mainMenu = MainScreenMainMenu(this);
			mainMenu.Bounds = manager.RootElement.Bounds;
			manager.RootElement.AddChild(mainMenu);

			// Let the new player choose their IGN
			if (cg_playerName.StringValue != "" and cg_playerName.StringValue != "Deuce")
				cg_playerNameIsSet.IntValue = 1;
			if (not cg_playerNameIsSet.BoolValue) {
				CreateProfileScreen al(mainMenu);
				al.Run();
			}
		}

		void SetupRenderer() {
			// load map
			@renderer.GameMap = GameMap("Maps/Title.vxl");
			renderer.FogColor = Vector3(0.1F, 0.1F, 0.1F);
			renderer.FogDistance = 128.0F;
			time = -1.0F;

			// returned from the client game, so reload the server list.
			if (mainMenu !is null)
				mainMenu.LoadServerList();

			if (manager !is null)
				manager.KeyPanic();
		}

		void MouseEvent(float x, float y) { manager.MouseEvent(x, y); }
		void WheelEvent(float x, float y) { manager.WheelEvent(x, y); }
		void KeyEvent(string key, bool down) { manager.KeyEvent(key, down); }
		void TextInputEvent(string text) { manager.TextInputEvent(text); }
		void TextEditingEvent(string text, int start, int len) {
			manager.TextEditingEvent(text, start, len);
		}

		bool AcceptsTextInput() { return manager.AcceptsTextInput; }
		AABB2 GetTextInputRect() { return manager.TextInputRect; }

		private SceneDefinition SetupCamera(SceneDefinition sceneDef,
			Vector3 eye, Vector3 at, Vector3 up, float fov, float ratio) {
			Vector3 dir = (at - eye).Normalized;
			Vector3 side = Cross(dir, up).Normalized;
			up = -Cross(dir, side);
			sceneDef.viewOrigin = eye;
			sceneDef.viewAxisX = side;
			sceneDef.viewAxisY = up;
			sceneDef.viewAxisZ = dir;
			sceneDef.fovY = fov * PiF / 180.0F;
			sceneDef.fovX = 2.0F * atan(tan(sceneDef.fovY * 0.5F) * ratio);
			return sceneDef;
		}

		void RunFrame(float dt) {
			if (time < 0.0F)
				time = 0.0F;

			float sw = renderer.ScreenWidth;
			float sh = renderer.ScreenHeight;

			SceneDefinition sceneDef;
			float cameraX = time;
			cameraX -= floor(cameraX / 512.0F) * 512.0F;
			cameraX = 512.0F - cameraX;

			Vector3 orig = Vector3(cameraX, 256.0F, 12.0F);
			Vector3 aimAt = Vector3(cameraX + 0.1F, 257.0F, 12.5F);
			Vector3 up = Vector3(0.0F, 0.0F, -1.0F);

			sceneDef = SetupCamera(sceneDef, orig, aimAt, up, 30.0F, sw / sh);
			sceneDef.zNear = 0.1F;
			sceneDef.zFar = 160.0F;
			sceneDef.time = int(time * 1000.0F);
			sceneDef.viewportWidth = int(sw);
			sceneDef.viewportHeight = int(sh);
			sceneDef.denyCameraBlur = true;
			sceneDef.depthOfFieldFocalLength = 100.0F;
			sceneDef.skipWorld = false;

			// fade the map
			float fade = Clamp((time - 1.0F) / 2.2F, 0.0F, 1.0F);
			sceneDef.globalBlur = Clamp((1.0F - (time - 1.0F) / 2.5F), 0.0F, 1.0F);
			if (not mainMenu.IsEnabled)
				sceneDef.globalBlur = Max(sceneDef.globalBlur, 0.5F);

			renderer.StartScene(sceneDef);
			renderer.EndScene();

			// fade the map (draw)
			if (fade < 1.0F) {
				renderer.ColorNP = Vector4(0.0F, 0.0F, 0.0F, 1.0F - fade);
				renderer.DrawImage(null, AABB2(0.0F, 0.0F, sw, sh));
			}

			// draw title logo
			Image@ img = renderer.RegisterImage("Gfx/Title/Logo.png");
			renderer.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 1.0F);
			renderer.DrawImage(img, Vector2((sw - img.Width) * 0.5F, 64.0F));

			manager.RunFrame(dt);
			manager.Render();

			time += Min(dt, 0.05F);
		}

		void RunFrameLate(float dt) {
			renderer.FrameDone();
			renderer.Flip();
		}

		void Closing() { shouldExit = true; }

		bool WantsToBeClosed() { return shouldExit; }
	}

	/**
	 * The entry point of the main screen.
	 */
	MainScreenUI@ CreateMainScreenUI(Renderer@ renderer, AudioDevice@ audioDevice, FontManager@ fontManager, MainScreenHelper@ helper) {
		return MainScreenUI(renderer, audioDevice, fontManager, helper);
	}
}