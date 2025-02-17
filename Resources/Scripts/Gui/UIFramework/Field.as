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

#include "Label.as"

namespace spades {
	namespace ui {

		class FieldCommand {
			int index;
			string oldString;
			string newString;
		}

		class FieldBase : UIElement {
			bool Dragging = false;
			EventHandler@ Changed;
			string Placeholder;
			int MarkPosition = 0;
			int CursorPosition = 0;
			int MaxLength = 255;
			bool DenyNonAscii = false;

			private string text;
			private FieldCommand @[] history;
			private int historyPos = 0; // index to insert next command

			Vector4 TextColor = Vector4(1.0F, 1.0F, 1.0F, 1.0F);
			Vector4 DisabledTextColor = Vector4(1.0F, 1.0F, 1.0F, 0.3F);
			Vector4 PlaceholderColor = Vector4(1.0F, 1.0F, 1.0F, 0.5F);
			Vector4 HighlightColor = Vector4(1.0F, 1.0F, 1.0F, 0.3F);

			Vector2 TextOrigin = Vector2(0.0F, 0.0F);
			float TextScale = 1.0F;

			FieldBase(UIManager@ manager) {
				super(manager);
				IsMouseInteractive = true;
				AcceptsFocus = true;
				@this.Cursor = Cursor(Manager, manager.Renderer.RegisterImage("Gfx/UI/IBeam.png"), Vector2(16.0F, 16.0F));
			}

			string Text {
				get { return text; }
				set {
					text = value;
					EraseUndoHistory();
				}
			}

			private void EraseUndoHistory() {
				history.length = 0;
				historyPos = 0;
			}

			private bool CheckCharType(string s) {
				if (DenyNonAscii) {
					for (uint i = 0, len = s.length; i < len; i++) {
						int c = s[i];
						if ((c & 0x80) != 0)
							return false;
					}
				}
				return true;
			}

			void OnChanged() {
				if (Changed !is null)
					Changed(this);
			}

			int SelectionStart {
				get final { return Min(MarkPosition, CursorPosition); }
				set { Select(value, SelectionEnd - value); }
			}

			int SelectionEnd {
				get final { return Max(MarkPosition, CursorPosition); }
				set { Select(SelectionStart, value - SelectionStart); }
			}

			int SelectionLength {
				get final { return SelectionEnd - SelectionStart; }
				set { Select(SelectionStart, value); }
			}

			string SelectedText {
				get final { return Text.substr(SelectionStart, SelectionLength); }
				set {
					if (not CheckCharType(value))
						return;
					FieldCommand cmd;
					cmd.oldString = this.SelectedText;
					if (cmd.oldString == value)
						return; // no change
					cmd.newString = value;
					cmd.index = this.SelectionStart;
					RunFieldCommand(cmd, true, true);
				}
			}

			private void RunFieldCommand(FieldCommand@ cmd, bool autoSelect, bool addHistory) {
				text = text.substr(0, cmd.index) + cmd.newString +
					   text.substr(cmd.index + cmd.oldString.length);

				if (autoSelect)
					Select(cmd.index, cmd.newString.length);
				if (addHistory) {
					history.length = historyPos;
					history.insertLast(cmd);
					historyPos += 1;
					// limit history length
				}
			}

			private void UndoFieldCommand(FieldCommand @cmd, bool autoSelect) {
				text = text.substr(0, cmd.index) + cmd.oldString +
					   text.substr(cmd.index + cmd.newString.length);
				if (autoSelect)
					Select(cmd.index, cmd.oldString.length);
			}

			private void SetHistoryPos(int index) {
				int p = historyPos;
				FieldCommand @[] @h = history;
				while (p < index) {
					RunFieldCommand(h[p], true, false);
					p++;
				}
				while (p > index) {
					p--;
					UndoFieldCommand(h[p], true);
				}
				historyPos = p;
			}

			bool Undo() {
				if (historyPos == 0)
					return false;
				SetHistoryPos(historyPos - 1);
				return true;
			}

			bool Redo() {
				if (historyPos >= int(history.length))
					return false;
				SetHistoryPos(historyPos + 1);
				return true;
			}

			AABB2 TextInputRect {
				get {
					Vector2 textPos = TextOrigin;
					Vector2 siz = this.Size;
					string text = Text;
					int cursorPos = CursorPosition;
					Font@ font = this.Font;
					float width = font.Measure(text.substr(0, cursorPos)).x;
					float fontHeight = font.Measure("A").y;
					return AABB2(textPos.x + width, textPos.y, siz.x - textPos.x - width, fontHeight);
				}
			}

