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

#include "CreateProfileScreen.as"
#include "ServerList.as"

namespace spades {

	class RefreshButton : spades::ui::SimpleButton {
		RefreshButton(spades::ui::UIManager@ manager) { super(manager); }
		void Render() {
			SimpleButton::Render();

			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;
			Image@ img = r.RegisterImage("Gfx/UI/Refresh.png");
			r.DrawImage(img, pos + (size - Vector2(16.0F, 16.0F)) * 0.5F);
		}
	}

	class ProtocolButton : spades::ui::SimpleButton {
		ProtocolButton(spades::ui::UIManager @manager) {
			super(manager);
			Toggle = true;
		}
	}

	uint8 ToLower(uint8 c) {
		if (c >= uint8(0x41) and c <= uint8(0x5a)) {
			return uint8(c - 0x41 + 0x61);
		} else {
			return c;
		}
	}
	bool StringContainsCaseInsensitive(string text, string pattern) {
		for (int i = text.length - 1; i >= 0; i--)
			text[i] = ToLower(text[i]);
		for (int i = pattern.length - 1; i >= 0; i--)
			pattern[i] = ToLower(pattern[i]);
		return text.findFirst(pattern) >= 0;
	}

	class MainScreenMainMenu : spades::ui::UIElement {
		MainScreenUI@ ui;
		MainScreenHelper@ helper;
		spades::ui::Field@ addressField;

		spades::ui::Button@ protocol3Button;
		spades::ui::Button@ protocol4Button;

		spades::ui::Button@ filterProtocol3Button;
		spades::ui::Button@ filterProtocol4Button;
		spades::ui::Button@ filterEmptyButton;
		spades::ui::Button@ filterFullButton;
		spades::ui::Field@ filterField;

		spades::ui::ListView@ serverList;
		MainScreenServerListLoadingView@ loadingView;
		MainScreenServerListErrorView@ errorView;
		bool loading = false, loaded = false, Replay = false;

		private ConfigItem cg_protocolVersion("cg_protocolVersion", "3");
		private ConfigItem cg_lastQuickConnectHost("cg_lastQuickConnectHost", "127.0.0.1");
		private ConfigItem cg_serverlistSort("cg_serverlistSort", "16385");
		MainScreenServerItem@[]@ savedlist = array<spades::MainScreenServerItem @>();

