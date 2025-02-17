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

#include "FieldWithHistory.as"

namespace spades {
	// TODO: Remove cvar editing (superseded by the system console) after 0.1.4

	/** Shows cvar's current value when user types something like "/cg_foobar" */
	class CommandFieldConfigValueView : spades::ui::UIElement {
		string[]@ configNames;
		string[] configValues;
		CommandFieldConfigValueView(spades::ui::UIManager@ manager, string[] configNames) {
			super(manager);
			for (uint i = 0, len = configNames.length; i < len; i++)
				configValues.insertLast(ConfigItem(configNames[i]).StringValue);
			@this.configNames = configNames;
		}
		void Render() {
			float maxNameLen = 0.0F;
			float maxDescLen = 20.0F;
			Font@ font = this.Font;
			Renderer@ r = this.Manager.Renderer;
			float rowHeight = 25.0F;

			for (uint i = 0, len = configNames.length; i < len; i++) {
				maxNameLen = Max(maxNameLen, font.Measure(configNames[i]).x);
				maxDescLen = Max(maxDescLen, font.Measure(configValues[i]).x);
			}

			float h = float(configNames.length) * rowHeight + 10.0F;

			Vector2 pos = this.ScreenPosition;
			pos.y -= h;

			r.ColorNP = Vector4(0.0F, 0.0F, 0.0F, 0.5F);
			r.DrawImage(null, AABB2(pos.x, pos.y, maxNameLen + maxDescLen + 20.0F, h));

			for (uint i = 0, len = configNames.length; i < len; i++) {
				font.DrawShadow(configNames[i],
					pos + Vector2(5.0F, 8.0F + float(i) * rowHeight), 1.0F,
						Vector4(1.0F, 1.0F, 1.0F, 0.7F), Vector4(0.0F, 0.0F, 0.0F, 0.3F));
				font.DrawShadow(configValues[i],
					pos + Vector2(15.0F + maxNameLen, 8.0F + float(i) * rowHeight), 1.0F,
						Vector4(1.0F, 1.0F, 1.0F, 1.0F), Vector4(0.0F, 0.0F, 0.0F, 0.4F));
			}
		}
	}

	class CommandField : FieldWithHistory {
		CommandFieldConfigValueView@ valueView;

		CommandField(spades::ui::UIManager@ manager,
			array<spades::ui::CommandHistoryItem @> @history) {
			super(manager, history);
		}

		void OnChanged() {
			FieldWithHistory::OnChanged();

			if (valueView !is null)
				@valueView.Parent = null;
			if (Text.substr(0, 1) == "/" and Text.substr(1, 1) != " ") {
				int whitespace = Text.findFirst(" ");
				if (whitespace < 0)
					whitespace = int(Text.length);

				string input = Text.substr(1, whitespace - 1);
				if (input.length >= 2) {
					string[]@ names = GetAllConfigNames();
					string[] filteredNames;
					for (uint i = 0, len = names.length; i < len; i++) {
						if (StringCommonPrefixLength(input, names[i]) == input.length
							and not ConfigItem(names[i]).IsUnknown) {
							filteredNames.insertLast(names[i]);
							if (filteredNames.length >= 8)
								break; // too many
						}
					}
					if (filteredNames.length > 0) {
						@valueView = CommandFieldConfigValueView(this.Manager, filteredNames);
						valueView.Bounds = AABB2(0.0F, -15.0F, 0.0F, 0.0F);
						@valueView.Parent = this;
					}
				}
			}
		}

		void KeyDown(string key) {
			if (key == "Tab") {
				if (SelectionLength == 0 and SelectionStart == int(Text.length)
					and Text.substr(0, 1) == "/" and Text.findFirst(" ") < 0) {
					// config variable auto completion
					string input = Text.substr(1);
					string[]@ names = GetAllConfigNames();
					string commonPart;
					bool foundOne = false;
					for (uint i = 0, len = names.length; i < len; i++) {
						if (StringCommonPrefixLength(input, names[i]) == input.length
							and not ConfigItem(names[i]).IsUnknown) {
							if (not foundOne) {
								commonPart = names[i];
								foundOne = true;
							}

							uint commonLen = StringCommonPrefixLength(commonPart, names[i]);
							commonPart = commonPart.substr(0, commonLen);
						}
					}

					if (commonPart.length > input.length) {
						Text = "/" + commonPart;
						Select(Text.length, 0);
					}
				}
			} else {
				FieldWithHistory::KeyDown(key);
			}
		}
	}

	class ClientChatWindow : spades::ui::UIElement {
		private ClientUI@ ui;
		private ClientUIHelper@ helper;

		CommandField@ field;
		spades::ui::Button@ sayButton;
		spades::ui::SimpleButton@ teamButton;
		spades::ui::SimpleButton@ globalButton;

		bool isTeamChat;