			private int PointToCharIndex(float x) {
				x -= TextOrigin.x;
				if (x < 0.0F)
					return 0;
				x /= TextScale;
				string text = Text;
				int len = text.length;
				float lastWidth = 0.0F;
				Font @font = this.Font;
				// FIXME: use binary search for better performance?
				int idx = 0;
				for (int i = 1; i <= len; i++) {
					int lastIdx = idx;
					idx = GetByteIndexForString(text, 1, idx);
					float width = font.Measure(text.substr(0, idx)).x;
					if (width > x) {
						if (x < (lastWidth + width) * 0.5F)
							return lastIdx;
						else
							return idx;
					}
					lastWidth = width;
					if (idx >= len)
						return len;
				}
				return len;
			}

			int PointToCharIndex(Vector2 pt) { return PointToCharIndex(pt.x); }
			int ClampCursorPosition(int pos) { return Clamp(pos, 0, Text.length); }

			void Select(int start, int length = 0) {
				MarkPosition = ClampCursorPosition(start);
				CursorPosition = ClampCursorPosition(start + length);
			}

			void SelectAll() { Select(0, Text.length); }

			void BackSpace() {
				if (SelectionLength > 0) {
					SelectedText = "";
				} else {
					int pos = CursorPosition;
					int cIdx = GetCharIndexForString(Text, CursorPosition);
					int bIdx = GetByteIndexForString(Text, cIdx - 1);
					Select(bIdx, pos - bIdx);
					SelectedText = "";
				}
				OnChanged();
			}

			void Delete() {
				if (SelectionLength > 0) {
					SelectedText = "";
				} else if (CursorPosition < int(Text.length)) {
					int pos = CursorPosition;
					int cIdx = GetCharIndexForString(Text, CursorPosition);
					int bIdx = GetByteIndexForString(Text, cIdx + 1);
					Select(bIdx, pos - bIdx);
					SelectedText = "";
				}
				OnChanged();
			}

			void Insert(string text) {
				if (not CheckCharType(text))
					return;
				string oldText = SelectedText;
				SelectedText = text;

				// if text overflows, deny the insertion
				if ((not FitsInBox(Text)) or (int(Text.length) > MaxLength)) {
					SelectedText = oldText;
					return;
				}

				Select(SelectionEnd);
				OnChanged();
			}

			void KeyDown(string key) {
				if (key == "BackSpace") {
					BackSpace();
				} else if (key == "Delete") {
					Delete();
				} else if (key == "Left") {
					if (Manager.IsShiftPressed) {
						int cIdx = GetCharIndexForString(Text, CursorPosition);
						CursorPosition = ClampCursorPosition(GetByteIndexForString(Text, cIdx - 1));
					} else {
						if (SelectionLength == 0) {
							int cIdx = GetCharIndexForString(Text, CursorPosition);
							Select(GetByteIndexForString(Text, cIdx - 1));
						} else {
							Select(SelectionStart);
						}
					}
					return;
				} else if (key == "Right") {
					if (Manager.IsShiftPressed) {
						int cIdx = GetCharIndexForString(Text, CursorPosition);
						CursorPosition = ClampCursorPosition(GetByteIndexForString(Text, cIdx + 1));
					} else {
						if (SelectionLength == 0) {
							int cIdx = GetCharIndexForString(Text, CursorPosition);
							Select(GetByteIndexForString(Text, cIdx + 1));
						} else {
							Select(SelectionEnd);
						}
					}

					return;
				}

				 if (Manager.IsControlPressed or Manager.IsMetaPressed /* for OSX; Cmd + [a-z] */) {
					if (key == "A") {
						SelectAll();
						return;
					} else if (key == "V") {
						Manager.Paste(PasteClipboardEventHandler(this.Insert));
						OnChanged();
					} else if (key == "C") {
						Manager.Copy(this.SelectedText);
					} else if (key == "X") {
						Manager.Copy(this.SelectedText);
						this.SelectedText = "";
						OnChanged();
					} else if (key == "Z") {
						if (Manager.IsShiftPressed) {
							if (Redo())
								OnChanged();
						} else {
							if (Undo())
								OnChanged();
						}
					} else if (key == "W") {
						if (Redo())
							OnChanged();
					}
				}

				Manager.ProcessHotKey(key);
			}
			void KeyUp(string key) {}

