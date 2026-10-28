#include "stdafx.hpp"

#include "GameConsole.hpp"

#include "FlexEngine.hpp"
#include "Helpers.hpp"
#include "InputManager.hpp"
#include "Localization.hpp"
#include "Time.hpp"

namespace flex
{
	namespace
	{
		const u32 MAX_LOG_LINES = 200;
		const u32 VISIBLE_LOG_LINES = 12;
		const u32 MAX_SUGGESTIONS = 6;
		const real RECENT_LOG_SECONDS = 6.0f;

		bool IsSeparator(char c) { return c == ' ' || c == ',' || c == '\t'; }

		std::string Lower(const std::string& str)
		{
			std::string result = str;
			for (char& c : result) { c = (char)tolower((unsigned char)c); }
			return result;
		}

		i32 EditDistance(const std::string& a, const std::string& b)
		{
			std::vector<i32> prev(b.size() + 1), cur(b.size() + 1);
			for (size_t j = 0; j <= b.size(); ++j) { prev[j] = (i32)j; }
			for (size_t i = 1; i <= a.size(); ++i)
			{
				cur[0] = (i32)i;
				for (size_t j = 1; j <= b.size(); ++j)
				{
					cur[j] = glm::min(glm::min(prev[j] + 1, cur[j - 1] + 1), prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1));
				}
				std::swap(prev, cur);
			}
			return prev[b.size()];
		}
	}

	GameConsole& GameConsole::Get()
	{
		static GameConsole* console = new GameConsole();
		return *console;
	}

	GameConsole::GameConsole()
	{
		RegisterBuiltins();
	}

	i32 GameConsole::FuzzyScore(const std::string& queryIn, const std::string& textIn)
	{
		const std::string query = Lower(queryIn);
		const std::string text = Lower(textIn);
		if (query.empty())
		{
			return 1;
		}
		if (query == text)
		{
			return 1000;
		}
		if (text.compare(0, query.size(), query) == 0)
		{
			return 800 - (i32)(text.size() - query.size());
		}
		if (text.find(query) != std::string::npos)
		{
			return 500 - (i32)(text.size() - query.size());
		}
		// Subsequence (e.g. "tpt" -> "teleport")
		size_t pos = 0;
		i32 gaps = 0;
		bool bAll = true;
		for (char c : query)
		{
			size_t found = text.find(c, pos);
			if (found == std::string::npos) { bAll = false; break; }
			gaps += (i32)(found - pos);
			pos = found + 1;
		}
		if (bAll && query.size() >= 2)
		{
			return glm::max(200 - gaps * 4, 50);
		}
		// A typo or two
		if (query.size() >= 3 && EditDistance(query, text) <= (query.size() >= 5 ? 2 : 1))
		{
			return 100;
		}
		return 0;
	}

	i32 GameConsole::BestMatch(const std::string& query, const std::vector<std::string>& candidates, bool bRequireUnique)
	{
		i32 best = -1;
		i32 bestScore = 0;
		bool bTie = false;
		for (i32 i = 0; i < (i32)candidates.size(); ++i)
		{
			const i32 score = FuzzyScore(query, candidates[i]);
			if (score > bestScore)
			{
				best = i;
				bestScore = score;
				bTie = false;
			}
			else if (score == bestScore && score > 0)
			{
				bTie = true;
			}
		}
		return (bRequireUnique && bTie) ? -1 : best;
	}

	void GameConsole::Register(const std::vector<std::string>& names, const std::string& usage, const std::string& description,
		RunFunction run, SuggestFunction suggest /* = nullptr */)
	{
		Command command;
		command.names = names;
		command.usage = usage;
		command.description = description;
		command.run = std::move(run);
		command.suggest = std::move(suggest);
		// "<a> [b=1]"
		size_t i = 0;
		while (i < usage.size())
		{
			const char open = usage[i];
			if (open == '<' || open == '[')
			{
				const char close = open == '<' ? '>' : ']';
				const size_t end = usage.find(close, i);
				if (end == std::string::npos) { break; }
				Arg arg;
				std::string body = usage.substr(i + 1, end - i - 1);
				arg.bOptional = open == '[';
				const size_t equals = body.find('=');
				if (equals != std::string::npos)
				{
					arg.defaultValue = body.substr(equals + 1);
					body = body.substr(0, equals);
				}
				arg.name = body;
				command.args.push_back(arg);
				i = end + 1;
			}
			else
			{
				++i;
			}
		}
		for (Command& existing : m_Commands)
		{
			if (!existing.names.empty() && !names.empty() && existing.names[0] == names[0])
			{
				existing = command; // Re-registering replaces
				return;
			}
		}
		m_Commands.push_back(command);
	}

	void GameConsole::RegisterGrantable(const std::string& name, GrantFunction give)
	{
		for (auto& pair : m_Grantables)
		{
			if (pair.first == name)
			{
				pair.second = give;
				return;
			}
		}
		m_Grantables.emplace_back(name, give);
	}

	std::vector<std::string> GameConsole::GetGrantableNames() const
	{
		std::vector<std::string> names;
		for (const auto& pair : m_Grantables) { names.push_back(pair.first); }
		return names;
	}

	bool GameConsole::Grant(const std::string& name, i32 count, std::string& outMessage) const
	{
		const std::vector<std::string> names = GetGrantableNames();
		const i32 index = BestMatch(name, names, true);
		if (index < 0)
		{
			return false;
		}
		outMessage = m_Grantables[index].second(count);
		return true;
	}

	std::vector<std::string> GameConsole::Tokenize(const std::string& line)
	{
		std::vector<std::string> tokens;
		std::string current;
		for (char c : line)
		{
			if (IsSeparator(c))
			{
				if (!current.empty()) { tokens.push_back(current); current.clear(); }
			}
			else
			{
				current += c;
			}
		}
		if (!current.empty()) { tokens.push_back(current); }
		return tokens;
	}

	const GameConsole::Command* GameConsole::FindCommand(const std::string& word, bool bFuzzy) const
	{
		for (const Command& command : m_Commands)
		{
			for (const std::string& name : command.names)
			{
				if (Lower(name) == Lower(word)) { return &command; }
			}
		}
		if (!bFuzzy)
		{
			return nullptr;
		}
		const Command* best = nullptr;
		i32 bestScore = 0;
		bool bTie = false;
		for (const Command& command : m_Commands)
		{
			i32 score = 0;
			for (const std::string& name : command.names) { score = glm::max(score, FuzzyScore(word, name)); }
			if (score > bestScore) { best = &command; bestScore = score; bTie = false; }
			else if (score == bestScore && score > 0) { bTie = true; }
		}
		return bTie ? nullptr : best;
	}

	std::vector<std::pair<std::string, std::string>> GameConsole::Suggestions(const std::string& input) const
	{
		std::vector<std::pair<std::string, std::string>> result;
		const std::vector<std::string> tokens = Tokenize(input);
		const bool bTrailingSeparator = !input.empty() && IsSeparator(input.back());
		const u32 argIndex = tokens.empty() ? 0 : (bTrailingSeparator ? (u32)tokens.size() : (u32)tokens.size() - 1);
		if (argIndex == 0)
		{
			const std::string partial = tokens.empty() ? "" : tokens[0];
			std::vector<std::pair<i32, std::pair<std::string, std::string>>> scored;
			for (const Command& command : m_Commands)
			{
				i32 bestScore = 0;
				std::string bestName = command.names[0];
				for (const std::string& name : command.names)
				{
					const i32 score = FuzzyScore(partial, name);
					if (score > bestScore) { bestScore = score; bestName = name; }
				}
				if (bestScore > 0)
				{
					std::string detail = command.usage.empty() ? command.description : command.usage + "  " + command.description;
					scored.push_back({ bestScore, { bestName, detail } });
				}
			}
			std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
			for (const auto& pair : scored) { result.push_back(pair.second); }
		}
		else
		{
			const Command* command = FindCommand(tokens[0], true);
			if (command != nullptr && command->suggest)
			{
				const std::string partial = bTrailingSeparator ? "" : tokens.back();
				const std::vector<std::string> values = command->suggest(argIndex - 1);
				std::vector<std::pair<i32, std::string>> scored;
				for (const std::string& value : values)
				{
					const i32 score = FuzzyScore(partial, value);
					if (score > 0) { scored.push_back({ score, value }); }
				}
				std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
				for (const auto& pair : scored) { result.push_back({ pair.second, "" }); }
			}
		}
		return result;
	}

	std::string GameConsole::ApplySuggestion(const std::string& input, const std::string& suggestion)
	{
		size_t cut = input.size();
		while (cut > 0 && !IsSeparator(input[cut - 1])) { --cut; }
		return input.substr(0, cut) + suggestion + " ";
	}

	void GameConsole::Log(const std::string& text, bool bError /* = false */)
	{
		size_t start = 0;
		while (start <= text.size())
		{
			size_t end = text.find('\n', start);
			if (end == std::string::npos) { end = text.size(); }
			if (end > start || text.empty())
			{
				m_Log.push_back({ text.substr(start, end - start), bError, m_Time });
			}
			start = end + 1;
		}
		while (m_Log.size() > MAX_LOG_LINES) { m_Log.erase(m_Log.begin()); }
	}

	std::string GameConsole::Execute(const std::string& line)
	{
		const std::vector<std::string> tokens = Tokenize(line);
		if (tokens.empty())
		{
			return "";
		}
		const Command* command = FindCommand(tokens[0], true);
		if (command == nullptr)
		{
			std::string message = std::string("!") + LOCF(Console_Unknown, tokens[0].c_str());
			const auto suggestions = Suggestions(tokens[0]);
			if (!suggestions.empty())
			{
				message += std::string("  ") + LOCF(Console_DidYouMean, suggestions[0].first.c_str());
			}
			return message;
		}
		std::vector<std::string> args(tokens.begin() + 1, tokens.end());
		u32 required = 0;
		for (const Arg& arg : command->args) { required += arg.bOptional ? 0 : 1; }
		if (args.size() < required)
		{
			return std::string("!") + LOCF(Console_Usage, command->names[0].c_str(), command->usage.c_str());
		}
		if (args.size() > command->args.size())
		{
			return std::string("!") + LOCF(Console_Usage, command->names[0].c_str(), command->usage.c_str());
		}
		for (u32 i = (u32)args.size(); i < command->args.size(); ++i)
		{
			args.push_back(command->args[i].defaultValue);
		}
		return command->run(args);
	}

	void GameConsole::SetOpen(bool bOpen)
	{
		if (m_bOpen != bOpen)
		{
			m_bOpen = bOpen;
			m_OpenTime = m_Time;
			m_HistoryIndex = -1;
			m_SuggestionBase.clear();
			m_SuggestionIndex = 0;
			if (bOpen) { m_Input.clear(); }
		}
	}

	void GameConsole::RegisterBuiltins()
	{
		Register({ "help", "?", "commands" }, "[command]", "Lists the commands, or describes one",
			[this](const std::vector<std::string>& args) -> std::string
			{
				if (!args[0].empty())
				{
					const Command* command = FindCommand(args[0], true);
					if (command == nullptr)
					{
						return std::string("!") + LOCF(Console_Unknown, args[0].c_str());
					}
					std::string names;
					for (const std::string& name : command->names) { names += (names.empty() ? "" : ", ") + name; }
					return names + " " + command->usage + "\n" + command->description;
				}
				std::string text;
				for (const Command& command : m_Commands)
				{
					std::string names;
					for (const std::string& name : command.names) { names += (names.empty() ? "" : "/") + name; }
					text += names + (command.usage.empty() ? "" : " " + command.usage) + " - " + command.description + "\n";
				}
				text += LOC(Console_HelpFooter);
				return text;
			},
			[this](u32) { std::vector<std::string> names; for (const Command& c : m_Commands) { names.push_back(c.names[0]); } return names; });
		Register({ "clear", "cls" }, "", "Clears the console", [this](const std::vector<std::string>&) { m_Log.clear(); return std::string(); });
	}

	void GameConsole::Draw(GameUI& ui)
	{
		m_Time += g_UnpausedDeltaTime;
		const real s = ui.Scale();
		const real margin = 40.0f * s;
		const real panelWidth = glm::min(1000.0f * s, ui.Width() - margin * 2.0f);
		const real lineHeight = 26.0f * s;
		const real textSize = UITheme::TEXT_BODY * s * 0.9f;

		if (!m_bOpen)
		{
			// Recent output stays up for a moment (e.g. a command's result after the console is closed)
			real y = margin;
			for (const Line& line : m_Log)
			{
				const real age = m_Time - line.time;
				if (age < RECENT_LOG_SECONDS)
				{
					const real alpha = glm::clamp((RECENT_LOG_SECONDS - age) * 0.5f, 0.0f, 1.0f);
					ui.Text(line.text, margin, y + lineHeight * 0.5f, textSize, UITheme::WithAlpha(line.bError ? UITheme::CLAY : UITheme::TEXT_PRIMARY, alpha));
					y += lineHeight;
				}
			}
			return;
		}

		// Input
		bool bTyped = false;
		for (char c : g_InputManager->GetTypedText())
		{
			if (c != '`' && c != '~' && c >= 32 && m_Input.size() < 200)
			{
				m_Input += c;
				bTyped = true;
			}
		}
		if (g_InputManager->GetKeyPressed(KeyCode::KEY_BACKSPACE, true) && !m_Input.empty())
		{
			m_Input.pop_back();
			bTyped = true;
		}
		if (bTyped)
		{
			m_SuggestionBase.clear();
			m_SuggestionIndex = 0;
			m_HistoryIndex = -1;
		}
		if (g_InputManager->GetKeyPressed(KeyCode::KEY_TAB, true))
		{
			if (m_SuggestionBase.empty()) { m_SuggestionBase = m_Input; m_SuggestionIndex = 0; }
			else { ++m_SuggestionIndex; }
			const auto suggestions = Suggestions(m_SuggestionBase);
			if (!suggestions.empty())
			{
				m_SuggestionIndex %= (i32)suggestions.size();
				m_Input = ApplySuggestion(m_SuggestionBase, suggestions[m_SuggestionIndex].first);
			}
		}
		if (g_InputManager->GetKeyPressed(KeyCode::KEY_UP, true) && !m_History.empty())
		{
			m_HistoryIndex = glm::min(m_HistoryIndex + 1, (i32)m_History.size() - 1);
			m_Input = m_History[m_History.size() - 1 - m_HistoryIndex];
			m_SuggestionBase.clear();
		}
		if (g_InputManager->GetKeyPressed(KeyCode::KEY_DOWN, true) && m_HistoryIndex >= 0)
		{
			--m_HistoryIndex;
			m_Input = m_HistoryIndex >= 0 ? m_History[m_History.size() - 1 - m_HistoryIndex] : "";
			m_SuggestionBase.clear();
		}
		if (g_InputManager->GetKeyPressed(KeyCode::KEY_ENTER, true) || g_InputManager->GetKeyPressed(KeyCode::KEY_KP_ENTER, true))
		{
			if (!m_Input.empty())
			{
				const std::string line = m_Input;
				m_Input.clear();
				m_SuggestionBase.clear();
				m_HistoryIndex = -1;
				if (m_History.empty() || m_History.back() != line) { m_History.push_back(line); }
				Log("> " + line);
				const std::string output = Execute(line);
				if (!output.empty())
				{
					if (output[0] == '!') { Log(output.substr(1), true); }
					else { Log(output); }
				}
			}
		}

		const std::string query = m_SuggestionBase.empty() ? m_Input : m_SuggestionBase;
		const auto suggestions = Suggestions(query);
		const std::vector<std::string> tokens = Tokenize(m_Input);
		const bool bTrailing = !m_Input.empty() && IsSeparator(m_Input.back());
		const u32 argIndex = tokens.empty() ? 0 : (bTrailing ? (u32)tokens.size() : (u32)tokens.size() - 1);
		const Command* command = nullptr;
		if (!tokens.empty() && (argIndex > 0 || FindCommand(tokens[0], false) != nullptr))
		{
			command = FindCommand(tokens[0], true);
		}

		const u32 logLines = glm::min((u32)m_Log.size(), VISIBLE_LOG_LINES);
		const u32 shownSuggestions = glm::min((u32)suggestions.size(), MAX_SUGGESTIONS);
		const real inputHeight = 50.0f * s;
		const real hintHeight = lineHeight + 4.0f * s;
		const real height = 16.0f * s + logLines * lineHeight + inputHeight + hintHeight + shownSuggestions * lineHeight + 12.0f * s;
		const UIRect panel(margin, margin, panelWidth, height);
		ui.Panel(panel);

		real y = panel.y + 12.0f * s;
		const real x = panel.x + 20.0f * s;
		for (u32 i = (u32)m_Log.size() - logLines; i < (u32)m_Log.size(); ++i)
		{
			const Line& line = m_Log[i];
			const glm::vec4 colour = line.bError ? UITheme::CLAY : (line.text.compare(0, 2, "> ") == 0 ? UITheme::TEXT_SECONDARY : UITheme::TEXT_PRIMARY);
			ui.Text(line.text, x, y + lineHeight * 0.5f, textSize, colour);
			y += lineHeight;
		}

		const UIRect inputRect(panel.x + 12.0f * s, y + 4.0f * s, panel.w - 24.0f * s, inputHeight - 8.0f * s);
		ui.FillRect(inputRect, UITheme::RAISED, UITheme::CORNER_RADIUS * s);
		ui.FillRect(UIRect(inputRect.x + 4.0f * s, inputRect.y + inputRect.h - 3.0f * s, inputRect.w - 8.0f * s, 3.0f * s), UITheme::EMBER, 1.5f * s);
		const real inputY = inputRect.y + inputRect.h * 0.5f;
		ui.Text(">", inputRect.x + 14.0f * s, inputY, UITheme::TEXT_BODY * s, UITheme::EMBER);
		const real inputX = inputRect.x + 38.0f * s;
		if (m_Input.empty())
		{
			ui.Text(LOC(Console_Placeholder), inputX, inputY, UITheme::TEXT_BODY * s, UITheme::TEXT_DISABLED);
		}
		else
		{
			ui.Text(m_Input, inputX, inputY, UITheme::TEXT_BODY * s, UITheme::TEXT_PRIMARY);
		}
		if (glm::fract(m_Time * 1.2f) < 0.6f)
		{
			const real caretX = inputX + (m_Input.empty() ? 0.0f : ui.MeasureText(m_Input, UITheme::TEXT_BODY * s)) + 2.0f * s;
			ui.FillRect(UIRect(caretX, inputY - 13.0f * s, 2.0f * s, 26.0f * s), UITheme::EMBER);
		}
		y += inputHeight;

		// Hint: the command's arguments, with the one being typed highlighted
		if (command != nullptr)
		{
			real hx = x;
			const real hy = y + lineHeight * 0.5f;
			std::string head = command->names[0] + " ";
			ui.Text(head, hx, hy, textSize, UITheme::TEXT_PRIMARY);
			hx += ui.MeasureText(head, textSize);
			u32 argNumber = 0;
			size_t i = 0;
			const std::string& usage = command->usage;
			while (i < usage.size())
			{
				if (usage[i] == '<' || usage[i] == '[')
				{
					const size_t end = usage.find(usage[i] == '<' ? '>' : ']', i);
					if (end == std::string::npos) { break; }
					const std::string word = usage.substr(i, end - i + 1) + " ";
					const bool bCurrent = argNumber + 1 == argIndex || (argIndex == 0 && false);
					ui.Text(word, hx, hy, textSize, bCurrent ? UITheme::EMBER_GLOW : (argNumber + 1 < argIndex ? UITheme::TEXT_DISABLED : UITheme::TEXT_SECONDARY));
					hx += ui.MeasureText(word, textSize);
					++argNumber;
					i = end + 1;
				}
				else
				{
					++i;
				}
			}
			ui.Text("- " + command->description, hx + 8.0f * s, hy, textSize, UITheme::TEXT_DISABLED);
		}
		else if (!m_Input.empty() && suggestions.empty())
		{
			ui.Text(LOC(Console_NoMatch), x, y + lineHeight * 0.5f, textSize, UITheme::TEXT_DISABLED);
		}
		y += hintHeight;

		const i32 highlighted = m_SuggestionBase.empty() ? 0 : m_SuggestionIndex % glm::max((i32)suggestions.size(), 1);
		for (u32 i = 0; i < shownSuggestions; ++i)
		{
			const bool bHighlighted = (i32)i == highlighted;
			if (bHighlighted)
			{
				ui.FillRect(UIRect(panel.x + 12.0f * s, y, panel.w - 24.0f * s, lineHeight), UITheme::WithAlpha(UITheme::EMBER, 0.18f), 4.0f * s);
			}
			ui.Text(suggestions[i].first, x + 20.0f * s, y + lineHeight * 0.5f, textSize, bHighlighted ? UITheme::EMBER_GLOW : UITheme::TEXT_PRIMARY);
			if (!suggestions[i].second.empty())
			{
				ui.Text(suggestions[i].second, x + 220.0f * s, y + lineHeight * 0.5f, textSize * 0.9f, UITheme::TEXT_DISABLED);
			}
			y += lineHeight;
		}
	}
} // namespace flex
