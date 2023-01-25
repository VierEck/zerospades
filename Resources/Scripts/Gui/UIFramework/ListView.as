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

		class ListViewModel {
			int NumRows {
				get { return 0; }
			}
			UIElement@ CreateElement(int row) { return null; }
			void RecycleElement(UIElement@ elem) {}
		}

		/** Simple virtual stack panel implementation. */
		class ListViewBase : UIElement {
			private ScrollBar@ scrollBar;
			private ListViewModel@ model;
			float RowHeight = 24.0F;
			float ScrollBarWidth = 16.0F;
			private UIElementDeque items;
			private int loadedStartIndex = 0;

			ListViewBase(UIManager@ manager) {
				super(manager);
				@scrollBar = ScrollBar(Manager);
				scrollBar.Bounds = AABB2();
				AddChild(scrollBar);
				IsMouseInteractive = true;

				@scrollBar.Changed = EventHandler(this.OnScrolled);
				@model = ListViewModel();
			}

			private void OnScrolled(UIElement@ sender) { Layout(); }

			int NumVisibleRows { get final { return int(floor(Size.y / RowHeight)); } }
			int MaxTopRowIndex { get final { return Max(0, model.NumRows - NumVisibleRows); } }

			int TopRowIndex {
				get final {
					int idx = int(floor(scrollBar.Value + 0.5));
					return Clamp(idx, 0, MaxTopRowIndex);
				}
			}

			void OnResized() {
				Layout();
				UIElement::OnResized();
			}

			void Layout() {
				scrollBar.MaxValue = double(MaxTopRowIndex);
				scrollBar.ScrollTo(scrollBar.Value); // ensures value is in range
				scrollBar.LargeChange = double(NumVisibleRows);

				int numRows = model.NumRows;

				// load items
				int visibleStart = TopRowIndex;
				int visibleEnd = Min(visibleStart + NumVisibleRows, numRows);
				int loadedStart = loadedStartIndex;
				int loadedEnd = loadedStartIndex + items.Count;

				if (items.Count == 0 or visibleStart >= loadedEnd or visibleEnd <= loadedStart) {
					// full reload
					UnloadAll();
					for (int i = visibleStart; i < visibleEnd; i++) {
						items.PushBack(model.CreateElement(i));
						AddChild(items.Back);
					}
					loadedStartIndex = visibleStart;
				} else {
					while (loadedStart < visibleStart) {
						RemoveChild(items.Front);
						model.RecycleElement(items.Front);
						items.PopFront();
						loadedStart++;
					}
					while (loadedEnd > visibleEnd) {
						RemoveChild(items.Back);
						model.RecycleElement(items.Back);
						items.PopBack();
						loadedEnd--;
					}
					while (visibleStart < loadedStart) {
						loadedStart--;
						items.PushFront(model.CreateElement(loadedStart));
						AddChild(items.Front);
					}
					while (visibleEnd > loadedEnd) {
						items.PushBack(model.CreateElement(loadedEnd));
						AddChild(items.Back);
						loadedEnd++;
					}
					loadedStartIndex = loadedStart;
				}

				// relayout items
				UIElementDeque@ items = this.items;
				int count = items.Count;
				float y = 0.0F;
				float w = ItemWidth;
				for (int i = 0; i < count; i++) {
					items[i].Bounds = AABB2(0.0F, y, w, RowHeight);
					y += RowHeight;
				}

				// move scroll bar
				scrollBar.Bounds = AABB2(Size.x - ScrollBarWidth, 0.0F, ScrollBarWidth, Size.y);
			}

			float ItemWidth {
				get { return Size.x - ScrollBarWidth; }
			}

			void MouseWheel(float delta) { scrollBar.ScrollBy(delta); }

			void Reload() {
				UnloadAll();
				Layout();
			}

			private void UnloadAll() {
				UIElementDeque@ items = this.items;
				int count = items.Count;
				for (int i = 0; i < count; i++) {
					RemoveChild(items[i]);
					model.RecycleElement(items[i]);
				}
				items.Clear();
			}

			ListViewModel@ Model {
				get final { return model; }
				set {
					UnloadAll();
					@model = value;
					Layout();
				}
			}

			void ScrollToTop() { scrollBar.ScrollTo(0.0); }
			void ScrollToEnd() { scrollBar.ScrollTo(scrollBar.MaxValue); }
		}

		class ListView : ListViewBase {
			ListView(UIManager@ manager) { super(manager); }
			void Render() {
				// render background
				Renderer@ r = Manager.Renderer;
				Vector2 pos = ScreenPosition;
				Vector2 size = Size;

				r.ColorNP = Vector4(0.0F, 0.0F, 0.0F, 0.2F);
				r.DrawImage(null, AABB2(pos.x, pos.y, size.x, size.y));

				r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.06F);
				DrawOutlinedRect(r, pos.x, pos.y, pos.x + size.x, pos.y + size.y);

				ListViewBase::Render();
			}
		}
	}
}