			void KeyPress(string text) {
				if (not(Manager.IsControlPressed or Manager.IsMetaPressed))
					Insert(text);
			}
			void MouseDown(MouseButton button, Vector2 clientPosition) {
				if (button != spades::ui::MouseButton::LeftMouseButton)
					return;
				Dragging = true;
				if (Manager.IsShiftPressed)
					MouseMove(clientPosition);
				else
					Select(PointToCharIndex(clientPosition));
			}
			void MouseMove(Vector2 clientPosition) {
				if (Dragging)
					CursorPosition = PointToCharIndex(clientPosition);
			}
			void MouseUp(MouseButton button, Vector2 clientPosition) {
				if (button != spades::ui::MouseButton::LeftMouseButton)
					return;
				Dragging = false;
			}

			bool FitsInBox(string text) {
				return Font.Measure(text).x * TextScale < Size.x - TextOrigin.x;
			}

			void DrawHighlight(Renderer@ r, float x, float y, float w, float h) {
				r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.2F);
				r.DrawImage(null, AABB2(x, y, w, h));
			}

			void DrawBeam(Renderer@ r, float x, float y, float h) {
				float pulse = float((int(Manager.Time * 2.0F)) & 1);

				r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, pulse);
				r.DrawImage(null, AABB2(x - 1.0F, y, 2.0F, h));
			}

			void DrawEditingLine(Renderer@ r, float x, float y, float w, float h) {
				r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.3F);
				r.DrawImage(null, AABB2(x, y + h, w, 2.0F));
			}

			void Render() {
				Renderer@ r = Manager.Renderer;
				Vector2 pos = ScreenPosition;
				Vector2 size = Size;
				Font@ font = this.Font;
				Vector2 textPos = TextOrigin + pos;
				string text = Text;

				string composition = this.EditingText;
				int editStart = this.TextEditingRangeStart;
				int editLen = this.TextEditingRangeLength;

				int markStart = SelectionStart;
				int markEnd = SelectionEnd;

				if (composition.length > 0) {
					this.SelectedText = "";
					markStart = SelectionStart + editStart;
					markEnd = markStart + editLen;
					text = text.substr(0, SelectionStart) + composition + text.substr(SelectionStart);
				}

				if (IsFocused) {
					float fontHeight = font.Measure("A").y;

					// draw selection
					int start = markStart;
					int end = markEnd;
					if (end == start) {
						float x = font.Measure(text.substr(0, start)).x;
						DrawBeam(r, x + textPos.x, textPos.y, fontHeight);
					} else {
						float x1 = font.Measure(text.substr(0, start)).x;
						float x2 = font.Measure(text.substr(0, end)).x;
						DrawHighlight(r, textPos.x + x1, textPos.y, x2 - x1, fontHeight);
					}

					// draw composition underline
					if (composition.length > 0) {
						start = SelectionStart;
						end = start + composition.length;
						float x1 = font.Measure(text.substr(0, start)).x;
						float x2 = font.Measure(text.substr(0, end)).x;
						DrawEditingLine(r, textPos.x + x1, textPos.y, x2 - x1, fontHeight);
					}
				}

				if (text.length == 0) {
					if (IsEnabled)
						font.Draw(Placeholder, textPos, TextScale, PlaceholderColor);
				} else {
					font.Draw(text, textPos, TextScale, IsEnabled ? TextColor : DisabledTextColor);
				}

				UIElement::Render();
			}
		}

		class Field : FieldBase {
			private bool hover;
			Field(UIManager@ manager) {
				super(manager);
				TextOrigin = Vector2(2.0F, 2.0F);
			}
			void MouseEnter() {
				hover = true;
				FieldBase::MouseEnter();
			}
			void MouseLeave() {
				hover = false;
				FieldBase::MouseLeave();
			}
			void Render() {
				// render background
				Renderer@ r = Manager.Renderer;
				Vector2 pos = ScreenPosition;
				Vector2 size = Size;

				r.ColorNP = Vector4(0.0F, 0.0F, 0.0F, IsFocused ? 0.3F : 0.1F);
				r.DrawImage(null, AABB2(pos.x, pos.y, size.x, size.y));

				if (IsFocused)
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.2F);
				else if (hover)
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.1F);
				else
					r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.06F);
				DrawOutlinedRect(r, pos.x, pos.y, pos.x + size.x, pos.y + size.y);

				FieldBase::Render();
			}
		}
	}
}