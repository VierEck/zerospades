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

namespace spades {
	namespace ui {

		class DropDownViewItem : spades::ui::Button {
			int index;
			DropDownViewItem(spades::ui::UIManager@ manager) { super(manager); }
			void Render() {
				Renderer@ r = Manager.Renderer;
				Vector2 pos = ScreenPosition;
				Vector2 size = Size;

				r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, Hover ? 0.12F : 0.0F);
				r.DrawImage(null, AABB2(pos.x, pos.y, size.x, size.y));

				r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, Hover ? 0.7F : 0.0F);
				DrawOutlinedRect(r, pos.x, pos.y, pos.x + size.x, pos.y + size.y);

				Vector2 txtSize = Font.Measure(Caption);
				Font.DrawShadow(Caption, pos + (size - txtSize) * Vector2(0.0F, 0.5F)
					+ Vector2(4.0F, 0.0F), 1.0F, Vector4(1, 1, 1, 1), Vector4(0, 0, 0, 0.4F));
			}
		}

		class DropDownViewModel : spades::ui::ListViewModel {
			spades::ui::UIManager@ manager;
			string[]@ list;

			DropDownListHandler@ Handler;

			DropDownViewModel(spades::ui::UIManager@ manager, string[]@ list) {
				@this.manager = manager;
				@this.list = list;
			}
			int NumRows {
				get { return int(list.length); }
			}
			private void ItemClicked(spades::ui::UIElement@ sender) {
				Handler(cast<DropDownViewItem>(sender).index);
			}
			spades::ui::UIElement@ CreateElement(int row) {
				DropDownViewItem i(manager);
				i.Caption = list[row];
				i.index = row;
				@i.Activated = spades::ui::EventHandler(this.ItemClicked);
				return i;
			}
			void RecycleElement(spades::ui::UIElement@ elem) {}
		}

		class DropDownBackground : UIElement {
			DropDownView@ view;
			UIElement@ oldRoot;
			DropDownBackground(UIManager@ manager, DropDownView@ view) {
				super(manager);
				IsMouseInteractive = true;
				@this.view = view;
			}
			void Destroy() {
				@Parent = null;
				if (oldRoot !is null)
					oldRoot.Enable = true;
			}
			void MouseDown(MouseButton button, Vector2 clientPosition) {
				view.Destroy();
				view.handler(-1);
			}
			void Render() {
				Renderer@ renderer = Manager.Renderer;
				renderer.ColorNP = Vector4(0.0F, 0.0F, 0.0F, 0.5F);
				renderer.DrawImage(null, ScreenBounds);
				UIElement::Render();
			}
		}

		class DropDownView : ListView {
			DropDownListHandler@ handler;
			DropDownBackground@ bg;

			DropDownView(UIManager@ manager, DropDownListHandler@ handler, string[]@ items) {
				super(manager);
				@this.handler = handler;

				DropDownViewModel model(manager, items);
				@model.Handler = DropDownListHandler(this.ItemActivated);
				@this.Model = model;
			}

			void Destroy() { bg.Destroy(); }

			private void ItemActivated(int index) {
				Destroy();
				handler(index);
			}
		}

		funcdef void DropDownListHandler(int index);

		void ShowDropDownList(UIManager@ manager, float x, float y, float width, string[]@ items, DropDownListHandler@ handler) {
			DropDownView view(manager, handler, items);
			DropDownBackground bg(manager, view);
			@view.bg = bg;

			UIElement@ root = manager.RootElement;
			Vector2 size = root.Size;

			float maxHeight = size.y - y;
			float height = 24.0F * float(items.length);
			if (height > maxHeight)
				height = maxHeight;

			bg.Bounds = AABB2(0.0F, 0.0F, size.x, size.y);
			view.Bounds = AABB2(x, y, width, height);

			UIElement @[] @roots = root.GetChildren();

			root.AddChild(bg);
			bg.AddChild(view);
		}
		void ShowDropDownList(UIElement@ e, string[]@ items, DropDownListHandler@ handler) {
			AABB2 b = e.ScreenBounds;
			ShowDropDownList(e.Manager, b.min.x, b.max.y, b.max.x - b.min.x, items, handler);
		}
	}
}