		MainScreenMainMenu(MainScreenUI@ ui) {
			super(ui.manager);
			@this.ui = ui;
			@this.helper = ui.helper;

			float contentsWidth = 750.0F;
			float contentsLeft = (Manager.ScreenWidth - contentsWidth) * 0.5F;
			float footerPos = Manager.ScreenHeight - 50.0F;
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("MainScreen", "Connect");
				button.Bounds = AABB2(contentsLeft + contentsWidth - 150.0F, 200.0F, 150.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnConnectPressed);
				AddChild(button);
			}
			{
				@addressField = spades::ui::Field(Manager);
				addressField.Bounds = AABB2(contentsLeft, 200.0F, contentsWidth - 240.0F, 30.0F);
				addressField.Placeholder = _Tr("MainScreen", "Quick Connect");
				addressField.Text = cg_lastQuickConnectHost.StringValue;
				@addressField.Changed = spades::ui::EventHandler(this.OnAddressChanged);
				AddChild(addressField);
			}
			{
				@protocol3Button = ProtocolButton(Manager);
				protocol3Button.Bounds =
					AABB2(contentsLeft + contentsWidth - 240.0F + 6.0F, 200.0F, 40.0F, 30.0F);
				protocol3Button.Caption = _Tr("MainScreen", "0.75");
				@protocol3Button.Activated = spades::ui::EventHandler(this.OnProtocol3Pressed);
				protocol3Button.Toggle = true;
				protocol3Button.Toggled = cg_protocolVersion.IntValue == 3;
				AddChild(protocol3Button);
			}
			{
				@protocol4Button = ProtocolButton(Manager);
				protocol4Button.Bounds =
					AABB2(contentsLeft + contentsWidth - 200.0F + 6.0F, 200.0F, 40.0F, 30.0F);
				protocol4Button.Caption = _Tr("MainScreen", "0.76");
				@protocol4Button.Activated = spades::ui::EventHandler(this.OnProtocol4Pressed);
				protocol4Button.Toggle = true;
				protocol4Button.Toggled = cg_protocolVersion.IntValue == 4;
				AddChild(protocol4Button);
			}
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("MainScreen", "Quit");
				button.Bounds = AABB2(contentsLeft + contentsWidth - 100.0F, footerPos, 100.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnQuitPressed);
				AddChild(button);
			}
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("MainScreen", "Credits");
				button.Bounds = AABB2(contentsLeft + contentsWidth - 202.0F, footerPos, 100.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnCreditsPressed);
				AddChild(button);
			}
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("MainScreen", "Setup");
				button.Bounds = AABB2(contentsLeft + contentsWidth - 304.0F, footerPos, 100.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnSetupPressed);
				AddChild(button);
			}
			{
				RefreshButton button(Manager);
				button.Bounds = AABB2(contentsLeft + contentsWidth - 364.0F, footerPos, 30.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnRefreshServerListPressed);
				AddChild(button);
			}
			{
				spades::ui::Label label(Manager);
				label.Text = _Tr("MainScreen", "Filter");
				label.Bounds = AABB2(contentsLeft, footerPos, 50.0F, 30.0F);
				label.Alignment = Vector2(0.0F, 0.5F);
				AddChild(label);
			}
			{
				@filterProtocol3Button = ProtocolButton(Manager);
				filterProtocol3Button.Bounds = AABB2(contentsLeft + 50.0F, footerPos, 40.0F, 30.0F);
				filterProtocol3Button.Caption = _Tr("MainScreen", "0.75");
				@filterProtocol3Button.Activated = spades::ui::EventHandler(this.OnFilterProtocol3Pressed);
				filterProtocol3Button.Toggle = true;
				AddChild(filterProtocol3Button);
			}
			{
				@filterProtocol4Button = ProtocolButton(Manager);
				filterProtocol4Button.Bounds = AABB2(contentsLeft + 90.0F, footerPos, 40.0F, 30.0F);
				filterProtocol4Button.Caption = _Tr("MainScreen", "0.76");
				@filterProtocol4Button.Activated = spades::ui::EventHandler(this.OnFilterProtocol4Pressed);
				filterProtocol4Button.Toggle = true;
				AddChild(filterProtocol4Button);
			}
			{
				@filterEmptyButton = ProtocolButton(Manager);
				filterEmptyButton.Bounds = AABB2(contentsLeft + 135.0F, footerPos, 50.0F, 30.0F);
				filterEmptyButton.Caption = _Tr("MainScreen", "Empty");
				@filterEmptyButton.Activated = spades::ui::EventHandler(this.OnFilterEmptyPressed);
				filterEmptyButton.Toggle = true;
				AddChild(filterEmptyButton);
			}
			{
				@filterFullButton = ProtocolButton(Manager);
				filterFullButton.Bounds = AABB2(contentsLeft + 185.0F, footerPos, 70.0F, 30.0F);
				filterFullButton.Caption = _Tr("MainScreen", "Not Full");
				@filterFullButton.Activated = spades::ui::EventHandler(this.OnFilterFullPressed);
				filterFullButton.Toggle = true;
				AddChild(filterFullButton);
			}
			{
				@filterField = spades::ui::Field(Manager);
				filterField.Bounds = AABB2(contentsLeft + 260.0F, footerPos, 120.0F, 30.0F);
				filterField.Placeholder = _Tr("MainScreen", "Filter");
				@filterField.Changed = spades::ui::EventHandler(this.OnFilterTextChanged);
				AddChild(filterField);
			}
			{
				@serverList = spades::ui::ListView(Manager);
				serverList.Bounds = AABB2(contentsLeft, 270.0F, contentsWidth, footerPos - 280.0F);
				AddChild(serverList);
			}
			{
				ServerListHeader header(Manager);
				header.Bounds = AABB2(contentsLeft + 2.0F, 240.0F, 290.0F, 30.0F);
				header.Text = _Tr("MainScreen", "Server Name");
				@header.Activated = spades::ui::EventHandler(this.SortServerListByName);
				AddChild(header);
			}
			{
				ServerListHeader header(Manager);
				header.Bounds = AABB2(contentsLeft + 290.0F, 240.0F, 40.0F, 30.0F);
				header.Text = _Tr("MainScreen", "Slots");
				@header.Activated = spades::ui::EventHandler(this.SortServerListByNumPlayers);
				AddChild(header);
			}
			{
				ServerListHeader header(Manager);
				header.Bounds = AABB2(contentsLeft + 330.0F, 240.0F, 140.0F, 30.0F);
				header.Text = _Tr("MainScreen", "Map Name");
				@header.Activated = spades::ui::EventHandler(this.SortServerListByMapName);
				AddChild(header);
			}
			{
				ServerListHeader header(Manager);
				header.Bounds = AABB2(contentsLeft + 470.0F, 240.0F, 100.0F, 30.0F);
				header.Text = _Tr("MainScreen", "Game Mode");
				@header.Activated = spades::ui::EventHandler(this.SortServerListByGameMode);
				AddChild(header);
			}
			{
				ServerListHeader header(Manager);
				header.Bounds = AABB2(contentsLeft + 570.0F, 240.0F, 60.0F, 30.0F);
				header.Text = _Tr("MainScreen", "Ver.");
				@header.Activated = spades::ui::EventHandler(this.SortServerListByProtocol);
				AddChild(header);
			}
			{
				ServerListHeader header(Manager);
				header.Bounds = AABB2(contentsLeft + 630.0F, 240.0F, 60.0F, 30.0F);
				header.Text = _Tr("MainScreen", "Loc.");
				@header.Activated = spades::ui::EventHandler(this.SortServerListByCountry);
				AddChild(header);
			}
			{
				ServerListHeader header(Manager);
				header.Bounds = AABB2(contentsLeft + 690.0F, 240.0F, 60.0F, 30.0F);
				header.Text = _Tr("MainScreen", "Ping");
				@header.Activated = spades::ui::EventHandler(this.SortServerListByPing);
				AddChild(header);
			}
			{
				@loadingView = MainScreenServerListLoadingView(Manager);
				loadingView.Bounds = AABB2(contentsLeft, 240.0F, contentsWidth, 100.0F);
				loadingView.Visible = false;
				AddChild(loadingView);
			}
			{
				@errorView = MainScreenServerListErrorView(Manager);
				errorView.Bounds = AABB2(contentsLeft, 240.0F, contentsWidth, 100.0F);
				errorView.Visible = false;
				AddChild(errorView);
			}
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("MainScreen", "Demo List / Server List");
				button.Bounds = AABB2(contentsLeft, 165, 180.f, 30.f);
				@button.Activated = spades::ui::EventHandler(this.OnChangeListPressed);
				AddChild(button);
            }
			LoadServerList();
		}

		void LoadServerList() {
			if (loading)
				return;
			loaded = false;
			loading = true;
			@serverList.Model = spades::ui::ListViewModel(); // empty
			errorView.Visible = false;
			loadingView.Visible = true;
			helper.StartQuery(Replay);
		}

		void ServerListItemActivated(ServerListModel@ sender, MainScreenServerItem@ item) {
			if (!Replay) {
				addressField.Text = item.Address;
				cg_lastQuickConnectHost = addressField.Text;
			} else {
				addressField.Text = item.Name;
			}
			if (item.Protocol == "0.75") {
				SetProtocolVersion(3);
			} else if (item.Protocol == "0.76") {
				SetProtocolVersion(4);
			}
			addressField.SelectAll();
		}

		void ServerListItemDoubleClicked(ServerListModel@ sender, MainScreenServerItem@ item) {
			ServerListItemActivated(sender, item);

			// Double-click to connect
			Connect();
		}

		void ServerListItemRightClicked(ServerListModel@ sender, MainScreenServerItem@ item) {
			helper.SetServerFavorite(item.Address, !item.Favorite);
			UpdateServerList();
		}

		private void SortServerListByPing(spades::ui::UIElement@ sender) { 
			if (Replay) {
				return;
			}
			SortServerList(0); 
		}
		private void SortServerListByNumPlayers(spades::ui::UIElement@ sender) { 
			if (Replay) {
				return;
			}
			SortServerList(1); 
		}
		private void SortServerListByName(spades::ui::UIElement@ sender) { SortServerList(2); }
		private void SortServerListByMapName(spades::ui::UIElement@ sender) {
			if (Replay) {
				return;
			}
			SortServerList(3); 
		}
		private void SortServerListByGameMode(spades::ui::UIElement@ sender) { 
			if (Replay) {
				return;
			}
			SortServerList(4); 
		}
		private void SortServerListByProtocol(spades::ui::UIElement@ sender) { SortServerList(5); }
		private void SortServerListByCountry(spades::ui::UIElement@ sender) { 
			if (Replay) {
				return;
			}
			SortServerList(6); 
		}

		private void SortServerList(int keyId) {
			int sort = cg_serverlistSort.IntValue;
			if (int(sort & 0xFFF) == keyId) {
				sort ^= int(0x4000);
			} else {
				sort = keyId;
			}
			cg_serverlistSort = sort;
			UpdateServerList();
		}

		private void UpdateServerList() {
			string key = "";
			switch (cg_serverlistSort.IntValue & 0xFFF) {
				case 0: key = "Ping"; break;
				case 1: key = "NumPlayers"; break;
				case 2: key = "Name"; break;
				case 3: key = "MapName"; break;
				case 4: key = "GameMode"; break;
				case 5: key = "Protocol"; break;
				case 6: key = "Country"; break;
			}
			if (Replay) {
				if (key != "Protocol") {
					key = "Name";
				}
			}
			MainScreenServerItem @[] @list = helper.GetServerList(key, (cg_serverlistSort.IntValue & 0x4000) != 0);
			if ((list is null) or loading) {
				@serverList.Model = spades::ui::ListViewModel(); // empty
				return;
			}

			// filter the server list
			bool filterProtocol3 = filterProtocol3Button.Toggled;
			bool filterProtocol4 = filterProtocol4Button.Toggled;
			bool filterEmpty = filterEmptyButton.Toggled;
			bool filterFull = filterFullButton.Toggled;
			string filterText = filterField.Text;
			savedlist.resize(0);
			for (int i = 0, count = list.length; i < count; i++) {
				MainScreenServerItem@ item = list[i];
				if (filterProtocol3 and (item.Protocol != "0.75"))
					continue;
				if (filterProtocol4 and (item.Protocol != "0.76"))
					continue;
				if (!Replay) {
					if (filterEmpty and (item.NumPlayers > 0))
						continue;
					if (filterFull and (item.NumPlayers >= item.MaxPlayers))
						continue;
				}
				if(filterText.length > 0) {
					if(not (StringContainsCaseInsensitive(item.Name, filterText) or
						StringContainsCaseInsensitive(item.MapName, filterText) or
						StringContainsCaseInsensitive(item.GameMode, filterText))) {
						continue;
					}
				}
				savedlist.insertLast(item);
			}

			ServerListModel model(Manager, savedlist);
			@serverList.Model = model;
			@model.ItemActivated = ServerListItemEventHandler(this.ServerListItemActivated);
			@model.ItemDoubleClicked = ServerListItemEventHandler(this.ServerListItemDoubleClicked);
			@model.ItemRightClicked = ServerListItemEventHandler(this.ServerListItemRightClicked);
			serverList.ScrollToTop();
		}

		private void CheckServerList() {
			if (helper.PollServerListState()) {
				MainScreenServerItem @[] @list = helper.GetServerList("", false);
				if (list is null or list.length == 0) {
					// failed.
					// FIXME: show error message?
					loaded = false;
					loading = false;
					errorView.Visible = true;
					loadingView.Visible = false;
					@serverList.Model = spades::ui::ListViewModel(); // empty
					return;
				}
				loading = false;
				loaded = true;
				errorView.Visible = false;
				loadingView.Visible = false;
				UpdateServerList();
			}
		}

		private void OnAddressChanged(spades::ui::UIElement@ sender) {
			if (Replay) {
				return;
			}
			cg_lastQuickConnectHost = addressField.Text;
		}

		private void SetProtocolVersion(int ver) {
			protocol3Button.Toggled = (ver == 3);
			protocol4Button.Toggled = (ver == 4);
			cg_protocolVersion = ver;
		}

		private void OnProtocol3Pressed(spades::ui::UIElement@ sender) { 
			if (Replay) {
				return;
			}
			SetProtocolVersion(3); 
		}
		private void OnProtocol4Pressed(spades::ui::UIElement@ sender) {
			if (Replay) {
				return;
			}
			SetProtocolVersion(4); 
		}

		private void OnFilterProtocol3Pressed(spades::ui::UIElement@ sender) {
			filterProtocol4Button.Toggled = false;
			UpdateServerList();
		}
		private void OnFilterProtocol4Pressed(spades::ui::UIElement@ sender) {
			filterProtocol3Button.Toggled = false;
			UpdateServerList();
		}
		private void OnFilterFullPressed(spades::ui::UIElement@ sender) {
			if (Replay) {
				return;
			}
			filterEmptyButton.Toggled = false;
			UpdateServerList();
		}
		private void OnFilterEmptyPressed(spades::ui::UIElement@ sender) {
			if (Replay) {
				return;
			}
			filterFullButton.Toggled = false;
			UpdateServerList();
		}
		private void OnFilterTextChanged(spades::ui::UIElement@ sender) { UpdateServerList(); }

		private void OnRefreshServerListPressed(spades::ui::UIElement@ sender) { LoadServerList(); }

		private void OnQuitPressed(spades::ui::UIElement@ sender) { ui.shouldExit = true; }

		private void OnCreditsPressed(spades::ui::UIElement@ sender) {
			AlertScreen al(this, ui.helper.Credits, Min(500.0F, Manager.Renderer.ScreenHeight - 100.0F));
			al.Run();
		}

		private void OnSetupPressed(spades::ui::UIElement@ sender) {
			PreferenceView al(this, PreferenceViewOptions(), ui.fontManager);
			al.Run();
		}

		private void Connect() {
			if (addressField.Text == "") {
				return;
			}
			string DemoFile = ""; 
			string FieldText = addressField.Text;
			if (Replay) {
				bool Found = false; 
				for(int i = 0, count = savedlist.length; i < count; i++) {
					MainScreenServerItem@ item = savedlist[i];
					if (item.Name == addressField.Text) {
						Found = true;
						break;
					}
				}
				if (!Found) {
					return;
				}
				DemoFile = "Demos/" + addressField.Text;
				FieldText = "aos://16777343:32887";
			}
            string msg = helper.ConnectServer(FieldText, cg_protocolVersion.IntValue, Replay, DemoFile);
			if (msg.length > 0) {
				// failde to initialize client.
				AlertScreen al(this, msg);
				al.Run();
			}
		}

		private void OnConnectPressed(spades::ui::UIElement@ sender) { Connect(); }

		void HotKey(string key) {
			if (IsEnabled and key == "Enter") {
				Connect();
			} else if (IsEnabled and key == "Escape") {
				ui.shouldExit = true;
			} else {
				UIElement::HotKey(key);
			}
		}

		void Render() {
			CheckServerList();
			UIElement::Render();

			// check for client error message.
			if (IsEnabled) {
				string msg = helper.GetPendingErrorMessage();
				if (msg.length > 0) {
					// try to maek the "disconnected" message more friendly.
					if (msg.findFirst("Disconnected:") >= 0) {
						int ind1 = msg.findFirst("Disconnected:");
						int ind2 = msg.findFirst("\n", ind1);
						if (ind2 < 0)
							ind2 = msg.length;
						ind1 += "Disconnected:".length;
						msg = msg.substr(ind1, ind2 - ind1);
						msg = _Tr(
							"MainScreen",
							"You were disconnected from the server because of the following reason:\n\n{0}",
							msg);
					}

					// failed to connect.
					AlertScreen al(this, msg);
					al.Run();
				}
			}
		}
		
		private void OnChangeListPressed(spades::ui::UIElement@ sender) {
			//change list
			Replay = !Replay;
			if (Replay) {
				addressField.Text = "";
			} else {
				addressField.Text = cg_lastQuickConnectHost.StringValue;
			}
            LoadServerList();
		}
	}
}