		ClientChatWindow(ClientUI@ ui, bool isTeamChat) {
			super(ui.manager);
			@this.ui = ui;
			@this.helper = ui.helper;
			this.isTeamChat = isTeamChat;

			float sw = Manager.ScreenWidth;
			float sh = Manager.ScreenHeight;

			float winW = sw * 0.7F, winH = 66.0F;
			float winX = (sw - winW) * 0.5F;
			float winY = (sh - winH) - 20.0F;

			{
				spades::ui::Label label(Manager);
				label.BackgroundColor = Vector4(0.0F, 0.0F, 0.0F, 0.5F);
				label.Bounds = AABB2(winX - 8.0F, winY - 8.0F, winW + 16.0F, winH + 16.0F);
				AddChild(label);
			}
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("Client", "Say");
				button.Bounds = AABB2(winX + winW - 244.0F, winY + 36.0F, 120.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnSay);
				AddChild(button);
				@sayButton = button;
			}
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("Client", "Cancel");
				button.Bounds = AABB2(winX + winW - 120.0F, winY + 36.0F, 120.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnCancel);
				AddChild(button);
			}
			{
				@field = CommandField(Manager, ui.chatHistory);
				field.Bounds = AABB2(winX, winY, winW, 30.0F);
				field.Placeholder = _Tr("Client", "Chat Text");
				field.MaxLength = 90; // more like 95, but just to make sure
				@field.Changed = spades::ui::EventHandler(this.OnFieldChanged);
				AddChild(field);
				UpdateState();
			}
			{
				@globalButton = spades::ui::SimpleButton(Manager);
				globalButton.Toggle = true;
				globalButton.Toggled = (isTeamChat == false);
				globalButton.Caption = _Tr("Client", "Global");
				globalButton.Bounds = AABB2(winX, winY + 36.0F, 70.0F, 30.0F);
				@globalButton.Activated = spades::ui::EventHandler(this.OnSetGlobal);
				AddChild(globalButton);
			}
			{
				@teamButton = spades::ui::SimpleButton(Manager);
				teamButton.Toggle = true;
				teamButton.Toggled = (isTeamChat == true);
				teamButton.Caption = _Tr("Client", "Team");
				teamButton.Bounds = AABB2(winX + 70.0F, winY + 36.0F, 70.0F, 30.0F);
				@teamButton.Activated = spades::ui::EventHandler(this.OnSetTeam);
				AddChild(teamButton);
			}
			{
                spades::ui::Button ClientButton(Manager);
                ClientButton.Caption = _Tr("Client", "Un/Pause");
                ClientButton.Bounds = AABB2(winX + winW / 2.f - 35.f, winY - 39.f, 70.f, 30.f);
                @ClientButton.Activated = spades::ui::EventHandler(this.OnPause);
                AddChild(ClientButton);
            }
			{
                spades::ui::Button ClientButton(Manager);
                ClientButton.Caption = _Tr("Client", "GoTo");
                ClientButton.Bounds = AABB2(winX + winW / 2.f - 35.f, winY - 70.f, 70.f, 30.f);
                @ClientButton.Activated = spades::ui::EventHandler(this.OnGoTo);
                AddChild(ClientButton);
            }
			{
                spades::ui::Button ClientButton(Manager);
                ClientButton.Caption = _Tr("Client", "Speed");
                ClientButton.Bounds = AABB2(winX + winW / 2.f - 35.f, winY - 100.f, 70.f, 30.f);
                @ClientButton.Activated = spades::ui::EventHandler(this.OnSpeed);
                AddChild(ClientButton);
            }
			{
                spades::ui::Button ClientButton(Manager);
                ClientButton.Caption = _Tr("Client", "FF 15");
                ClientButton.Bounds = AABB2(winX + winW / 2.f + 36.f, winY - 39.f, 70.f, 30.f);
                @ClientButton.Activated = spades::ui::EventHandler(this.OnFFshort);
                AddChild(ClientButton);
            } 
			{
                spades::ui::Button ClientButton(Manager);
                ClientButton.Caption = _Tr("Client", "BB 15");
                ClientButton.Bounds = AABB2(winX + winW / 2.f - 106.f, winY - 39.f, 70.f, 30.f);
                @ClientButton.Activated = spades::ui::EventHandler(this.OnBBshort);
                AddChild(ClientButton);
            }
			{
                spades::ui::Button ClientButton(Manager);
                ClientButton.Caption = _Tr("Client", "NU 1");
                ClientButton.Bounds = AABB2(winX + winW / 2.f + 107.f, winY - 39.f, 70.f, 30.f);
                @ClientButton.Activated = spades::ui::EventHandler(this.OnNUshort);
                AddChild(ClientButton);
            }
			{
                spades::ui::Button ClientButton(Manager);
                ClientButton.Caption = _Tr("Client", "PU 1");
                ClientButton.Bounds = AABB2(winX + winW / 2.f - 176.f, winY - 39.f, 70.f, 30.f);
                @ClientButton.Activated = spades::ui::EventHandler(this.OnPUshort);
                AddChild(ClientButton);
            }
			{
                spades::ui::Button ClientButton(Manager);
                ClientButton.Caption = _Tr("Client", "FForward");
                ClientButton.Bounds = AABB2(winX + winW / 2.f + 36.f, winY - 70.f, 70.f, 30.f);
                @ClientButton.Activated = spades::ui::EventHandler(this.OnFForward);
                AddChild(ClientButton);
            } 
			{
                spades::ui::Button ClientButton(Manager);
                ClientButton.Caption = _Tr("Client", "BBack");
                ClientButton.Bounds = AABB2(winX + winW / 2.f - 106.f, winY - 70.f, 70.f, 30.f);
                @ClientButton.Activated = spades::ui::EventHandler(this.OnBBack);
                AddChild(ClientButton);
            }
			{
                spades::ui::Button ClientButton(Manager);
                ClientButton.Caption = _Tr("Client", "NextUps");
                ClientButton.Bounds = AABB2(winX + winW / 2.f + 107.f, winY - 70.f, 70.f, 30.f);
                @ClientButton.Activated = spades::ui::EventHandler(this.OnNextUps);
                AddChild(ClientButton);
            }
			{
                spades::ui::Button ClientButton(Manager);
                ClientButton.Caption = _Tr("Client", "PrevUps");
                ClientButton.Bounds = AABB2(winX + winW / 2.f - 176.f, winY - 70.f, 70.f, 30.f);
                @ClientButton.Activated = spades::ui::EventHandler(this.OnPrevUps);
                AddChild(ClientButton);
			}
		}

		void UpdateState() { sayButton.Enable = field.Text.length > 0; }

		bool IsTeamChat {
			get final { return isTeamChat; }
			set {
				if (isTeamChat == value)
					return;
				isTeamChat = value;
				teamButton.Toggled = isTeamChat;
				globalButton.Toggled = not isTeamChat;
				UpdateState();
			}
		}

		private void OnSetGlobal(spades::ui::UIElement@ sender) { IsTeamChat = false; }
		private void OnSetTeam(spades::ui::UIElement@ sender) { IsTeamChat = true; }

		private void OnFieldChanged(spades::ui::UIElement@ sender) { UpdateState(); }
		private void Close() { @ui.ActiveUI = null; }

		private void OnCancel(spades::ui::UIElement@ sender) {
			field.Cancelled();
			Close();
		}

		private bool CheckAndSetConfigVariable() {
			string text = field.Text;
			if (text.substr(0, 1) != "/")
				return false;
			int idx = text.findFirst(" ");
			if (idx < 2)
				return false;

			// find variable
			string varname = text.substr(1, idx - 1);
			string[] vars = GetAllConfigNames();

			for (uint i = 0, len = vars.length; i < len; i++) {
				if (vars[i].length == varname.length and
					StringCommonPrefixLength(vars[i], varname) == vars[i].length) {
					// match
					string val = text.substr(idx + 1);
					ConfigItem item(vars[i]);
					item.StringValue = val;
					return true;
				}
			}

			return false;
		}

		private void OnSay(spades::ui::UIElement@ sender) {
			field.CommandSent();
			if (not CheckAndSetConfigVariable()) {
				if (isTeamChat)
					ui.helper.SayTeam(field.Text);
				else
					ui.helper.SayGlobal(field.Text);
			}

			Close();
		}

		void HotKey(string key) {
			if (IsEnabled and key == "Escape") {
				OnCancel(this);
			} else if (IsEnabled and key == "Enter") {
				if (field.Text.length == 0) {
					OnCancel(this);
				} else {
					OnSay(this);
				}
			} else {
				UIElement::HotKey(key);
			}
		}
		
		private void OnPause(spades::ui::UIElement@ sender) {
           ui.helper.SayTeam("pause");
		}
		private void OnGoTo(spades::ui::UIElement@ sender) {
           string str = "gt ";
		   field.Text = str;
           field.Select(GetByteIndexForString(field.Text, str.length));
		}
		private void OnSpeed(spades::ui::UIElement@ sender) {
           string str = "sp ";
		   field.Text = str;
           field.Select(GetByteIndexForString(field.Text, str.length));
		}

		private void OnFFshort(spades::ui::UIElement@ sender) {
           ui.helper.SayTeam("ff 15");
		}
		private void OnBBshort(spades::ui::UIElement@ sender) {
           ui.helper.SayTeam("bb 15");
		}
		private void OnNUshort(spades::ui::UIElement@ sender) {
           ui.helper.SayTeam("nu 1");
		}
		private void OnPUshort(spades::ui::UIElement@ sender) {
           ui.helper.SayTeam("pu 1");
		}

		private void OnFForward(spades::ui::UIElement@ sender) {
           string str = "ff ";
		   field.Text = str;
           field.Select(GetByteIndexForString(field.Text, str.length));
		}
		private void OnBBack(spades::ui::UIElement@ sender) {
           string str = "bb ";
		   field.Text = str;
           field.Select(GetByteIndexForString(field.Text, str.length));
		}
		private void OnNextUps(spades::ui::UIElement@ sender) {
           string str = "nu ";
		   field.Text =str;
           field.Select(GetByteIndexForString(field.Text, str.length));
		}
		private void OnPrevUps(spades::ui::UIElement@ sender) {
           string str = "pu ";
		   field.Text = str;
           field.Select(GetByteIndexForString(field.Text, str.length));
		}
	}
}