// (AI-assisted) Controller keyboard for a network game directory URL.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <string>

namespace fe
{
	class UrlKeyboard
	{
	public:
		enum class Action
		{
			Changed,
			Save,
			Cancel
		};
		static constexpr int Columns = 12, Characters = 84, Keys = Characters + 4;
		void Open(const std::string& text)
		{
			m_text = text;
			m_selected = 0;
		}
		const std::string& text() const { return m_text; }
		int selected() const { return m_selected; }
		static std::string Label(int key)
		{
			static constexpr char chars[] = "1234567890./abcdefghijklmnopqrstuvwxyz:_-~ ()[]+ABCDEFGHIJKLMN OPQRSTUVWXYZ@%?#,&=!$";
			static_assert(sizeof(chars) - 1 == Characters);
			if (key < Characters)
				return chars[key] == ' ' ? "Space" : std::string(1, chars[key]);
			static constexpr const char* actions[] = {"Delete", "Clear", "Save", "Cancel"};
			return actions[key - Characters];
		}
		void Move(int horizontal, int vertical)
		{
			const int row = m_selected / Columns;
			const int col = m_selected < Characters ? m_selected % Columns : (m_selected - Characters) * 3;
			if (vertical)
			{
				const int next = std::clamp(row + vertical, 0, 7);
				m_selected = next == 7 ? Characters + col / 3 : next * Columns + col;
			}
			else if (horizontal)
			{
				const int count = row == 7 ? 4 : Columns;
				const int at = m_selected - row * Columns;
				m_selected = row * Columns + (at + horizontal + count) % count;
			}
		}
		Action Activate()
		{
			if (m_selected == Characters + 2)
				return Action::Save;
			if (m_selected == Characters + 3)
				return Action::Cancel;
			if (m_selected == Characters)
				Delete();
			else if (m_selected == Characters + 1)
				m_text.clear();
			else if (m_text.size() < 1024)
			{
				const std::string label = Label(m_selected);
				m_text += label == "Space" ? " " : label;
			}
			return Action::Changed;
		}
		void Delete()
		{
			if (m_text.empty())
				return;
			size_t at = m_text.size() - 1;
			while (at > 0 && (static_cast<unsigned char>(m_text[at]) & 0xc0) == 0x80)
				--at;
			m_text.erase(at);
		}

	private:
		std::string m_text;
		int m_selected = 0;
	};
} // namespace fe
